#include <cassert>

#include "cache_base.h"
#include "compress_cache_set.h"
#include "compress_cache_set_lru.h"
#include "config.h"
#include "config.hpp"
#include "fixed_sizes.h"
#include "log.h"
#include "simulator.h"

std::unique_ptr<CompressCacheSet> CompressCacheSet::createCompressCacheSet(
    String cfgname, core_id_t core_id, String replacement_policy,
    CacheBase::cache_t cache_type, UInt32 associativity, UInt32 blocksize,
    CacheSetInfo* set_info) {

  CacheBase::ReplacementPolicy policy = parsePolicyType(replacement_policy);

  CacheSetInfoLRU* set_info_lru = dynamic_cast<CacheSetInfoLRU*>(set_info);

  switch (policy) {
    case CacheBase::LRU:
    // Fall-through
    case CacheBase::LRU_QBS:
      return std::unique_ptr<CompCacheSet>(
          new CacheSetLRU(cache_type, associativity, blocksize, set_info_lru,
                          getNumQBSAttempts(policy, cfgname, core_id)));
    default:
      LOG_PRINT_ERROR("Unrecognized or unsupported replacement policy: %i",
                      policy);
      break;
  }

  return std::unique_ptr<CompressCacheSet>(nullptr);
}

std::unique_ptr<CacheSetInfo> CompressCacheSet::createCacheSetInfo(
    String name, String cfgname, core_id_t core_id, String replacement_policy,
    UInt32 associativity) {

  CacheBase::ReplacementPolicy policy = parsePolicyType(replacement_policy);

  switch (policy) {
    case CacheBase::LRU:
    // Fall-through
    case CacheBase::LRU_QBS:
      return std::unique_ptr<CacheSetInfo>(
          new CacheSetInfoLRU(name, cfgname, core_id, associativity,
                              getNumQBSAttempts(policy, cfgname, core_id)));
    default:
      LOG_PRINT_ERROR("Unrecognized or unsupported replacement policy: %i",
                      policy);
      break;
  }

  return std::unique_ptr<CacheSetInfo>(nullptr);
}

CompressCacheSet::CompressCacheSet(CacheBase::cache_t cache_type,
                                   UInt32 associativity, UInt32 blocksize)
    : CacheSet(cache_type, associativity, blocksize),
      m_super_block_info_ways(associativity),
      m_data_ways(associativity) {

  for (auto& super_block_info : m_super_block_info_ways) {
    for (UInt32 i = 0; i < MAX_SUPERBLOCK_SIZE; ++i) {
      std::unique_ptr<CacheBlockInfo> block_info =
          CacheBlockInfo::create(cache_type);

      // Replace dummy object from initialization with one specialized to
      // replacement policy
      super_block_info.swapBlockInfo(i, block_info);
    }
  }
}

CompressCacheSet::~CompressCacheSet() {}

void CompressCacheSet::readLine(UInt32 way, UInt32 block_id, UInt32 offset,
                                UInt32 bytes, bool update_replacement,
                                Byte* rd_data) {

  assert(offset + bytes <= m_blocksize);
  assert(rd_data != nullptr);

  const SuperBlockInfo& super_block_info = m_super_block_info_ways[way];
  assert(super_block_info.isValid(block_id));

  const CompressBlockData& super_data = m_data_ways[way];
  super_data.decompress(block_id, offset, bytes, rd_data);

  if (update_replacement) updateReplacementIndex(way);
}

CacheBlockInfo* CompressCacheSet::find(IntPtr tag, UInt32* way = nullptr,
                                       UInt32* block_id = nullptr) {

  for (UInt32 i = 0; i < m_associativity; ++i) {
    const SuperBlockInfo& super_block_info = m_super_block_info_ways;

    UInt32 tmp_block_id;
    if (super_block_info.compareTags(tag, &tmp_block_id)) {
      if (block_id != nullptr) *block_id = tmp_block_id;
      return super_block_info.getBlock(tmp_block_id);
    }
  }

  return nullptr;
}

bool CompressCacheSet::invalidate(IntPtr tag) {
  for (auto& super_block_info : m_super_block_info_ways) {
    if (super_block_info.invalidate(tag)) return true;
  }

  return false;
}

