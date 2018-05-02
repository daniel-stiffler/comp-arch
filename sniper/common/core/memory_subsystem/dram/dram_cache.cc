#include <vector>

#include "cache.h"
#include "config.hpp"
#include "dram_cache.h"
#include "memory_manager_base.h"
#include "pr_l1_cache_block_info.h"
#include "prefetcher.h"
#include "queue_model.h"
#include "shmem_perf.h"
#include "simulator.h"
#include "stats.h"

DramCache::DramCache(MemoryManagerBase* memory_manager,
                     ShmemPerfModel* shmem_perf_model,
                     AddressHomeLookup* home_lookup, UInt32 cache_block_size,
                     DramCntlrInterface* dram_cntlr)
    : DramCntlrInterface(memory_manager, shmem_perf_model, cache_block_size),
      m_core_id(memory_manager->getCore()->getId()),
      m_cache_block_size(cache_block_size),
      m_data_access_time(SubsecondTime::NS(Sim()->getCfg()->getIntArray(
          "perf_model/dram/cache/data_access_time", m_core_id))),
      m_tags_access_time(SubsecondTime::NS(Sim()->getCfg()->getIntArray(
          "perf_model/dram/cache/tags_access_time", m_core_id))),
      m_data_array_bandwidth(
          8 * Sim()->getCfg()->getFloat("perf_model/dram/cache/bandwidth")),
      m_home_lookup(home_lookup),
      m_dram_cntlr(dram_cntlr),
      m_queue_model(NULL),
      m_prefetcher(NULL),
      m_prefetch_mshr("dram-cache.prefetch-mshr", m_core_id, 16),
      m_reads(0),
      m_writes(0),
      m_read_misses(0),
      m_write_misses(0),
      m_hits_prefetch(0),
      m_prefetches(0),
      m_prefetch_mshr_delay(SubsecondTime::Zero()) {

  UInt32 cache_size = Sim()->getCfg()->getIntArray(
      "perf_model/dram/cache/cache_size", m_core_id);
  UInt32 associativity = Sim()->getCfg()->getIntArray(
      "perf_model/dram/cache/associativity", m_core_id);
  UInt32 num_sets = k_KILO * cache_size / (associativity * m_cache_block_size);

  LOG_ASSERT_ERROR(
      k_KILO * cache_size == num_sets * associativity * m_cache_block_size,
      "Invalid cache configuration: size(%d Kb) != sets(%d) * "
      "associativity(%d) * block_size(%d)",
      cache_size, num_sets, associativity, m_cache_block_size);

  m_cache = new Cache(
      "dram-cache", "perf_model/dram/cache", m_core_id, num_sets, associativity,
      m_cache_block_size, false,  // Cannot be compressed
      Sim()->getCfg()->getStringArray(
          "perf_model/dram/cache/replacement_policy", m_core_id),
      CacheBase::PR_L1_CACHE, /* Accelerator cache for prefetches only */
      CacheBase::parseAddressHash(Sim()->getCfg()->getStringArray(
          "perf_model/dram/cache/address_hash", m_core_id)),
      nullptr, /* FaultinjectionManager */
      home_lookup);

  if (Sim()->getCfg()->getBool("perf_model/dram/cache/queue_model/enabled")) {
    String queue_model_type =
        Sim()->getCfg()->getString("perf_model/dram/queue_model/type");
    m_queue_model =
        QueueModel::create("dram-cache-queue", m_core_id, queue_model_type,
                           m_data_array_bandwidth.getRoundedLatency(
                               8 * m_cache_block_size));  // bytes to bits
  }

  m_prefetcher = Prefetcher::createPrefetcher(
      Sim()->getCfg()->getString("perf_model/dram/cache/prefetcher"),
      "dram/cache", m_core_id, 1);
  m_prefetch_on_prefetch_hit = Sim()->getCfg()->getBool(
      "perf_model/dram/cache/prefetcher/prefetch_on_prefetch_hit");

  registerStatsMetric("dram-cache", m_core_id, "reads", &m_reads);
  registerStatsMetric("dram-cache", m_core_id, "writes", &m_writes);
  registerStatsMetric("dram-cache", m_core_id, "read-misses", &m_read_misses);
  registerStatsMetric("dram-cache", m_core_id, "write-misses", &m_write_misses);
  registerStatsMetric("dram-cache", m_core_id, "hits-prefetch",
                      &m_hits_prefetch);
  registerStatsMetric("dram-cache", m_core_id, "prefetches", &m_prefetches);
  registerStatsMetric("dram-cache", m_core_id, "prefetch-mshr-delay",
                      &m_prefetch_mshr_delay);
}

