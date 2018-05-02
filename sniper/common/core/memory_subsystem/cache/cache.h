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

class CacheCompressionCntlr {
 private:
  bool m_compressible;
  bool m_change_scheme_otf;
  bool m_prune_dish_entries;
  int num_scheme1;
  int num_scheme2;
 public:
  CacheCompressionCntlr(bool compressible = false,
                        bool change_scheme_on_the_fly = false,
                        bool prune_dish_entries = false) :
      m_compressible(compressible),
      m_change_scheme_otf(change_scheme_on_the_fly),
      m_prune_dish_entries(prune_dish_entries),
      num_scheme1(0), num_scheme2(0) {
  }

  DISH::scheme_t getDefaultScheme() {
    if (num_scheme1 >= num_scheme2) {
      return DISH::scheme_t::SCHEME1;
    } else {
      return DISH::scheme_t::SCHEME2;
    }
  }

  void evict(DISH::scheme_t scheme) {
    switch (scheme) {
      case DISH::scheme_t::SCHEME1:
        num_scheme1--;
        break;
      case DISH::scheme_t::SCHEME2:
        num_scheme2--;
        break;
      default:
        break;
    }
  }

  void insert(DISH::scheme_t scheme) {
    switch (scheme) {
      case DISH::scheme_t::SCHEME1:
        num_scheme1++;
        break;
      case DISH::scheme_t::SCHEME2:
        num_scheme2++;
        break;
      default:
        break;
    }
  }

  bool canCompress() {
    return m_compressible;
  }

  bool canChangeSchemeOTF(){
    return m_compressible && m_change_scheme_otf;
  }

  bool shouldPruneDISHEntries() {
    return m_compressible && m_prune_dish_entries;
  }
};

class Cache : public CacheBase {
 private:
  bool m_enabled;

  // Cache counters
  UInt64 m_num_accesses;
  UInt64 m_num_hits;

  // Generic Cache Info
  cache_t m_cache_type;
  std::vector<std::unique_ptr<CacheSet>> m_sets;
  std::unique_ptr<CacheSetInfo> m_set_info;

  FaultInjector* m_fault_injector;
  std::unique_ptr<CacheCompressionCntlr> m_compress_cntlr;

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
        AddressHomeLookup* ahl        = nullptr,
        bool change_scheme_on_the_fly = false,
        bool prune_dish_entries       = false);
  ~Cache();

  Lock& getSetLock(IntPtr addr);
  bool isCompressible();
  UInt32 getSuperblockSize();

  void invalidateSingleLine(IntPtr addr);
  CacheBlockInfo* accessSingleLine(IntPtr addr, access_t access_type,
                                   Byte* acc_data, UInt32 bytes,
                                   SubsecondTime now, bool update_replacement,
                                   WritebackLines* writebacks = nullptr,
                                   CacheCntlr* cntlr          = nullptr);
  void insertSingleLine(IntPtr addr, const Byte* ins_data, SubsecondTime now,
                        bool is_fill, WritebackLines* writebacks,
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
