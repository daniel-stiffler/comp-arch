#include <cmath>
#include <utility>

#include "address_home_lookup.h"
#include "cache.h"
#include "log.h"
#include "rng.h"
#include "simulator.h"

// Cache class
// constructors/destructors
Cache::Cache(String name, String cfgname, core_id_t core_id, UInt32 num_sets,
             UInt32 associativity, UInt32 blocksize, bool compressible,
             String replacement_policy, cache_t cache_type, hash_t hash,
             FaultInjector* fault_injector, AddressHomeLookup* ahl)
    : CacheBase(name, num_sets, associativity, blocksize, hash, ahl),
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
  m_sets.reserve(m_num_sets);
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
                                        WritebackLines* writebacks,
                                        CacheCntlr* cntlr) {

  IntPtr tag;
  UInt32 set_index;
  UInt32 way;
  UInt32 block_id;
  UInt32 offset;

  splitAddress(addr, tag, set_index, block_id, offset);

  CacheSet* set              = m_sets[set_index].get();
  CacheBlockInfo* block_info = set->find(tag, &way, &block_id);

  if (block_info == nullptr) return nullptr;

  if (access_type == LOAD) {
    set->readLine(way, block_id, offset, bytes, update_replacement, acc_data);
  } else {
    set->writeLine(way, block_id, offset, acc_data, bytes, update_replacement,
                   writebacks, cntlr);
  }

  return block_info;
}

void Cache::insertSingleLine(IntPtr addr, const Byte* ins_data,
                             SubsecondTime now, WritebackLines* writebacks,
                             CacheCntlr* cntlr) {

  // TODO: TLB implementation uses Cache objects to store
  assert(ins_data != nullptr);
  assert(writebacks != nullptr);

  IntPtr tag;
  UInt32 set_index;
  UInt32 block_id;
  splitAddress(addr, tag, set_index, block_id);

  CacheBlockInfoUPtr block_info =
      std::move(CacheBlockInfo::create(m_cache_type));
  block_info->setTag(tag);

  CacheSet* set = m_sets[set_index].get();
  set->insertLine(std::move(block_info), ins_data, writebacks, cntlr);

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

void Cache::splitAddress(const IntPtr addr, IntPtr& tag) const {
  UInt32 tmp_set_index;
  UInt32 tmp_block_id;
  UInt32 tmp_offset;

  splitAddress(addr, tag, tmp_set_index, tmp_block_id, tmp_offset);
}
void Cache::splitAddress(const IntPtr addr, IntPtr& tag,
                         UInt32& set_index) const {

  UInt32 tmp_block_id;
  UInt32 tmp_offset;

  splitAddress(addr, tag, set_index, tmp_block_id, tmp_offset);
}
void Cache::splitAddress(const IntPtr addr, IntPtr& tag, UInt32& set_index,
                         UInt32& block_id) const {
  UInt32 tmp_offset;

  splitAddress(addr, tag, set_index, block_id, tmp_offset);
}
void Cache::splitAddress(const IntPtr addr, IntPtr& tag, UInt32& set_index,
                         UInt32& block_id, UInt32& offset) const {

  UInt32 log2_blocksize = std::log2(m_blocksize);

  tag    = addr >> log2_blocksize;
  offset = addr & (m_blocksize - 1);

  IntPtr linear_addr    = m_ahl ? m_ahl->getLinearAddress(addr) : addr;
  IntPtr superblock_num = linear_addr >> log2_blocksize;
  IntPtr block_num      = superblock_num >> SUPERBLOCK_SIZE;

  block_id = block_num & (SUPERBLOCK_SIZE - 1);  // Superblocks are consecutive

  switch (m_hash) {
    case CacheBase::HASH_MASK:
      set_index = block_num & (m_num_sets - 1);
      break;
    case CacheBase::HASH_MOD:
      set_index = block_num % m_num_sets;
      break;
    case CacheBase::HASH_RNG1_MOD: {
      UInt64 state = rng_seed(block_num);
      set_index    = rng_next(state) % m_num_sets;
      break;
    }
    case CacheBase::HASH_RNG2_MOD: {
      UInt64 state = rng_seed(block_num);
      rng_next(state);
      set_index = rng_next(state) % m_num_sets;
      break;
    }
    default:
      LOG_PRINT_ERROR("Invalid hash function %d", m_hash);
      assert(false);
  }
}

IntPtr Cache::tagToAddress(IntPtr tag) const {
  UInt32 log2_blocksize = std::log2(m_blocksize);
  return tag << log2_blocksize;
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
