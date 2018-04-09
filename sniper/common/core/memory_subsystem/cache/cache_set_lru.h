#ifndef __CACHE_SET_LRU_H__
#define __CACHE_SET_LRU_H__

#include <cassert>
#include <list>
#include <unordered_map>

#include "cache_set.h"

class CacheSetInfoLRU : public CacheSetInfo {
 public:
  CacheSetInfoLRU(String name, String cfgname, core_id_t core_id,
                  UInt32 associativity, bool compressible, UInt8 num_attempts);
  virtual ~CacheSetInfoLRU();

  void increment(UInt32 way) {
    LOG_ASSERT_ERROR(way < m_associativity, "Way(%d) >= Associativity(%d)", way,
                     m_associativity);

    ++m_access[way];
  }
  void incrementAttempt(UInt8 attempt) {
    assert(attempt <= m_attempts.size());

    if (!m_attempts.empty())
      ++m_attempts[attempt];
    else
      LOG_ASSERT_ERROR(attempt == 0,
                       "No place to store attempt# histogram but attempt != 0");
  }

 private:
  const UInt32 m_associativity;
  const bool m_compressible;
  std::vector<UInt64> m_access;
  std::vector<UInt64> m_attempts;
};

class CacheSetLRU : public CacheSet {
 public:
  CacheSetLRU(CacheBase::cache_t cache_type, UInt32 associativity,
              UInt32 blocksize, bool compressible, CacheSetInfoLRU* set_info,
              UInt8 num_attempts);
  virtual ~CacheSetLRU();

  virtual UInt32 getReplacementWay(CacheCntlr* cntlr);
  void updateReplacementWay(UInt32 accessed_way);

 protected:
  const UInt8 m_num_attempts;

  // LRU cache is represented as a doubly-linked list and hash table which maps
  // elements to their references in the list
  std::unordered_map<UInt32, std::list<UInt32>::iterator> m_lru_places;
  std::list<UInt32> m_lru_priorities;

  CacheSetInfoLRU* m_set_info;

  void moveToMRU(UInt32 accessed_way);
};

#endif /* __CACHE_SET_LRU_H__ */
