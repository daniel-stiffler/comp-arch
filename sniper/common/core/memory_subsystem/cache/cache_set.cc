#include "cache.h"  // Forward declared the class
#include "cache_set.h"
#include "cache_set_lru.h"

#include <algorithm>
#include <cassert>

#include "config.h"
#include "config.hpp"
#include "log.h"
#include "simulator.h"
#include "stats.h"

std::unique_ptr<CacheSet> CacheSet::createCacheSet(
    UInt32 set_index, String cfgname, core_id_t core_id,
    String replacement_policy, CacheBase::cache_t cache_type,
    UInt32 associativity, UInt32 blocksize,
    CacheCompressionCntlr* compress_cntlr, const Cache* parent_cache,
    CacheSetInfo* set_info) {

  CacheBase::ReplacementPolicy policy = parsePolicyType(replacement_policy);

  switch (policy) {
    case CacheBase::LRU:
    // Fall through
    case CacheBase::LRU_QBS: {
      CacheSetLRU* tmp = new CacheSetLRU(
          set_index, cache_type, associativity, blocksize, compress_cntlr,
          parent_cache, dynamic_cast<CacheSetInfoLRU*>(set_info),
          getNumQBSAttempts(policy, cfgname, core_id));

      return std::unique_ptr<CacheSet>(tmp);
    } break;

    default:
      LOG_PRINT_ERROR(
          "Unrecognized or unsupported cache replacement policy: %i", policy);
  }
}

std::unique_ptr<CacheSetInfo> CacheSet::createCacheSetInfo(
    String name, String cfgname, core_id_t core_id, String replacement_policy,
    UInt32 associativity, CacheCompressionCntlr* compress_cntlr) {

  CacheBase::ReplacementPolicy policy = parsePolicyType(replacement_policy);

  switch (policy) {
    case CacheBase::LRU:
    // Fall through
    case CacheBase::LRU_QBS: {
      CacheSetInfoLRU* tmp =
          new CacheSetInfoLRU(name, cfgname, core_id, associativity,
                              getNumQBSAttempts(policy, cfgname, core_id));

      return std::unique_ptr<CacheSetInfo>(tmp);
    } break;

    default:
      LOG_PRINT_ERROR(
          "Unrecognized or unsupported cache replacement policy: %i", policy);
  }
}

CacheSet::CacheSet(UInt32 set_index, CacheBase::cache_t cache_type,
                   UInt32 associativity, UInt32 blocksize,
                   CacheCompressionCntlr* compress_cntlr,
                   const Cache* parent_cache)
    : m_associativity(associativity),
      m_blocksize(blocksize),
      m_compress_cntlr(compress_cntlr),
      m_superblock_info_ways(associativity),
      m_parent_cache(parent_cache),
      m_evict_bc_write(0) {

  bool is_compressible;
  if (compress_cntlr != nullptr && compress_cntlr->canCompress()) {
    is_compressible = true;
  } else {
    is_compressible = false;
  }

  // Create the objects containing block data
  for (UInt32 i = 0; i < m_associativity; ++i) {
    m_data_ways.emplace_back(i, set_index, m_blocksize, parent_cache,
                             is_compressible);
  }

  core_id_t core_id = m_parent_cache->getCoreId();
  String cache_name = m_parent_cache->getName();

  std::string stat_name =
      std::string("evict_bc_write_s") + std::to_string(set_index);
  registerStatsMetric(cache_name, core_id, stat_name.c_str(),
                      &m_evict_bc_write);
}

CacheSet::~CacheSet() {
  // RAII takes care of destructing everything for us
}

void CacheSet::readLine(UInt32 way, UInt32 block_id, UInt32 offset,
                        UInt32 bytes, bool update_replacement, Byte* rd_data) {

  assert(offset + bytes <= m_blocksize);

  if (!((rd_data == nullptr && offset == 0 && bytes == 0) ||
        (rd_data != nullptr))) {
    LOG_PRINT_WARNING(
        "TODO: CacheSet readLine failed nullptr condition, so manually setting "
        "offset and bytes for now");
    offset = 0;
    bytes  = 0;
  }

  const SuperblockInfo& superblock_info = m_superblock_info_ways[way];
  assert(superblock_info.isValid(block_id));

  const BlockData& super_data = m_data_ways[way];
  super_data.readBlockData(block_id, offset, bytes, rd_data);

  if (update_replacement) updateReplacementWay(way);
}

