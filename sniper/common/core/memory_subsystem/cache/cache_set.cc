#include <cassert>

#include "cache_set.h"
#include "cache_set_lru.h"

#include "config.h"
#include "config.hpp"
#include "log.h"
#include "simulator.h"

std::unique_ptr<CacheSet> CacheSet::createCacheSet(
    String cfgname, core_id_t core_id, String replacement_policy,
    CacheBase::cache_t cache_type, UInt32 associativity, UInt32 blocksize,
    bool compressed, CacheSetInfo* set_info) {

  CacheBase::ReplacementPolicy policy = parsePolicyType(replacement_policy);

  switch (policy) {
    case CacheBase::LRU:
    // Fall through
    case CacheBase::LRU_QBS: {
      CacheSet* tmp =
          new CacheSetLRU(cache_type, associativity, blocksize, compressed,
                          dynamic_cast<CacheSetInfoLRU*>(set_info),
                          getNumQBSAttempts(policy, cfgname, core_id));

      return std::unique_ptr<CacheSet>(tmp);
    } break;  // Not necessary

    default:
      LOG_PRINT_ERROR(
          "Unrecognized or unsupported cache replacement policy: %i", policy);
      assert(false);
  }
}

std::unique_ptr<CacheSetInfo> CacheSet::createCacheSetInfo(
    String name, String cfgname, core_id_t core_id, String replacement_policy,
    UInt32 associativity, bool compressible) {

  CacheBase::ReplacementPolicy policy = parsePolicyType(replacement_policy);

  switch (policy) {
    case CacheBase::LRU:
    // Fall through
    case CacheBase::LRU_QBS: {
      CacheSetInfo* tmp = new CacheSetInfoLRU(
          name, cfgname, core_id, associativity, compressible,
          getNumQBSAttempts(policy, cfgname, core_id));

      return std::unique_ptr<CacheSetInfo>(tmp);
    } break;  // Not necessary

    default:
      LOG_PRINT_ERROR(
          "Unrecognized or unsupported cache replacement policy: %i", policy);
      assert(false);
  }
}

CacheSet::CacheSet(CacheBase::cache_t cache_type, UInt32 associativity,
                   UInt32 blocksize, bool compressible)
    : m_associativity(associativity),
      m_blocksize(blocksize),
      m_compressible(compressible),
      m_superblock_info_ways(associativity) {

  for (auto& superblock_info : m_superblock_info_ways) {
    for (UInt32 i = 0; i < SUPERBLOCK_SIZE; ++i) {
      CacheBlockInfoUPtr tmp_block_info =
          std::move(CacheBlockInfo::create(cache_type));

      // Replace dummy object from initialization with one specialized to
      // cache type
      superblock_info.swapBlockInfo(i, tmp_block_info);
      tmp_block_info.release();  // Explicitly destroy the dummy objects
    }
  }

  // Create the objects containing block data
  for (UInt32 i = 0; i < m_associativity; ++i) {
    m_data_ways.emplace_back(m_blocksize);
  }
}

CacheSet::~CacheSet() {
  // RAII takes care of destructing everything for us
}

void CacheSet::readLine(UInt32 way, UInt32 block_id, UInt32 offset,
                        UInt32 bytes, bool update_replacement, Byte* rd_data) {

  assert(offset + bytes <= m_blocksize);

  const SuperblockInfo& superblock_info = m_superblock_info_ways[way];
  assert(superblock_info.isValid(block_id));

  const BlockData& super_data = m_data_ways[way];
  super_data.decompress(block_id, offset, bytes, rd_data);

  if (update_replacement) updateReplacementWay(way);
}

