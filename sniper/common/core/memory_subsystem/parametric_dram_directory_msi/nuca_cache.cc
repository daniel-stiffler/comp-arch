#include "nuca_cache.h"
#include "config.hpp"
#include "memory_manager_base.h"
#include "pr_l1_cache_block_info.h"
#include "queue_model.h"
#include "shmem_perf.h"
#include "stats.h"

NucaCache::NucaCache(MemoryManagerBase* memory_manager,
                     ShmemPerfModel* shmem_perf_model,
                     AddressHomeLookup* home_lookup, UInt32 cache_block_size,
                     bool compressed,
                     ParametricDramDirectoryMSI::CacheParameters& parameters)
    : m_core_id(memory_manager->getCore()->getId()),
      m_memory_manager(memory_manager),
      m_shmem_perf_model(shmem_perf_model),
      m_home_lookup(home_lookup),
      m_cache_block_size(cache_block_size),
      m_data_access_time(parameters.data_access_time),
      m_tags_access_time(parameters.tags_access_time),
      m_data_array_bandwidth(
          8 * Sim()->getCfg()->getFloat("perf_model/nuca/bandwidth")),
      m_queue_model(NULL),
      m_reads(0),
      m_writes(0),
      m_read_misses(0),
      m_write_misses(0) {
  m_cache = new Cache("nuca-cache", "perf_model/nuca/cache", m_core_id,
                      parameters.num_sets, parameters.associativity,
                      m_cache_block_size,
                      compressed,  // NUCA cache doesn't support compression
                      parameters.replacement_policy, CacheBase::PR_L1_CACHE,
                      CacheBase::parseAddressHash(parameters.hash_function),
                      NULL, /* FaultinjectionManager */
                      home_lookup);

  if (Sim()->getCfg()->getBool("perf_model/nuca/queue_model/enabled")) {
    String queue_model_type =
        Sim()->getCfg()->getString("perf_model/nuca/queue_model/type");
    m_queue_model =
        QueueModel::create("nuca-cache-queue", m_core_id, queue_model_type,
                           m_data_array_bandwidth.getRoundedLatency(
                               8 * m_cache_block_size));  // bytes to bits
  }

  registerStatsMetric("nuca-cache", m_core_id, "reads", &m_reads);
  registerStatsMetric("nuca-cache", m_core_id, "writes", &m_writes);
  registerStatsMetric("nuca-cache", m_core_id, "read-misses", &m_read_misses);
  registerStatsMetric("nuca-cache", m_core_id, "write-misses", &m_write_misses);
}

NucaCache::~NucaCache() {
  delete m_cache;

  if (m_queue_model) delete m_queue_model;
}

std::pair<SubsecondTime, HitWhere::where_t> NucaCache::read(
    IntPtr address, Byte* data_buf, SubsecondTime now, ShmemPerf* perf,
    bool count) {

  assert(false); // Not implemented
  /*
   HitWhere::where_t hit_where = HitWhere::MISS;
   perf->updateTime(now);

   PrL1CacheBlockInfo* block_info =
   (PrL1CacheBlockInfo*)m_cache->peekSingleLine(address);
   SubsecondTime latency = m_tags_access_time.getLatency();
   perf->updateTime(now + latency, ShmemPerf::NUCA_TAGS);

   if (block_info)
   {
      WritebackLines evictions;
      std::vector<IntPtr> eviction_addrs;

      m_cache->accessSingleLine(address, Cache::LOAD, data_buf,
   m_cache_block_size, now + latency, true, &eviction_addrs, &evictions);
      assert(eviction_addrs.empty());
      latency += accessDataArray(Cache::LOAD, now + latency, perf);
      hit_where = HitWhere::NUCA_CACHE;
   }
   else
   {
      if (count) ++m_read_misses;
   }
   if (count) ++m_reads;

   return std::pair<SubsecondTime, HitWhere::where_t>(latency, hit_where);
   */
}

std::pair<SubsecondTime, HitWhere::where_t> NucaCache::write(
    IntPtr address, Byte* data_buf, SubsecondTime now, bool count,
    std::vector<IntPtr>* eviction_addrs, WritebackLines* evictions) {

  assert(false); // Not implemented
  /*
  HitWhere::where_t hit_where = HitWhere::MISS;

  PrL1CacheBlockInfo* block_info =
      (PrL1CacheBlockInfo*)m_cache->peekSingleLine(address);
  SubsecondTime latency = m_tags_access_time.getLatency();
  std::vector<IntPtr> _eviction_addrs;
  WritebackLines _evictions;

  if (block_info) {
    block_info->setCState(CacheState::MODIFIED);
    m_cache->accessSingleLine(address, Cache::STORE, data_buf,
                              m_cache_block_size, now + latency, true,
                              &_eviction_addrs, &_evictions);
    latency += accessDataArray(Cache::STORE, now + latency, NULL);
    hit_where = HitWhere::NUCA_CACHE;
  } else {
    PrL1CacheBlockInfo evict_block_info;

    m_cache->insertSingleLine(address, data_buf, now + latency,
                              &_eviction_addrs, &_evictions, NULL);

    if (count) ++m_write_misses;
  }
  if (count) ++m_writes;

  if (!_eviction_addrs.empty()) {
    for (size_t i = 0; i < _eviction_addrs.size(); i++) {
      if (std::get<0>((*evictions)[i])->getCState() == CacheState::MODIFIED) {
        eviction_addrs->push_back(_eviction_addrs[i]);
        evictions->push_back(std::move(_evictions[i]));
      }
    }
  }

  return std::pair<SubsecondTime, HitWhere::where_t>(latency, hit_where);
  */
}

SubsecondTime NucaCache::accessDataArray(Cache::access_t access,
                                         SubsecondTime t_start,
                                         ShmemPerf* perf) {

  assert(false); // Not implemented
  /*
  perf->updateTime(t_start);

  // Compute Queue Delay
  SubsecondTime queue_delay;
  if (m_queue_model) {
    SubsecondTime processing_time = m_data_array_bandwidth.getRoundedLatency(
        8 * m_cache_block_size);  // bytes to bits

    queue_delay =
        processing_time +
        m_queue_model->computeQueueDelay(t_start, processing_time, m_core_id);

    perf->updateTime(t_start + processing_time, ShmemPerf::NUCA_BUS);
    perf->updateTime(t_start + queue_delay, ShmemPerf::NUCA_QUEUE);
  } else {
    queue_delay = SubsecondTime::Zero();
  }

  perf->updateTime(t_start + queue_delay + m_data_access_time.getLatency(),
                   ShmemPerf::NUCA_DATA);

  return queue_delay + m_data_access_time.getLatency();
  */
}
