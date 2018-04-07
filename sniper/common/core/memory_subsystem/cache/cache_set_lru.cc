#include "cache_set_lru.h"

#include "log.h"
#include "stats.h"

// Implements LRU replacement, optionally augmented with Query-Based Selection
// [Jaleel et al., MICRO'10]

CacheSetLRU::CacheSetLRU(CacheBase::cache_t cache_type, UInt32 associativity,
                         UInt32 blocksize, bool compressible,
                         CacheSetInfoLRU* set_info, UInt8 num_attempts)
    : CacheSet(cache_type, associativity, blocksize, compressible),
      m_num_attempts(num_attempts),
      m_lru_priorities(associativity),
      m_set_info(set_info) {

  for (auto& e : m_lru_priorities) e = 0;
}

CacheSetLRU::~CacheSetLRU() {
  // RAII takes care of destructing everything for us
}

UInt32 CacheSetLRU::getReplacementWay(CacheCntlr* cntlr) {
  // First try to find an unallocated superblock
  for (UInt32 i = 0; i < m_associativity; ++i) {
    if (!m_superblock_info_ways[i].isValid()) {
      // Found an empty superblock, so mark it as the most-recently used and
      // return the way intex
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
    LOG_ASSERT_ERROR(repl_way < m_associativity, "Error Finding LRU bits");

    bool qbs_reject = false;
    if (attempt <
        m_num_attempts - 1) {  // Do not use this on the last iteration
      LOG_ASSERT_ERROR(cntlr != nullptr,
                       "CacheCntlr == nullptr, QBS can only be used when cntlr "
                       "is passed in");

      /*
       * Perform query-based-selection on all cache blocks in the superblock.
       * This could potentially lead to hangs, since the SNIPER cache hierarchy
       * is mostly inclusive and we cannot bypass caches to evict something in
       * a lower level.
       */

      const SuperblockInfo& superblock = m_superblock_info_ways[repl_way];
      for (UInt32 i = 0; i < SUPERBLOCK_SIZE; ++i) {
        CacheBlockInfo* block_info = superblock.peekBlock(i);

        qbs_reject |= cntlr->isInLowerLevelCache(block_info);
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

  LOG_PRINT_ERROR(
      "!! Deadlock condition for cache replacement reached !!"
      "Could not find a suitable line to evict, so we have to panic.");
  assert(false);
}

void CacheSetLRU::updateReplacementWay(UInt32 accessed_way) {
  m_set_info->increment(m_lru_priorities[accessed_way]);
  moveToMRU(accessed_way);
}

void CacheSetLRU::moveToMRU(UInt32 accessed_way) {
  for (UInt32 i = 0; i < m_associativity; i++) {
    if (m_lru_priorities[i] < m_lru_priorities[accessed_way])
      m_lru_priorities[i]++;
  }

  m_lru_priorities[accessed_way] = 0;
}

CacheSetInfoLRU::CacheSetInfoLRU(String name, String cfgname, core_id_t core_id,
                                 UInt32 associativity, bool compressible,
                                 UInt8 num_attempts)
    : m_associativity(associativity),
      m_compressible(compressible),
      m_access(associativity),
      m_attempts(num_attempts) {

  for (UInt32 i = 0; i < m_associativity; ++i) {
    m_access[i] = 0;
    registerStatsMetric(name, core_id, String("access-mru-") + itostr(i),
                        &m_access[i]);
  }

  if (num_attempts > 1) {
    for (UInt32 i = 0; i < num_attempts; ++i) {
      m_attempts[i] = 0;
      registerStatsMetric(name, core_id, String("qbs-attempt-") + itostr(i),
                          &m_attempts[i]);
    }
  }
};

CacheSetInfoLRU::~CacheSetInfoLRU() {
  // RAII takes care of destructing everything for us
}
