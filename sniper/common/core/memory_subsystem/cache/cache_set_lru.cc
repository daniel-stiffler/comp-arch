#include "cache_set_lru.h"

#include <sstream>

#include "log.h"
#include "stats.h"

// Implements LRU replacement, optionally augmented with Query-Based Selection
// [Jaleel et al., MICRO'10]
CacheSetLRU::CacheSetLRU(UInt32 set_index, CacheBase::cache_t cache_type, UInt32 associativity,
                         UInt32 blocksize,
                         CacheCompressionCntlr* compress_cntlr,
                         const Cache* parent_cache, CacheSetInfoLRU* set_info)
    : CacheSet(set_index, cache_type, associativity, blocksize, compress_cntlr,
               parent_cache),

      m_lru_places(associativity),  // Maximum number of buckets
      m_set_info(set_info) {

  for (UInt32 i = 0; i < m_associativity; ++i) {
    m_lru_priorities.push_back(i);
    m_lru_places.insert({i, std::prev(m_lru_priorities.end())});
  }
}

CacheSetLRU::~CacheSetLRU() {
  // RAII takes care of destructing everything for us
}

UInt32 CacheSetLRU::getReplacementWay(bool allow_fwd_inv, CacheCntlr* cntlr) {
  // First try to find an unallocated superblock
  for (UInt32 i = 0; i < m_associativity; ++i) {
    if (!m_superblock_info_ways[i].isValid()) {
      // Found an empty superblock, so mark it as the most-recently used and
      // return the way index
      moveToMRU(i);
      return i;
    }
  }

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
  } else {
    // Mark our newly-inserted line as most-recently used
    moveToMRU(repl_way);

    return repl_way;
  }
}

void CacheSetLRU::updateReplacementWay(UInt32 accessed_way) {
  // Subtract the iterators to get the priority
  UInt32 prev_priority =
      std::distance(m_lru_priorities.begin(), m_lru_places[accessed_way]);

  m_set_info->increment(prev_priority);
  moveToMRU(accessed_way);
}

std::string CacheSetLRU::dump_priorities() const {
  std::stringstream info_ss;

  info_ss << "LRU( ";
  
  for (const auto e : m_lru_priorities) {
    info_ss << e << " ";
  } 
  
  info_ss << " )";

  return info_ss.str();
}

void CacheSetLRU::moveToMRU(UInt32 accessed_way) {
  assert(accessed_way < m_associativity);

  std::list<UInt32>::iterator prev_it = m_lru_places[accessed_way];
  m_lru_priorities.erase(m_lru_places[accessed_way]);
  m_lru_priorities.push_back(accessed_way);
  m_lru_places[accessed_way] = std::prev(m_lru_priorities.end());
}

CacheSetInfoLRU::CacheSetInfoLRU(String name, String cfgname, core_id_t core_id,
                                 UInt32 associativity)
    : m_associativity(associativity),
      m_access(associativity) {

  for (UInt32 i = 0; i < m_associativity; ++i) {
    m_access[i] = 0;
    registerStatsMetric(name, core_id, String("access-mru-") + itostr(i),
                        &m_access[i]);
  }
};

CacheSetInfoLRU::~CacheSetInfoLRU() {
  // RAII takes care of destructing everything for us
}

