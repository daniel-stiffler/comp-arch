#ifndef __CACHE_H__
#define __CACHE_H__

#include <memory>
#include <vector>

#include "cache_base.h"
#include "cache_block_info.h"
#include "cache_perf_model.h"
#include "cache_set.h"
#include "compress_utils.h"
#include "core.h"
#include "fault_injection.h"
#include "hash_map_set.h"
#include "log.h"
#include "shmem_perf_model.h"
#include "utils.h"

//#define ENABLE_SET_USAGE_HIST

class Cache : public CacheBase {
 private:
  bool m_enabled;
  bool m_compressible;

  // Cache counters
  UInt64 m_num_accesses;
  UInt64 m_num_hits;

  // Generic Cache Info
  cache_t m_cache_type;
  std::vector<std::unique_ptr<CacheSet>> m_sets;
  std::unique_ptr<CacheSetInfo> m_set_info;

  FaultInjector* m_fault_injector;

#ifdef ENABLE_SET_USAGE_HIST
  std::vector<UInt64> m_set_usage_hist;
#endif

 public:
  // The Cache object can be configured to use DISH dictionary compression, or
  // manage lines normally.
  Cache(String name, String cfgname, core_id_t core_id, UInt32 num_sets,
        UInt32 associativity, UInt32 blocksize, bool compressible,
        String replacement_policy, cache_t cache_type,
        hash_t hash                   = CacheBase::HASH_MASK,
        FaultInjector* fault_injector = nullptr,
        AddressHomeLookup* ahl        = nullptr);
  ~Cache();

  Lock& getSetLock(IntPtr addr);
  bool isCompressible();
  UInt32 getSuperblockSize();

  bool invalidateSingleLine(IntPtr addr);
  CacheBlockInfo* accessSingleLine(IntPtr addr, access_t access_type,
                                   Byte* acc_data, UInt32 bytes,
                                   SubsecondTime now, bool update_replacement,
                                   WritebackLines* writebacks = nullptr,
                                   CacheCntlr* cntlr          = nullptr);
  void insertSingleLine(IntPtr addr, const Byte* ins_data, SubsecondTime now,
                        WritebackLines* writebacks,
                        CacheCntlr* cntlr = nullptr);
  CacheBlockInfo* peekSingleLine(IntPtr addr);

  CacheBlockInfo* peekBlock(UInt32 set_index, UInt32 way,
                            UInt32 block_id) const;

  // Address parsing utilities
  void splitAddress(IntPtr addr, IntPtr* tag = nullptr,
                    IntPtr* supertag = nullptr, UInt32* set_index = nullptr,
                    UInt32* block_id = nullptr, UInt32* offset = nullptr) const;
  IntPtr tagToAddress(IntPtr tag) const;

  // Update Cache Counters
  void updateCounters(bool cache_hit);
  void updateHits(Core::mem_op_t mem_op_type, UInt64 hits);

  void enable();
  void disable();
};

#endif /* __CACHE_H__ */
