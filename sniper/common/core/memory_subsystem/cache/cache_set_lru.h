#pragma once

#include "cache_set.h"

#include <cassert>
#include <list>
#include <unordered_map>

#include "log.h"

class CacheSetInfoLRU : public CacheSetInfo {
 public:
  CacheSetInfoLRU(String name, String cfgname, core_id_t core_id,
                  UInt32 associativity);
  virtual ~CacheSetInfoLRU();

  void increment(UInt32 way) {
    LOG_ASSERT_ERROR(way < m_associativity, "Way(%d) >= Associativity(%d)", way,
                     m_associativity);

    ++m_access[way];
  }

 private:
  const UInt32 m_associativity;
  std::vector<UInt64> m_access;
};

class CacheSetLRU : public CacheSet {
 public:
  CacheSetLRU(UInt32 set_index, CacheBase::cache_t cache_type, UInt32 associativity,
              UInt32 blocksize, CacheCompressionCntlr* compress_cntlr, const Cache* parent_cache,
              CacheSetInfoLRU* set_info);
  virtual ~CacheSetLRU();

  virtual UInt32 getReplacementWay(bool allow_fwd_inv, CacheCntlr* cntlr);
  void updateReplacementWay(UInt32 accessed_way);
  std::string dump_priorities() const;

 protected:
  // LRU cache is represented as a doubly-linked list and hash table which maps
  // elements to their references in the list
  std::unordered_map<UInt32, std::list<UInt32>::iterator> m_lru_places;
  std::list<UInt32> m_lru_priorities;

  CacheSetInfoLRU* m_set_info;

  void moveToMRU(UInt32 accessed_way);
};
