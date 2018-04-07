#include <utility>

#include "cache.h"
#include "log.h"
#include "simulator.h"

// Cache class
// constructors/destructors
Cache::Cache(String name, String cfgname, core_id_t core_id, UInt32 num_sets,
             UInt32 associativity, UInt32 cache_block_size, bool compressible,
             String replacement_policy, cache_t cache_type, hash_t hash,
             FaultInjector* fault_injector, AddressHomeLookup* ahl)
    : CacheBase(name, num_sets, associativity, cache_block_size, hash, ahl),
      m_enabled(false),
      m_compressible(compressible),
      m_num_accesses(0),
      m_num_hits(0),
      m_cache_type(cache_type),
      m_fault_injector(fault_injector) {

  m_set_info = std::move(
      CacheSet::createCacheSetInfo(name, cfgname, core_id, replacement_policy,
                                   m_associativity, m_compressible));

  // Populate m_sets with newly-constructed objects
  for (UInt32 i = 0; i < m_num_sets; i++) {
    // CacheSet::createCacheSet is a factory function for CacheSet objects, so
    // use move semantics to push the unique pointers into the vector
    m_sets.push_back(std::move(CacheSet::createCacheSet(
        cfgname, core_id, replacement_policy, m_cache_type, m_associativity,
        m_blocksize, m_compressible, m_set_info.get())));
  }

#ifdef ENABLE_SET_USAGE_HIST
  // Reserve and construct memory to hold the elements
  m_set_usage_hist.resize(m_num_sets);
  for (auto& e : m_set_usage_hist) e = 0;  // Zero out the memory counters
#endif
}

Cache::~Cache() {
#ifdef ENABLE_SET_USAGE_HIST
  printf("Cache %s set usage:", m_name.c_str());
  for (const auto& e : m_set_usage_hist) {
    printf(" %" PRId64, e);
  }
  printf("\n");
#endif

  // Let the containers go out of scope and automatically call their respective
  // destructors via RAII
}

Lock& Cache::getSetLock(IntPtr addr) {
  IntPtr tag;
  UInt32 set_index;

  splitAddress(addr, tag, set_index);
  assert(set_index < m_num_sets);

  return m_sets[set_index]->getLock();
}

bool Cache::invalidateSingleLine(IntPtr addr) {
  IntPtr tag;
  UInt32 set_index;

  splitAddress(addr, tag, set_index);
  assert(set_index < m_num_sets);

  return m_sets[set_index]->invalidate(tag);
}

CacheBlockInfo* Cache::accessSingleLine(IntPtr addr, access_t access_type,
                                        Byte* acc_data, UInt32 bytes,
                                        SubsecondTime now,
                                        bool update_replacement,
                                        std::vector<IntPtr>* writeback_addrs,
                                        WritebackLines* writebacks,
                                        CacheCntlr* cntlr) {
  assert(writeback_addrs != nullptr);
  assert(writebacks != nullptr);
  UInt32 prior_writebacks = writebacks->size();

  IntPtr tag;
  UInt32 set_index;
  UInt32 way;
  UInt32 block_id;
  UInt32 offset;

  splitAddress(addr, tag, set_index, block_id, offset);

  CacheSet* set                    = m_sets[set_index].get();
  CacheBlockInfo* block_info = set->find(tag, &way, &block_id);

  if (block_info == nullptr) return nullptr;

  if (access_type == LOAD) {
    set->readLine(way, block_id, offset, bytes, update_replacement, acc_data);
  } else {
    set->writeLine(way, block_id, offset, acc_data, bytes, update_replacement,
                   writebacks, cntlr);
  }
  for (UInt32 i = prior_writebacks; i < writebacks->size(); ++i) {
    const WritebackTuple& tmp_tuple            = (*writebacks)[i];
    const CacheBlockInfo* tmp_block_info = std::get<0>(tmp_tuple).get();
    IntPtr addr = tagToAddress(tmp_block_info->getTag());
    
    writeback_addrs->push_back(addr);
  }

  return block_info;
}

void Cache::insertSingleLine(IntPtr addr, Byte* ins_data, SubsecondTime now,
                             std::vector<IntPtr>* writeback_addrs,
                             WritebackLines* writebacks, CacheCntlr* cntlr) {

  assert(ins_data != nullptr);
  assert(writeback_addrs != nullptr);
  assert(writebacks != nullptr);

  IntPtr tag;
  UInt32 set_index;
  UInt32 block_id;
  splitAddress(addr, tag, set_index, block_id);

  CacheBlockInfoUPtr block_info =
      std::move(CacheBlockInfo::create(m_cache_type));
  block_info->setTag(tag);

  // Record the number of writebacks prior to performing the insertion, so that
  // we can iterate through the results and pick out their addresses
  UInt32 prior_writebacks = writebacks->size();

  CacheSet* set = m_sets[set_index].get();
  set->insertLine(std::move(block_info), ins_data, writebacks, cntlr);

  // Iterate through the new additions to the writebacks vector and add each of
  // their addresses to the eviction queue.
  //
  // The tags consist of the upper address bits, minus the block offset, so it
  // already includes the set index etc.
  for (UInt32 i = prior_writebacks; i < writebacks->size(); ++i) {
    const WritebackTuple& tmp_tuple            = (*writebacks)[i];
    const CacheBlockInfo* tmp_block_info = std::get<0>(tmp_tuple).get();
    IntPtr addr = tagToAddress(tmp_block_info->getTag());

    writeback_addrs->push_back(addr);
  }

#ifdef ENABLE_SET_USAGE_HIST
  ++m_set_usage_hist[set_index];
#endif
}

CacheBlockInfo* Cache::peekSingleLine(IntPtr addr) {
  IntPtr tag;
  UInt32 set_index;
  splitAddress(addr, tag, set_index);

  return m_sets[set_index]->find(tag);
}

void Cache::updateCounters(bool cache_hit) {
  if (m_enabled) {
    m_num_accesses++;

    if (cache_hit) m_num_hits++;
  }
}

void Cache::updateHits(Core::mem_op_t mem_op_type, UInt64 hits) {
  if (m_enabled) {
    m_num_accesses += hits;
    m_num_hits += hits;
  }
}
