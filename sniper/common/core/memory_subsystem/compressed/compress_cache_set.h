#ifndef __COMPRESS_CACHE_SET_H__
#define __COMPRESS_CACHE_SET_H__

#include <cassert>
#include <memory>
#include <vector>

#include "cache_base.h"
#include "cache_block_info.h"
#include "cache_set.h"
#include "compress_block_data.h"
#include "dish_utils.h"
#include "superblock_info.h"

class CompressCacheSet : public CacheSet {
 public:
  /*
   * This function violates the is-a principle of inheritance, since
   * CompressCacheSet uses RAII for managing the lifetimes of it's objects.
   *
   * To enforce this at compile time, delete it.
   */
  static CacheSet* createCacheSet(String cfgname, core_id_t core_id,
                                  String replacement_policy,
                                  CacheBase::cache_t cache_type,
                                  UInt32 associativity, UInt32 blocksize,
                                  CacheSetInfo* set_info = NULL) = delete;

  /*
   * These functions violate the is-a principle of inheritance, since
   * CompressCacheSet has a different interface for writing to memory.  This is
   * because writes, in addition to inserts, can cause up to four evictions if
   * it is compressed and the new data makes it incompressible.
   *
   * To enforce this at compile time, delete them.
   */
  void writeLine(UInt32 line_index, UInt32 offset, Byte* in_buff, UInt32 bytes,
                 bool update_replacement) = delete;
  void insert(CacheBlockInfo* cache_block_info, Byte* fill_buff, bool* eviction,
              CacheBlockInfo* evict_block_info, Byte* evict_buff,
              CacheCntlr* cntlr = NULL) = delete;
  char* getDataPtr(UInt32 way, UInt32 offset = 0) = delete;

  // Factory method used to create the CompressCacheSet subclasses using
  // RAII
  static std::unique_ptr<CompressCacheSet> createCompressCacheSet(
      String cfgname, core_id_t core_id, String replacement_policy,
      CacheBase::cache_t cache_type, UInt32 associativity, UInt32 blocksize,
      CacheSetInfo* set_info = nullptr);

  // Factory method used to create the CacheSetInfo using RAII.  This function
  // hides the parent implementation on purpose.
  static std::unique_ptr<CacheSetInfo> createCacheSetInfo(
      String name, String cfgname, core_id_t core_id, String replacement_policy,
      UInt32 associativity);

 protected:
  std::vector<SuperBlockInfo> m_super_block_info_ways;
  std::vector<CompressBlockData> m_data_ways;

 public:
  CompressCacheSet(CacheBase::cache_t cache_type, UInt32 associativity,
                   UInt32 blocksize);
  virtual ~CompressCacheSet();

  void readLine(UInt32 way, UInt32 block_id, UInt32 offset, UInt32 bytes,
                bool update_replacement, Byte* rd_data);
  CacheBlockInfo* find(IntPtr tag, UInt32* way = nullptr,
                       UInt32* block_id = nullptr);
  bool invalidate(IntPtr tag);
  UInt32 write_and_evict(
      UInt32 way, UInt32 block_id, UInt32 offset, const Byte* wr_data,
      UInt32 bytes, bool update_replacement,
      std::unique_ptr<CacheBlockInfo>& evict_block_info_1, Byte* evict_data_1,
      std::unique_ptr<CacheBlockInfo>& evict_block_info_2, Byte* evict_data_2,
      std::unique_ptr<CacheBlockInfo>& evict_block_info_3, Byte* evict_data_3,
      std::unique_ptr<CacheBlockInfo>& evict_block_info_4, Byte* evict_data_4,
      CacheCntlr* cntlr = nullptr);
  UInt32 insert_and_evict(
      std::unique_ptr<CacheBlockInfo> ins_block_info, const Byte* ins_data,
      std::unique_ptr<CacheBlockInfo>& evict_block_info_1, Byte* evict_data_1,
      std::unique_ptr<CacheBlockInfo>& evict_block_info_2, Byte* evict_data_2,
      std::unique_ptr<CacheBlockInfo>& evict_block_info_3, Byte* evict_data_3,
      std::unique_ptr<CacheBlockInfo>& evict_block_info_4, Byte* evict_data_4,
      CacheCntlr* cntlr = nullptr);

  /*
   * These functions provides a way to attempt modifying or shuffling a cache
   * lines, in case the new data happens to be is compressible.  It fails if the
   * modification would lead to an eviction.
   */
  bool tryWriteLine(UInt32 way, UInt32 block_id, UInt32 offset,
                    const Byte* wr_data, UInt32 bytes, bool update_replacement);
  bool tryInsert(std::unique_ptr<CacheBlockInfo> ins_block_info,
                 const Byte* ins_data, CacheCntlr* cntlr = nullptr);

  SuperBlockInfo* peekSuperBlock(UInt32 way) const {
    assert(way < m_associativity);

    return &(m_super_block_info_ways[way]);
  }
  CacheBlockInfo* peekBlock(UInt32 way, UInt32 block_id) const {
    assert(way < m_associativity);

    return m_super_block_info_ways[way].getBlockInfo(block_id);
  }

  // Pure virtual functions must be overridden by subclasses implementing
  // replacement policies.
  virtual UInt32 getReplacementIndex(CacheCntlr* cntlr)    = 0;
  virtual void updateReplacementIndex(UInt32 accessed_way) = 0;

  bool isValidReplacement(UInt32 way);
};

#endif /* __COMPRESS_CACHE_SET_H__ */