DramCache::~DramCache() {
  // Old way does not use RAII
  delete m_cache;

  // Queue model is only instantiated if the simulation configuration
  // specifies one
  if (m_queue_model) delete m_queue_model;
}

std::pair<SubsecondTime, HitWhere::where_t> DramCache::getDataFromDram(
    IntPtr addr, core_id_t requester, SubsecondTime now, ShmemPerf* perf,
    Byte* rd_data) {

  // Check the accelerator cache then the DRAM for the requested block
  bool dram_cache_hit;
  SubsecondTime latency;
  std::tie(dram_cache_hit, latency) =
      doAccess(Cache::LOAD, addr, requester, rd_data, now, perf);

  if (!dram_cache_hit) ++m_read_misses;
  ++m_reads;

  if (dram_cache_hit) {
    return std::pair<SubsecondTime, HitWhere::where_t>(latency,
                                                       HitWhere::DRAM_CACHE);
  } else {
    return std::pair<SubsecondTime, HitWhere::where_t>(latency, HitWhere::DRAM);
  }
}

std::pair<SubsecondTime, HitWhere::where_t> DramCache::putDataToDram(
    IntPtr addr, core_id_t requester, Byte* wr_data, SubsecondTime now) {

  /* wr_data cannot be const because doAccess uses the same pointer for reads
   * and writes */

  bool dram_cache_hit;
  SubsecondTime latency;
  std::tie(dram_cache_hit, latency) =
      doAccess(Cache::STORE, addr, requester, wr_data, now, nullptr);

  if (!dram_cache_hit) ++m_write_misses;
  ++m_writes;

  if (dram_cache_hit) {
    return std::pair<SubsecondTime, HitWhere::where_t>(latency,
                                                       HitWhere::DRAM_CACHE);
  } else {
    return std::pair<SubsecondTime, HitWhere::where_t>(latency, HitWhere::DRAM);
  }
}

std::pair<bool, SubsecondTime> DramCache::doAccess(
    Cache::access_t access_type, IntPtr addr, core_id_t requester,
    Byte* acc_data, SubsecondTime now, ShmemPerf* perf) {

  PrL1CacheBlockInfo* block_info =
      dynamic_cast<PrL1CacheBlockInfo*>(m_cache->peekSingleLine(addr));

  SubsecondTime latency = m_tags_access_time;
  perf->updateTime(now);
  perf->updateTime(now + latency, ShmemPerf::DRAM_CACHE_TAGS);

  bool dram_cache_hit = false;
  bool prefetch_hit   = false;

  if (block_info) {
    // Cache peek operation did not return a null pointer, indicating that it
    // contains the requested block
    dram_cache_hit = true;

    if (block_info->hasOption(CacheBlockInfo::PREFETCH)) {
      // This line was fetched by the prefetcher and has proven useful
      m_hits_prefetch++;
      prefetch_hit = true;
      block_info->clearOption(CacheBlockInfo::PREFETCH);

      // If prefetch is still in progress, delay
      SubsecondTime t_completed = m_prefetch_mshr.getTagCompletionTime(addr);
      if (t_completed != SubsecondTime::MaxTime() &&
          t_completed > now + latency) {
        m_prefetch_mshr_delay += t_completed - (now + latency);
        latency = t_completed - now;
      }
    }

    m_cache->accessSingleLine(addr, access_type, acc_data, m_cache_block_size,
                              now + latency, true, nullptr /* No writebacks */,
                              nullptr /* No controller reference */);

    latency += accessDataArray(access_type, requester, now + latency, perf);
    if (access_type == Cache::STORE) {
      block_info->setCState(CacheState::MODIFIED);
    }
  } else {
    // Cache peek operation returned a null pointer, indicating that it did
    // not contain the requested block
    if (access_type == Cache::LOAD) {
      // Get the data from DRAM for loads misses
      SubsecondTime dram_latency;
      HitWhere::where_t hit_where;
      std::tie(dram_latency, hit_where) = m_dram_cntlr->getDataFromDram(
          addr, requester, now + latency, perf, acc_data);

      latency += dram_latency;
    }

    // For load misses and all stores, the granularity is complete cache
    // lines so we don't need to access DRAM
    putDataToCache(access_type, addr, requester, acc_data, now + latency);
  }

  if (m_prefetcher) {
    callPrefetcher(addr, dram_cache_hit, prefetch_hit, now + latency);
  }

  return std::pair<bool, SubsecondTime>(dram_cache_hit, latency);
}