void CacheSet::writeLine(UInt32 way, UInt32 block_id, UInt32 offset,
                         const Byte* wr_data, UInt32 bytes,
                         bool update_replacement, WritebackLines* writebacks,
                         CacheCntlr* cntlr) {
  assert(offset + bytes <= m_blocksize);
  assert(wr_data != nullptr);

  SuperblockInfo& superblock_info = m_superblock_info_ways[way];
  assert(superblock_info.isValid(block_id));

  BlockData& super_data = m_data_ways[way];

  if (super_data.isCompressible(block_id, offset, wr_data, bytes)) {
    super_data.compress(block_id, offset, wr_data, bytes);

    if (update_replacement) updateReplacementWay(way);
  } else {
    /*
     * Current block cannot be modified in memory without changing the
     * compression factor.  Therefore, we will pretend to evict it from it's
     * current superblock and re-insert it according to the replacement policy.
     * This re-insertion task leverages CacheSet::insertLine and simply passes
     * the eviction references through.
     */

    // Current (modified) block data
    std::unique_ptr<Byte> mod_block_data(new Byte[m_blocksize]);
    super_data.evictBlockData(block_id, mod_block_data.get());

    CacheBlockInfoUPtr mod_block_info =
        std::move(superblock_info.evictBlockInfo(block_id));

    insertLine(std::move(mod_block_info), mod_block_data.get(), writebacks,
               cntlr);
  }
}

CacheBlockInfo* CacheSet::find(IntPtr tag, UInt32* way, UInt32* block_id) {
  for (UInt32 tmp_way = 0; tmp_way < m_associativity; ++tmp_way) {
    const SuperblockInfo& superblock_info = m_superblock_info_ways[tmp_way];

    UInt32 tmp_block_id;
    if (superblock_info.compareTags(tag, &tmp_block_id)) {
      if (block_id != nullptr) *block_id = tmp_block_id;
      if (way != nullptr) *way           = tmp_way;

      return superblock_info.peekBlock(tmp_block_id);
    }
  }

  return nullptr;
}

bool CacheSet::invalidate(IntPtr tag) {
  for (auto& superblock_info : m_superblock_info_ways) {
    if (superblock_info.invalidate(tag)) return true;
  }

  return false;
}

void CacheSet::insertLine(CacheBlockInfoUPtr ins_block_info,
                          const Byte* ins_data, WritebackLines* writebacks,
                          CacheCntlr* cntlr) {

  assert(ins_block_info.get() != nullptr);
  assert(ins_data != nullptr);
  assert(writebacks != nullptr);

  /*
   * First insert attempt: scan through superblocks and test to see if
   * ins_block_info can be inserted using canInsertBlockInfo.  If there is
   * a superblock match, additionally check to see if the new data can be
   * compressed with the line as well.
   */
  for (UInt32 i = 0; i < m_associativity; ++i) {
    SuperblockInfo& merge_superblock_info = m_superblock_info_ways[i];
    BlockData& merge_data                 = m_data_ways[i];
    UInt32 merge_block_id;

    // TODO: arbitrate between schemes, possibly using SuperblockInfo
    if (merge_superblock_info.canInsertBlockInfo(ins_block_info.get(),
                                                 &merge_block_id) &&
        merge_data.isCompressible(merge_block_id, 0, ins_data, m_blocksize)) {

      merge_data.insertBlockData(merge_block_id, ins_data);
      merge_superblock_info.insertBlockInfo(merge_block_id,
                                            std::move(ins_block_info));
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

  /*
   * Second insert attempt: kick out every cache block in the superblock
   * picked for replacement; this way we are guaranteed to have an empty space
   * for the new data.
   */
  for (UInt32 i = 0; i < SUPERBLOCK_SIZE; ++i) {
    if (superblock_info.isValid(i)) {
      std::unique_ptr<Byte> evict_block_data(new Byte[m_blocksize]);

      CacheBlockInfoUPtr evict_block_info = superblock_info.evictBlockInfo(i);
      IntPtr evict_addr = tagToAddress(evict_block_info->getTag(), m_blocksize);
      super_data.evictBlockData(i, evict_block_data.get());

      // Allocate and construct a new WritebackTuple in the vector
      writebacks->emplace_back(evict_addr, std::move(evict_block_info),
                               std::move(evict_block_data));
    }
  }
  assert(!superblock_info.isValid());  // Ensure the superblock is now empty

  UInt32 repl_block_id;
  (void)superblock_info.canInsertBlockInfo(ins_block_info.get(),
                                           &repl_block_id);

  super_data.insertBlockData(repl_block_id, ins_data);
  superblock_info.insertBlockInfo(repl_block_id, std::move(ins_block_info));
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
