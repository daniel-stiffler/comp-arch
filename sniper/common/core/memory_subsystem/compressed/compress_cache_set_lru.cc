#include "compress_cache_set_lru.h"

#include "fixed_types.h"
#include "log.h"
#include "stats.h"

CompressCacheSetLRU::CompressCacheSetLRU(CacheBase::cache_t cache_type,
                                         UInt32 associativity, UInt32 blocksize,
                                         CacheSetInfoLRU* set_info,
                                         UInt8 num_attempts)
    : CompressCacheSet(cache_type, associativity, blocksize),
      m_num_attempts{num_attempts},
      m_set_info{set_info} {

  m_lru_priorities.resize(m_associativity);
  for (UInt32 i = 0; i < m_associativity; ++i) m_lru_priorities[i] = i;
}

CompressCacheSetLRU::~CompressCacheSetLRU() {}

UInt32 CompressCacheSetLRU::getReplacementIndex(CacheCntlr* cntlr) {
  // First try to find an unallocated superblock
  for (UInt32 i = 0; i < m_associativity; ++i) {
    if (!m_super_block_info_ways[i].isValid()) {
      // Found an empty superblock, so mark it as most-recently used and return
      // the way index
      moveToMRU(i);
      return i;
    }
  }

  // Make m_num_attemps attempts at evicting the block at LRU position
  for (UInt8 attempt = 0; attempt < m_num_attempts; ++attempt) {
    UInt32 repl_way  = 0;
    UInt8 oldest_lru = 0;
    for (UInt32 i = 0; i < m_associativity; ++i) {
      if (m_lru_priorities[i] > oldest_lru && isValidReplacement(i)) {
        repl_way   = i;
        oldest_lru = m_lru_priorities[i];
      }
    }
    LOG_ASSERT_ERROR(index < m_associativity, "Error Finding LRU bits");

    bool qbs_reject = false;
    if (attempt <
        m_num_attempts - 1) {  // Do not attempt on the last iteration?
      LOG_ASSERT_ERROR(cntlr != nullptr,
                       "CacheCntlr == nullptr, QBS can only be used when cntlr "
                       "is passed in");

      /*
       * Perform query-based-selection on all cache blocks in the superblock.
       * This could potentially lead to hangs, since the SNIPER cache hierarchy
       * is inclusive and we cannot evict anything that is contained in
       * a lower-level cache.
       */
      const SuperBlockInfo& super_block = m_super_block_info_ways[repl_way];
      for (UInt32 i = 0; i < MAX_SUPERBLOCK_SIZE; ++i) {
        if (super_block.isValid(i)) {
          // Should not be const pointer, because this breaks compatibility in
          // the cache controller class
          CacheBlockInfo* block_info = super_block.getBlockInfo(i);

          qbs_reject |= cntlr->isInLowerLevelCache(block_info);
        }
      }
    }

    if (qbs_reject) {
      // Block is contained in lower-level cache, and we have more tries
      // remaining.  Move this block to MRU and try again
      moveToMRU(repl_way);
      cntlr->incrementQBSLookupCost();
    } else {
      // Mark our newly-inserted line as most-recently used
      moveToMRU(repl_way);
      m_set_info->incrementAttempt(attempt);
      return repl_way;
    }
  }

  // This is very very bad.  We could not find a suitable block to kick out of
  // the cache, so the memory is deadlocked.
  assert(false);
}

void CompressCacheSetLRU::updateReplacementIndex(UInt32 accessed_way) {
  m_set_info->increment(m_lru_priorities[accessed_way]);
  moveToMRU(accessed_way);
}

void CompressCacheSetLRU::moveToMRU(UInt32 accessed_way) {
  for (UInt32 i = 0; i < m_associativity; i++) {
    if (m_lru_priorities[i] < m_lru_priorities[accessed_way]) {
      m_lru_priorities[i]++;
    }
  }

  m_lru_priorities[accessed_way] = 0;
}