void DramCache::putDataToCache(Cache::access_t access_type, IntPtr addr,
                               core_id_t requester, const Byte* ins_data,
                               SubsecondTime now) {

  WritebackLines writebacks;
  writebacks.reserve(1);  // Reserve space for only one possible eviction

  m_cache->insertSingleLine(addr, ins_data, now, false, &writebacks,
                            nullptr /* No controller reference */);

  CacheBlockInfo* block_info = m_cache->peekSingleLine(addr);
  if (access_type == Cache::STORE) {
    block_info->setCState(CacheState::MODIFIED);
  } else {
    block_info->setCState(CacheState::SHARED);
  }

  // Write to data array off-line, so it doesn't affect return latency
  accessDataArray(Cache::STORE, requester, now, nullptr);

  // Writeback to DRAM done off-line, so don't affect return latency
  if (!writebacks.empty()) {
    for (const auto& wb : writebacks) {

      IntPtr evict_addr                      = std::get<0>(wb);
      const CacheBlockInfo* evict_block_info = std::get<1>(wb).get();
      const Byte* evict_block_data           = std::get<2>(wb).get();
      Byte* evict_block_data_unsafe = const_cast<Byte*>(evict_block_data);

      if (evict_block_info->getCState() == CacheState::MODIFIED) {
        m_dram_cntlr->putDataToDram(evict_addr, requester,
                                    evict_block_data_unsafe, now);
      }
    }
  }
}

SubsecondTime DramCache::accessDataArray(Cache::access_t access_type,
                                         core_id_t requester,
                                         SubsecondTime t_start,
                                         ShmemPerf* perf) {

  SubsecondTime processing_time = m_data_array_bandwidth.getRoundedLatency(
      8 * m_cache_block_size);  // bytes to bits

  // Compute Queue Delay
  SubsecondTime queue_delay;
  if (m_queue_model) {
    queue_delay =
        m_queue_model->computeQueueDelay(t_start, processing_time, requester);
  } else {
    queue_delay = SubsecondTime::Zero();
  }

  perf->updateTime(t_start);
  perf->updateTime(t_start + queue_delay, ShmemPerf::DRAM_CACHE_QUEUE);
  perf->updateTime(t_start + queue_delay + processing_time,
                   ShmemPerf::DRAM_CACHE_BUS);
  perf->updateTime(t_start + queue_delay + processing_time + m_data_access_time,
                   ShmemPerf::DRAM_CACHE_DATA);

  return queue_delay + processing_time + m_data_access_time;
}

void DramCache::callPrefetcher(IntPtr train_addr, bool dram_cache_hit,
                               bool prefetch_hit, SubsecondTime t_issue) {

  // Always train the prefetcher
  std::vector<IntPtr> prefetch_list =
      m_prefetcher->getNextAddress(train_addr, INVALID_CORE_ID);

  // Only do prefetches on misses, or on hits to lines previously brought in
  // by the prefetcher (if enabled)
  if (!dram_cache_hit || (m_prefetch_on_prefetch_hit && prefetch_hit)) {
    for (const auto prefetch_addr : prefetch_list) {
      CacheBlockInfo* prefetch_block_info =
          m_cache->peekSingleLine(prefetch_addr);

      if (!prefetch_block_info) {  // Line does not exist in the DRAM cache
        // Fill data from DRAM
        SubsecondTime dram_latency;
        HitWhere::where_t hit_where;
        Byte prefetch_data[m_cache_block_size];
        std::tie(dram_latency, hit_where) = m_dram_cntlr->getDataFromDram(
            prefetch_addr, m_core_id, t_issue, nullptr, prefetch_data);

        // Insert into DRAM cache
        putDataToCache(Cache::LOAD, prefetch_addr, m_core_id, prefetch_data,
                       t_issue + dram_latency);

        // Set prefetched bit
        prefetch_block_info = m_cache->peekSingleLine(prefetch_addr);
        // Cache must contain this line after prefetch
        assert(prefetch_block_info);
        prefetch_block_info->setOption(CacheBlockInfo::PREFETCH);

        // Update completion time
        m_prefetch_mshr.getCompletionTime(t_issue, dram_latency, prefetch_addr);

        ++m_prefetches;
      }
    }
  }
}
