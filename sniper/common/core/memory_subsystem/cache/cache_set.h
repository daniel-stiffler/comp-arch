#pragma once

#include <cstring>
#include <memory>
#include <vector>

#include "block_data.h"
#include "cache_base.h"
#include "cache_block_info.h"
#include "compress_utils.h"
#include "fixed_types.h"
#include "lock.h"
#include "superblock_info.h"

class Cache;  // Forward declaration

// Per-cache object to store replacement-policy related info (e.g. statistics),
// collect data from all CacheSet* objects (per set) and implement the actual
// replacement policy
class CacheSetInfo {
  // Nothing here for now
 public:
  virtual ~CacheSetInfo() {}
};

class CacheSet {
 public:
  // Factory method used to create the CacheSet specialized subclasses
  static std::unique_ptr<CacheSet> createCacheSet(
      String cfgname, core_id_t core_id, String replacement_policy,
      CacheBase::cache_t cache_type, UInt32 associativity, UInt32 blocksize,
      CacheCompressionCntlr* compress_cntlr, const Cache* parent_cache,
      CacheSetInfo* set_info = nullptr);

  // Factory method used to create the CacheSetInfo specialized subclasses
  static std::unique_ptr<CacheSetInfo> createCacheSetInfo(
      String name, String cfgname, core_id_t core_id, String replacement_policy,
      UInt32 associativity, CacheCompressionCntlr* compress_cntlr);

  static CacheBase::ReplacementPolicy parsePolicyType(String policy);
  static UInt8 getNumQBSAttempts(CacheBase::ReplacementPolicy, String cfgname,
                                 core_id_t core_id);

 protected:
  UInt32 m_associativity;
  UInt32 m_blocksize;
  CacheCompressionCntlr* m_compress_cntlr;
  // bool m_compressible;
  // bool m_change_scheme_otf;
  // bool m_prune_dish_entries;
  Lock m_lock;
  std::vector<SuperblockInfo> m_superblock_info_ways;
  std::vector<BlockData> m_data_ways;

  const Cache* m_parent_cache;  // Used for address translation

 public:
  CacheSet(CacheBase::cache_t cache_type, UInt32 associativity,
           UInt32 blocksize, CacheCompressionCntlr* compress_cntlr,
           const Cache* parent_cache);
  virtual ~CacheSet();

  UInt32 getAssociativity() { return m_associativity; }
  UInt32 getBlockSize() { return m_blocksize; }
  Lock& getLock() { return m_lock; }

  void readLine(UInt32 way, UInt32 block_id, UInt32 offset, UInt32 bytes,
                bool update_replacement, Byte* rd_data);
  void writeLine(UInt32 way, UInt32 block_id, UInt32 offset,
                 const Byte* wr_buff, UInt32 bytes, bool update_replacement,
                 WritebackLines* writebacks, CacheCntlr* cntlr = nullptr);
  CacheBlockInfo* find(IntPtr tag, UInt32 block_id,
                       UInt32* way = nullptr) const;
  void invalidate(IntPtr tag, UInt32 block_info);
  void insertLine(CacheBlockInfoUPtr ins_block_info, const Byte* ins_data,
                  WritebackLines* writebacks, CacheCntlr* cntlr = nullptr);

  CacheBlockInfo* peekBlock(UInt32 way, UInt32 block_id) const {
    assert(way < m_associativity);

    return m_superblock_info_ways[way].peekBlock(block_id);
  }

  // Pure virtual functions for the replacement policies.  These will be
  // overridden by specialized subclasses of CacheSet to manage LRU.
  virtual UInt32 getReplacementWay(CacheCntlr* cntlr)    = 0;
  virtual void updateReplacementWay(UInt32 accessed_way) = 0;

  bool isValidReplacement(UInt32 way);
};
