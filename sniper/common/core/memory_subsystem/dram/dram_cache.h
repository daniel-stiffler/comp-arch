#ifndef __DRAM_CACHE_H__
#define __DRAM_CACHE_H__

#include <utility>

#include "cache.h"
#include "contention_model.h"
#include "dram_cntlr_interface.h"
#include "subsecond_time.h"

class QueueModel;
class Prefetcher;

class DramCache : public DramCntlrInterface {
 public:
  DramCache(MemoryManagerBase* memory_manager, ShmemPerfModel* shmem_perf_model,
            AddressHomeLookup* home_lookup, UInt32 cache_block_size,
            DramCntlrInterface* dram_cntlr);
  ~DramCache();

  virtual std::pair<SubsecondTime, HitWhere::where_t> getDataFromDram(
      IntPtr addr, core_id_t requester, SubsecondTime now, ShmemPerf* perf,
      Byte* rd_data);
  virtual std::pair<SubsecondTime, HitWhere::where_t> putDataToDram(
      IntPtr addr, core_id_t requester, Byte* wr_data, SubsecondTime now);

 private:
  core_id_t m_core_id;
  UInt32 m_cache_block_size;
  SubsecondTime m_data_access_time;
  SubsecondTime m_tags_access_time;
  ComponentBandwidth m_data_array_bandwidth;

  AddressHomeLookup* m_home_lookup;
  DramCntlrInterface* m_dram_cntlr;
  Cache* m_cache;
  QueueModel* m_queue_model;
  Prefetcher* m_prefetcher;
  bool m_prefetch_on_prefetch_hit;
  ContentionModel m_prefetch_mshr;

  UInt64 m_reads, m_writes;
  UInt64 m_read_misses, m_write_misses;
  UInt64 m_hits_prefetch, m_prefetches;
  SubsecondTime m_prefetch_mshr_delay;

  std::pair<bool, SubsecondTime> doAccess(Cache::access_t access_type,
                                          IntPtr addr, core_id_t requester,
                                          Byte* acc_data, SubsecondTime now,
                                          ShmemPerf* perf);
  void putDataToCache(Cache::access_t access_type, IntPtr addr,
                      core_id_t requester, const Byte* wr_data,
                      SubsecondTime now);
  SubsecondTime accessDataArray(Cache::access_t access_type,
                                core_id_t requester, SubsecondTime t_start,
                                ShmemPerf* perf);
  void callPrefetcher(IntPtr train_addr, bool dram_cache_hit, bool prefetch_hit,
                      SubsecondTime t_issue);
};

#endif  // __DRAM_CACHE_H__