void CacheSet::writeLine(IntPtr tag, UInt32 block_id, UInt32 offset,
                         const Byte* wr_data, UInt32 bytes,
                         bool update_replacement, WritebackLines* writebacks,
                         CacheCntlr* cntlr) {

  assert(offset + bytes <= m_blocksize);
  assert((wr_data == nullptr && offset == 0 && bytes == 0) ||
         (wr_data != nullptr));

  UInt32 init_way;
  LOG_ASSERT_ERROR(find(tag, block_id, &init_way) != nullptr,
                   "Attempting to write non-resident line");

  SuperblockInfo& superblock_info = m_superblock_info_ways[init_way];
  BlockData& super_data           = m_data_ways[init_way];

  LOG_PRINT(
      "(%s->%p): BEGIN writing line tag: %lx init_way: %u block_id: %u offset: "
      "%u wr_data: %p bytes: %u, %u writebacks scheduled",
      m_parent_cache->getName().c_str(), this, tag, init_way, block_id, offset,
      wr_data, bytes, writebacks->size());

  UInt32 final_way;

  if (super_data.canWriteBlockData(block_id, offset, wr_data, bytes,
                                   m_compress_cntlr)) {
    super_data.writeBlockData(block_id, offset, wr_data, bytes,
                              m_compress_cntlr);
    final_way = init_way;
  } else {
    /*
     * Current block cannot be modified in memory without changing the
     * compression factor.  Therefore, we will pretend to evict it from it's
     * current superblock and re-insert it according to the replacement policy.
     * This re-insertion task leverages CacheSet::insertLine and simply passes
     * the eviction references through.
     */

    ++m_evict_bc_write;

    // Current (modified) block data
    std::unique_ptr<Byte> mod_block_data(new Byte[m_blocksize]);
    super_data.evictBlockData(block_id, mod_block_data.get(), m_compress_cntlr);

    CacheBlockInfoUPtr mod_block_info =
        superblock_info.evictBlockInfo(block_id);

    // Manually update the cache line contents
    std::copy_n(wr_data + offset, bytes, mod_block_data.get());

    LOG_PRINT(
        "(%s->%p): Writing line caused evictions, now preparing to re-insert "
        "the updated line",
        m_parent_cache->getName().c_str(), this);

    insertLine(std::move(mod_block_info), mod_block_data.get(), writebacks,
               cntlr);

    LOG_ASSERT_ERROR(find(tag, block_id, &final_way),
                     "Could not find the line just re-placed");
  }

  if (update_replacement) updateReplacementWay(final_way);
}

CacheBlockInfo* CacheSet::find(IntPtr tag, UInt32 block_id, UInt32* way) const {
  for (UInt32 tmp_way = 0; tmp_way < m_associativity; ++tmp_way) {
    const SuperblockInfo& superblock_info = m_superblock_info_ways[tmp_way];

    UInt32 tmp_block_id;
    if (superblock_info.compareTags(tag, &tmp_block_id)) {
      LOG_ASSERT_ERROR(
          block_id == tmp_block_id,
          "Found a matching block (tag:%lx block_id:%u) in the wrong place %u",
          tag, block_id, tmp_block_id);

      if (way != nullptr) *way = tmp_way;

      CacheBlockInfo* peek_block_info = superblock_info.peekBlock(block_id);
      return peek_block_info;
    }
  }

  return nullptr;
}

void CacheSet::invalidate(IntPtr tag, UInt32 block_id) {
  UInt32 inv_way;
  CacheBlockInfo* inv_block_info = find(tag, block_id, &inv_way);

  if (inv_block_info != nullptr) {
    m_superblock_info_ways[inv_way].invalidateBlockInfo(tag, block_id);
    m_data_ways[inv_way].invalidateBlockData(block_id, m_compress_cntlr);
  } else {
    LOG_PRINT_WARNING(
        "Attempted to invalidate tag: %lx block_id: %u but no lines were "
        "touched",
        tag, block_id);
  }
}