UInt32 CompressCacheSet::writeAndEvict(
    UInt32 way, UInt32 block_id, UInt32 offset, const Byte* wr_data,
    UInt32 bytes, bool update_replacement,
    std::unique_ptr<CacheBlockInfo>& evict_block_info_1, Byte* evict_data_1,
    std::unique_ptr<CacheBlockInfo>& evict_block_info_2, Byte* evict_data_2,
    std::unique_ptr<CacheBlockInfo>& evict_block_info_3, Byte* evict_data_3,
    std::unique_ptr<CacheBlockInfo>& evict_block_info_4, Byte* evict_data_4,
    CacheCntlr* cntlr) {

  assert(offset + bytes <= m_blocksize);
  assert(wr_data != nullptr);

  const SuperBlockInfo& super_block_info = m_super_block_info_ways[way];
  assert(super_block_info.isValid(block_id));

  const CompCacheBlockData& super_data = m_data_ways[way];

  if (super_data.isCompressible(block_id, offset, wr_data, bytes)) {
    super_data.compress(block_id, offset, wr_data, bytes);

    if (update_replacement) updateReplacementIndex(way);
    return 0;
  } else {
    /*
     * Current block cannot be modified in memory without changing the
     * compression factor.  Therefore, we will pretend to evict it from it's
     * current superblock and re-insert it according to the replacement policy.
     * This re-insertion task leverages CompressCacheSet::insertAndEvict and
     * simply passes the eviction references through.
     */
    std::unique_ptr<Byte> evict_block_data(new Byte[m_blocksize]);
    super_data.evictBlockData(block_id, evict_block_data.get());

    std::unique_ptr<CacheBlockInfo> evict_block_info =
        super_block_info.evictBlockInfo(block_id);

    return CompressCacheSet::insertAndEvict(
        evict_block_info, evict_block_data.get(), cntlr, evict_block_info_1,
        evict_data_1, evict_block_info_2, evict_data_2, evict_block_info_3,
        evict_data_3, evict_block_info_4, evict_data_4);
  }

  assert(false);
}

UInt32 CompressCacheSet::insertAndEvict(
    std::unique_ptr<CacheBlockInfo> ins_block_info, const Byte* ins_data,
    std::unique_ptr<CacheBlockInfo>& evict_block_info_1, Byte* evict_data_1,
    std::unique_ptr<CacheBlockInfo>& evict_block_info_2, Byte* evict_data_2,
    std::unique_ptr<CacheBlockInfo>& evict_block_info_3, Byte* evict_data_3,
    std::unique_ptr<CacheBlockInfo>& evict_block_info_4, Byte* evict_data_4,
    CacheCntlr* cntlr) {

  assert(offset + bytes <= m_blocksize);
  assert(ins_data != nullptr);

  /*
   * First insert attempt: scan through super blocks and test to see if
   * ins_block_info can be inserted using canInsertBlockInfo.  If the block info
   * has a superblock match, check to see if the new data can be compressed with
   * the line as well.
   */
  for (UInt32 i = 0; i < m_associativity; ++i) {
    const SuperBlockInfo& merge_block_info = m_super_block_info_ways[i];
    const CompCacheBlockData& merge_data   = m_data_ways[i];
    UInt32 merge_block_id;
    if (merge_block_info.canInsertBlockInfo(ins_block_info.get(),
                                            &merge_block_id) &&
        merge_data.canInsertBlockData(ins_block_info.get(), ins_data)) {
      merge_data.insertBlockData(merge_block_id, ins_data);
      merge_block_info.insertBlockInfo(merge_block_id,
                                       std::move(ins_block_info));

      return 0;
    }
  }

  /*
   *   There is a particularly nasty edge-case here in which all ways contain
   *   a line that is in the SHARED_UPGRADING coherence state.
   */
  const UInt32 repl_way                  = getReplacementIndex(cntlr);
  const SuperBlockInfo& super_block_info = m_super_block_info_ways[repl_way];
  const CompCacheBlockData& super_data   = m_data_ways[repl_way];

  /*
   * Second insert attempt: kick out every cache block in the superblock
   * highlighted for replacement, then we are guaranteed to have an empty block
   * for the inserted data.
   */
  UInt32 n_evicted = 0;
  for (UInt32 i = 0; i < MAX_SUPERBLOCK_SIZE; ++i) {
    if (super_block_info.isValid(i)) {
      ++n_evicted;

      switch (n_evicted) {
        case 1:
          assert(evict_data_1 != nullptr);
          super_data.evictBlockData(i, evict_data_1);
          evict_block_info_1 = super_block_info.evictBlockInfo(block_id);
          break;
        case 2:
          assert(evict_data_1 != nullptr);
          super_data.evictBlockData(i, evict_data_2);
          evict_block_info_2 = super_block_info.evictBlockInfo(block_id);
          break;
        case 3:
          assert(evict_data_3 != nullptr);
          super_data.evictBlockData(i, evict_data_3);
          evict_block_info_3 = super_block_info.evictBlockInfo(block_id);
          break;
        case 4:
          assert(evict_data_4 != nullptr);
          super_data.evictBlockData(i, evict_data_4);
          evict_block_info_4 = super_block_info.evictBlockInfo(block_id);
          break;
        default:
          assert(false);
      }
    }
  }
  assert(!super_block_info.isValid());

  UInt32 repl_block_id;
  (void)super_block_info.canInsertBlockInfo(ins_block_info.get(),
                                            &repl_block_id);

  super_data.insertBlockData(repl_block_id, ins_data);
  super_block_info.insertBlockInfo(repl_block_id, std::move(ins_block_info));

  return n_evicted;
}

