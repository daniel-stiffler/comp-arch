#pragma once

// Define to re-enable DramAccessCount
//#define ENABLE_DRAM_ACCESS_COUNT

#include "dram_cntlr_interface.h"

#include <unordered_map>

#include "dram_perf_model.h"
#include "fixed_types.h"
#include "memory_manager_base.h"
#include "shmem_msg.h"
#include "subsecond_time.h"

class FaultInjector;

namespace PrL1PrL2DramDirectoryMSI {
class DramCntlr : public DramCntlrInterface {
 private:
  std::unordered_map<IntPtr, Byte*> m_data_map;
  DramPerfModel* m_dram_perf_model;
  FaultInjector* m_fault_injector;

  typedef std::unordered_map<IntPtr, UInt64> AccessCountMap;
  AccessCountMap* m_dram_access_count;
  UInt64 m_reads, m_writes;

  SubsecondTime runDramPerfModel(core_id_t requester, SubsecondTime time,
                                 IntPtr address,
                                 DramCntlrInterface::access_t access_type,
                                 ShmemPerf* perf);

  void addToDramAccessCount(IntPtr address, access_t access_type);
  void printDramAccessCount(void);

 public:
  DramCntlr(MemoryManagerBase* memory_manager, ShmemPerfModel* shmem_perf_model,
            UInt32 cache_block_size);

  ~DramCntlr();

  DramPerfModel* getDramPerfModel() { return m_dram_perf_model; }

  // Run DRAM performance model. Pass in begin time, returns latency
  std::pair<SubsecondTime, HitWhere::where_t> getDataFromDram(
      IntPtr addr, core_id_t requester, SubsecondTime now, ShmemPerf* perf,
      Byte* rd_data);
  std::pair<SubsecondTime, HitWhere::where_t> putDataToDram(IntPtr addr,
                                                            core_id_t requester,
                                                            Byte* wr_data,
                                                            SubsecondTime now);
};
}