void CacheSet::insertLine(CacheBlockInfoUPtr ins_block_info,
                          const Byte* ins_data, WritebackLines* writebacks,
                          CacheCntlr* cntlr) {

  assert(ins_block_info.get() != nullptr);
  assert(writebacks != nullptr);

  IntPtr ins_addr = m_parent_cache->tagToAddress(ins_block_info->getTag());
  IntPtr ins_supertag;
  UInt32 ins_block_id;
  m_parent_cache->splitAddress(ins_addr, nullptr, &ins_supertag, nullptr,
                               &ins_block_id, nullptr);

  LOG_PRINT(
      "(%s->%p): BEGIN inserting line addr: %lx ins_supertag: %lx "
      "ins_block_id: %u ins_data: %p, %u writebacks scheduled",
      m_parent_cache->getName().c_str(), this, ins_addr, ins_supertag,
      ins_block_id, ins_data, writebacks->size());

  /*
   * First insert attempt: scan through superblocks and test to see if
   * ins_block_info can be inserted using canInsertBlockInfo.  If there is
   * a superblock match, additionally check to see if the new data can be
   * compressed with the line as well.
   */
  for (UInt32 i = 0; i < m_associativity; ++i) {
    SuperblockInfo& merge_superblock_info = m_superblock_info_ways[i];
    BlockData& merge_data                 = m_data_ways[i];

    if (merge_superblock_info.canInsertBlockInfo(ins_supertag, ins_block_id,
                                                 ins_block_info.get()) &&
        merge_data.canInsertBlockData(ins_block_id, ins_data,
                                      m_compress_cntlr)) {

      merge_data.insertBlockData(ins_block_id, ins_data, m_compress_cntlr);
      merge_superblock_info.insertBlockInfo(ins_supertag, ins_block_id,
                                            std::move(ins_block_info));

      LOG_PRINT("(%s->%p): END inserting line, merged into existing at way: %u",
                m_parent_cache->getName().c_str(), this, i);

      return;
    }
  }

  /*
   * This replacement strategy does not take into account the fact that cache
   * blocks can be voluntarily flushed or invalidated due to another write
   * request.
   *
   * TODO:
   *   There is a particularly nasty edge-case here in which all ways contain
   *   a line that is in the SHARED_UPGRADING coherence state.  This would
   *   prevent the replacement engine from finding any suitable victim and
   *   crashes the simulator.
   */
  const UInt32 repl_way           = getReplacementWay(cntlr);
  SuperblockInfo& superblock_info = m_superblock_info_ways[repl_way];
  BlockData& super_data           = m_data_ways[repl_way];

  LOG_PRINT("(%s->%p): Inserting line causes evictions repl_way: %u %s",
            m_parent_cache->getName().c_str(), this, repl_way,
            superblock_info.dump().c_str());

  /*
   * Second insert attempt: kick out every cache block in the superblock
   * picked for replacement; this way we are guaranteed to have an empty space
   * for the new data.
   */
  for (UInt32 i = 0; i < SUPERBLOCK_SIZE; ++i) {
    if (superblock_info.isValid(i)) {
      std::unique_ptr<Byte> evict_block_data(new Byte[m_blocksize]);

      CacheBlockInfoUPtr evict_block_info = superblock_info.evictBlockInfo(i);
      IntPtr evict_addr =
          m_parent_cache->tagToAddress(evict_block_info->getTag());

      super_data.evictBlockData(i, evict_block_data.get(), m_compress_cntlr);

      // Allocate and construct a new WritebackTuple in the vector
      writebacks->emplace_back(evict_addr, std::move(evict_block_info),
                               std::move(evict_block_data));
    }
  }
  assert(!superblock_info.isValid());  // Ensure the superblock is now empty

  super_data.insertBlockData(ins_block_id, ins_data, m_compress_cntlr);
  superblock_info.insertBlockInfo(ins_supertag, ins_block_id,
                                  std::move(ins_block_info));

  LOG_PRINT(
      "(%s->%p): END inserting line with evictions, %u writebacks scheduled",
      m_parent_cache->getName().c_str(), this, writebacks->size());
}

UInt8 CacheSet::getNumQBSAttempts(CacheBase::ReplacementPolicy policy,
                                  String cfgname, core_id_t core_id) {
  switch (policy) {
    case CacheBase::LRU_QBS:
      return Sim()->getCfg()->getIntArray(cfgname + "/qbs/attempts", core_id);
    default:
      return 1;
  }
}

CacheBase::ReplacementPolicy CacheSet::parsePolicyType(String policy) {
  if (policy == "round_robin") return CacheBase::ROUND_ROBIN;
  if (policy == "lru") return CacheBase::LRU;
  if (policy == "lru_qbs") return CacheBase::LRU_QBS;
  if (policy == "nru") return CacheBase::NRU;
  if (policy == "mru") return CacheBase::MRU;
  if (policy == "nmru") return CacheBase::NMRU;
  if (policy == "plru") return CacheBase::PLRU;
  if (policy == "srrip") return CacheBase::SRRIP;
  if (policy == "srrip_qbs") return CacheBase::SRRIP_QBS;
  if (policy == "random") return CacheBase::RANDOM;

  LOG_PRINT_ERROR("Unknown replacement policy %s", policy.c_str());
}

bool CacheSet::isValidReplacement(UInt32 way) {
  return m_superblock_info_ways[way].isValidReplacement();
}
