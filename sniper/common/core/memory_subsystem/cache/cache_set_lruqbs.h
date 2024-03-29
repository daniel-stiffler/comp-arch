#pragma once

#include "cache_set.h"

#include <cassert>
#include <list>
#include <unordered_map>

#include "log.h"

class CacheSetInfoLRUQBS : public CacheSetInfo {
 public:
  CacheSetInfoLRUQBS(String name, String cfgname, core_id_t core_id,
                     UInt32 associativity, UInt8 num_attempts);
  virtual ~CacheSetInfoLRUQBS();

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
  std::vector<UInt64> m_access;
  std::vector<UInt64> m_attempts;
};

class CacheSetLRUQBS : public CacheSet {
 public:
  CacheSetLRUQBS(UInt32 set_index, CacheBase::cache_t cache_type, 
                 UInt32 associativity, UInt32 blocksize, 
                 CacheCompressionCntlr* compress_cntlr, 
                 const Cache* parent_cache, CacheSetInfoLRUQBS* set_info,
                 UInt8 num_attempts);
  virtual ~CacheSetLRUQBS();

  virtual UInt32 getReplacementWay(bool allow_fwd_inv, CacheCntlr* cntlr);
  void updateReplacementWay(UInt32 accessed_way);
  std::string dump_priorities() const;

 protected:
  const UInt8 m_num_attempts;

  // LRU cache is represented as a doubly-linked list and hash table which maps
  // elements to their references in the list
  std::unordered_map<UInt32, std::list<UInt32>::iterator> m_lru_places;
  std::list<UInt32> m_lru_priorities;

  CacheSetInfoLRUQBS* m_set_info;

  void moveToMRU(UInt32 accessed_way);
};
