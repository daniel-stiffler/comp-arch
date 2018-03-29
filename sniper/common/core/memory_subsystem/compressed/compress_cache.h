#ifndef __COMPRESS_CACHE_H__
#define __COMPRESS_CACHE_H__

#include <memory>
#include <vector>

#include "cache.h"
#include "fixed_types.h"
#include "log.h"
#include "simulator.h"

class CompressCache : public Cache {
  /*
   * These functions violate the is-a principle of inheritance, since
   * CompressCacheSet has a different interface for writing to memory.  This is
   * because writes, in addition to inserts, can cause up to four evictions if
   * it is compressed and the new data makes it incompressible.
   *
   * To enforce this at compile time, delete them.
   */
  CacheBlockInfo* accessSingleLine(IntPtr addr, access_t access_type,
                                   Byte* buff, UInt32 bytes, SubsecondTime now,
                                   bool update_replacement) = delete;
  void insertSingleLine(IntPtr addr, Byte* fill_buff, bool* eviction,
                        IntPtr* evict_addr, CacheBlockInfo* evict_block_info,
                        Byte* evict_buff, SubsecondTime now,
                        CacheCntlr* cntlr = NULL) = delete;

 private:
  bool m_enabled;

  // Cache counters
  UInt64 m_num_accesses;
  UInt64 m_num_hits;

  // Generic Cache Info
  cache_t m_cache_type;
  std::vector<std::unique_ptr<CompressCacheSet>> m_sets;
  std::unique_ptr<CacheSetInfo> m_set_info;

  FaultInjector* m_fault_injector;

 public:
  CompressCache(String name, String cfgname, core_id_t core_id, UInt32 num_sets,
                UInt32 associativity, UInt32 cache_block_size,
                String replacement_policy, cache_t cache_type,
                hash_t hash            = CacheBase::HASH_MASK,
                AddressHomeLookup* ahl = nullptr);
  ~CompressCache();

  Lock& getSetLock(IntPtr addr);

  bool readSingleLine(IntPtr addr, UInt32 bytes, SubsecondTime now,
                      bool update_replacement, CacheBlockInfo* rd_block_info,
                      Byte* rd_data);
  UInt32 writeAndEvictSingleLine(
      IntPtr addr, const Byte* wr_data, UInt32 bytes, SubsecondTime now,
      bool update_replacement,
      std::unique_ptr<CacheBlockInfo>& evict_block_info_1, Byte* evict_data_1,
      std::unique_ptr<CacheBlockInfo>& evict_block_info_2, Byte* evict_data_2,
      std::unique_ptr<CacheBlockInfo>& evict_block_info_3, Byte* evict_data_3,
      std::unique_ptr<CacheBlockInfo>& evict_block_info_4, Byte* evict_data_4,
      CacheCntlr* cntlr = nullptr);
  UInt32 insertAndEvictSingleLine(
      IntPtr addr, const Byte* ins_data, SubsecondTime now,
      std::unique_ptr<CacheBlockInfo>& evict_block_info_1, Byte* evict_data_1,
      std::unique_ptr<CacheBlockInfo>& evict_block_info_2, Byte* evict_data_2,
      std::unique_ptr<CacheBlockInfo>& evict_block_info_3, Byte* evict_data_3,
      std::unique_ptr<CacheBlockInfo>& evict_block_info_4, Byte* evict_data_4,
      CacheCntlr* cntlr = nullptr);
  bool invalidateSingleLine(IntPtr addr);

  bool tryWriteSingleLine(IntPtr addr, const Byte* wr_data, UInt32 bytes,
                          SubsecondTime now, bool update_replacement,
                          CacheCntlr* cntlr = nullptr);
  bool tryInsertSingleLine(IntPtr addr, const Byte* ins_data, SubsecondTime now,
                           CacheCntlr* cntlr = nullptr);

  CacheBlockInfo* peekSingleLine(IntPtr addr);

  SuperBlockInfo* peekSuperBlock(UInt32 set_index, UInt32 way) const {
    return m_sets[set_index]->peekSuperBlock(way);
  }

  CacheBlockInfo* peekBlock(UInt32 set_index, UInt32 way,
                            UInt32 block_id) const {
    return m_sets[set_index]->peekBlock(way, block_id);
  }

  // For compatibility purposes, the address splitting functions must operate on
  // references instead of mutable pointers
  void splitAddress(IntPtr addr, IntPtr& tag, UInt32& set_index) const;
  void splitAddress(IntPtr addr, IntPtr& tag, UInt32& set_index,
                    UInt32& offset) const;
  void splitAddress(IntPtr address, IntPtr& super_tag, UInt32 set_index,
                    UInt32 block_id, UInt32& offset) const;
  IntPtr tagToAddress(const IntPtr tag) const;

  // Update Cache Counters
  void updateCounters(bool cache_hit);
  void updateHits(Core::mem_op_t mem_op_type, UInt64 hits);

  void enable() { m_enabled = true; }
  void disable() { m_enabled = false; }
};

#endif /* __COMPRESS_CACHE_H__ */