bool CompressCacheSet::tryWriteLine(UInt32 way, UInt32 block_id, UInt32 offset,
                                    const Byte* wr_data, UInt32 bytes,
                                    bool update_replacement) {

  assert(offset + bytes <= m_blocksize);
  assert(wr_data != nullptr);

  const SuperBlockInfo& super_block_info = m_super_block_info_ways[way];
  assert(super_block_info.isValid(block_id));

  if (super_data.isCompressible(block_id, offset, wr_data, bytes)) {
    super_data.compress(block_id, offset, wr_data, bytes);

    if (update_replacement) updateReplacementIndex(way);
    return true;
  } else {
    return false;
  }

  assert(false);
}

bool CompressCache::tryInsert(std::unique_ptr<CacheBlockInfo> ins_block_info,
                              const Byte* ins_data, CacheCntlr* cntlr) {

  assert(offset + bytes <= m_blocksize);
  assert(ins_data != nullptr);

  /*
   * First insert attempt: scan through super blocks and test to see if
   * ins_block_info can be inserted using canInsertBlockInfo.  If the block info
   * has a superblock match, check to see if the new data can be compressed with
   * the line as well.
   */
  for (UInt32 i = 0; i < m_associativity; ++i) {
    const SuperBlockInfo& merge_block_info = m_super_block_info_ways[i];
    const CompCacheBlockData& merge_data   = m_data_ways[i];
    UInt32 merge_block_id;
    if (merge_block_info.canInsertBlockInfo(ins_block_info.get(),
                                            &merge_block_id) &&
        merge_data.canInsertBlockData(ins_block_info.get(), ins_data)) {
      merge_data.insertBlockData(merge_block_id, ins_data);
      merge_block_info.insertBlockInfo(merge_block_id,
                                       std::move(ins_block_info));

      return 0;
    }
  }

  /*
   *   There is a particularly nasty edge-case here in which all ways contain
   *   a line that is in the SHARED_UPGRADING coherence state.
   */
  const UInt32 repl_way                  = getReplacementIndex(cntlr);
  const SuperBlockInfo& super_block_info = m_super_block_info_ways[repl_way];
  const CompCacheBlockData& super_data   = m_data_ways[repl_way];

  /*
   * Second insert attempt: if the superblock highlighted for replacement
   * happens to be empty, then fill it with the new data.  Otherwise fail.
   */
  if (!super_block.isValid()) {
    UInt32 repl_block_id;
    (void)super_block_info.canInsertBlockInfo(ins_block_info.get(),
                                              &repl_block_id);

    super_data.insertBlockData(repl_block_id, ins_data);
    super_block_info.insertBlockInfo(repl_block_id, std::move(ins_block_info));

    return true;
  }

  return false;
}

bool CompressCacheSet::isValidReplacement(UInt32 way) {
  // If at least cache block in the superblock for the specified way is
  // upgrading, do not let anything touch the superblock
  const SuperBlockInfo& super_block_info = m_super_block_info_ways[way];

  for (UInt32 i = 0; i < MAX_SUPERBLOCK_SIZE; ++i) {
    if (super_block_info.isValid(i)) {
      const CacheBlockInfo* block_info = super_block_info.getBlockInfo(i);
      if (block_info->getCState() == CacheState::SHARED_UPGRADING) return false;
    }
  }

  return true;
}
