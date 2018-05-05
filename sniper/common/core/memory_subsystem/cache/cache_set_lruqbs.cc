#include "cache_set_lruqbs.h"

#include <sstream>

#include "log.h"
#include "stats.h"

// Implements LRU replacement augmented with Query-Based Selection
// [Jaleel et al., MICRO'10]
CacheSetLRUQBS::CacheSetLRUQBS(UInt32 set_index, CacheBase::cache_t cache_type,
                               UInt32 associativity, UInt32 blocksize,
                               CacheCompressionCntlr* compress_cntlr,
                               const Cache* parent_cache, 
                               CacheSetInfoLRUQBS* set_info,
                               UInt8 num_attempts)
    : CacheSet(set_index, cache_type, associativity, blocksize, compress_cntlr,
               parent_cache),

      m_num_attempts(num_attempts),
      m_lru_places(associativity),  // Maximum number of buckets
      m_set_info(set_info) {

  for (UInt32 i = 0; i < m_associativity; ++i) {
    m_lru_priorities.push_back(i);
    m_lru_places.insert({i, std::prev(m_lru_priorities.end())});
  }
}

CacheSetLRUQBS::~CacheSetLRUQBS() {
  // RAII takes care of destructing everything for us
}

UInt32 CacheSetLRUQBS::getReplacementWay(bool allow_fwd_inv, 
                                         CacheCntlr* cntlr) {

  // First try to find an unallocated superblock
  for (UInt32 i = 0; i < m_associativity; ++i) {
    if (!m_superblock_info_ways[i].isValid()) {
      // Found an empty superblock, so mark it as the most-recently used and
      // return the way index
      moveToMRU(i);
      return i;
    }
  }

  // Make m_num_attemps attempts at evicting the block at LRU position
  for (UInt8 attempt = 0; attempt < m_num_attempts; ++attempt) {
    UInt32 repl_way = m_associativity;
    for (const auto e : m_lru_priorities) {
      if (isValidReplacement(e)) {
        repl_way = e;
        break;
      }
    } 
    if (repl_way == m_associativity) {
      LOG_PRINT_WARNING("None of the blocks were marked as valid replacements");
      return m_associativity;
    }

    /*
     * Perform query-based-selection on all cache blocks in the superblock.
     * This could potentially lead to hangs, since the SNIPER cache hierarchy
     * is mostly inclusive and we cannot bypass caches to evict something in
     * a lower level.
     */

    bool qbs_reject = false;
    const SuperblockInfo& superblock = m_superblock_info_ways[repl_way];
    for (UInt32 i = 0; i < SUPERBLOCK_SIZE; ++i) {
      CacheBlockInfo* block_info = superblock.peekBlock(i);

      qbs_reject |= cntlr->isInLowerLevelCache(block_info);
    }


    if (attempt < m_num_attempts - 1) {  // Not the last attempt
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
    } else {  // Last attempt: may have to forward invalidate
      if (!qbs_reject || (qbs_reject && allow_fwd_inv)) {
        // Mark our newly-inserted line as most-recently used
        moveToMRU(repl_way);
        m_set_info->incrementAttempt(attempt);

        return repl_way;
      }
    }
  }

  LOG_PRINT_WARNING("Could not find a suitable block for eviction using "
                    "QBS.");

  return m_associativity;
}

void CacheSetLRUQBS::updateReplacementWay(UInt32 accessed_way) {
  // Subtract the iterators to get the priority
  UInt32 prev_priority =
      std::distance(m_lru_priorities.begin(), m_lru_places[accessed_way]);

  m_set_info->increment(prev_priority);
  moveToMRU(accessed_way);
}

std::string CacheSetLRUQBS::dump_priorities() const {
  std::stringstream info_ss;

  info_ss << "LRUQBS( ";
  
  for (const auto e : m_lru_priorities) {
    info_ss << e << " ";
  } 
  
  info_ss << " )";

  return info_ss.str();
}

void CacheSetLRUQBS::moveToMRU(UInt32 accessed_way) {
  assert(accessed_way < m_associativity);

  std::list<UInt32>::iterator prev_it = m_lru_places[accessed_way];
  m_lru_priorities.erase(m_lru_places[accessed_way]);
  m_lru_priorities.push_back(accessed_way);
  m_lru_places[accessed_way] = std::prev(m_lru_priorities.end());
}

CacheSetInfoLRUQBS::CacheSetInfoLRUQBS(String name, String cfgname, 
                                       core_id_t core_id, UInt32 associativity,
                                       UInt8 num_attempts)
    : m_associativity(associativity),
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

CacheSetInfoLRUQBS::~CacheSetInfoLRUQBS() {
  // RAII takes care of destructing everything for us
}

