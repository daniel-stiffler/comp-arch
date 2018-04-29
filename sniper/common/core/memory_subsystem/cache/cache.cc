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
             FaultInjector* fault_injector, AddressHomeLookup* ahl,
             bool change_scheme_otf, bool prune_dish_entries)
    : CacheBase(name, num_sets, associativity, blocksize, hash, ahl),
      m_enabled(false),
      m_num_accesses(0),
      m_num_hits(0),
      m_cache_type(cache_type),
      m_fault_injector(fault_injector),
      m_compression_cntlr(new CacheCompressionCntlr(
          compressible, change_scheme_otf, prune_dish_entries)) {

  m_set_info =
      CacheSet::createCacheSetInfo(name, cfgname, core_id, replacement_policy,
                                   m_associativity, m_compression_cntlr.get());

  // Populate m_sets with newly-constructed objects
  m_sets.reserve(m_num_sets);
  for (UInt32 i = 0; i < m_num_sets; i++) {
    // CacheSet::createCacheSet is a factory function for CacheSet objects, so
    // use move semantics to push the unique pointers into the vector
    m_sets.push_back(std::move(CacheSet::createCacheSet(
        cfgname, core_id, replacement_policy, m_cache_type, m_associativity,
        m_blocksize, m_compression_cntlr.get(), this, m_set_info.get())));
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
  UInt32 set_index;
  splitAddress(addr, nullptr, nullptr, &set_index);

  return m_sets[set_index]->getLock();
}

bool Cache::isCompressible() { return m_compression_cntlr->canCompress(); }
UInt32 Cache::getSuperblockSize() { return SUPERBLOCK_SIZE; }

bool Cache::invalidateSingleLine(IntPtr addr) {
  IntPtr tag;
  UInt32 set_index;
  splitAddress(addr, &tag, nullptr, &set_index);

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
  UInt32 block_id;
  UInt32 offset;
  splitAddress(addr, &tag, nullptr, &set_index, &block_id, &offset);

  CacheSet* set = m_sets[set_index].get();

  UInt32 way;
  CacheBlockInfo* block_info = set->find(tag, block_id, &way);

  if (block_info == nullptr) return nullptr;

  if (access_type == LOAD) {
    LOG_PRINT(
        "Cache accessing (LOAD) line addr: %lx tag: %lx set_index: %u "
        "block_id: %u "
        "offset: %u acc_data: %p bytes: %u",
        addr, tag, set_index, block_id, offset, acc_data, bytes);

    set->readLine(way, block_id, offset, bytes, update_replacement, acc_data);
  } else {
    LOG_PRINT(
        "Cache accessing (STORE) line addr: %lx tag: %lx set_index: %u "
        "block_id: %u "
        "offset: %u acc_data: %p bytes: %u",
        addr, tag, set_index, block_id, offset, acc_data, bytes);

    set->writeLine(way, block_id, offset, acc_data, bytes, update_replacement,
                   writebacks, cntlr);
  }

  return block_info;
}

void Cache::insertSingleLine(IntPtr addr, const Byte* ins_data,
                             SubsecondTime now, WritebackLines* writebacks,
                             CacheCntlr* cntlr) {

  assert(writebacks != nullptr);

  IntPtr tag;
  UInt32 set_index;
  splitAddress(addr, &tag, nullptr, &set_index);

  LOG_PRINT(
      "Cache inserting line addr: %lx tag: %lx set_index: %u ins_data: %p",
      addr, tag, set_index, ins_data);

  CacheBlockInfoUPtr block_info = CacheBlockInfo::create(m_cache_type);
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
  UInt32 block_id;
  splitAddress(addr, &tag, nullptr, &set_index, &block_id);

  LOG_PRINT(
      "Cache peeking line addr: %lx tag: %lx set_index: %u block_id: %u",
      addr, tag, set_index, block_id);

  return m_sets[set_index]->find(tag, block_id);
}

CacheBlockInfo* Cache::peekBlock(UInt32 set_index, UInt32 way,
                                 UInt32 block_id) const {

  return m_sets[set_index]->peekBlock(way, block_id);
}

void Cache::splitAddress(IntPtr addr, IntPtr* tag, IntPtr* supertag,
                         UInt32* set_index, UInt32* block_id,
                         UInt32* offset) const {

  UInt32 log2_superblock_size = std::log2(SUPERBLOCK_SIZE);
  UInt32 log2_blocksize       = std::log2(m_blocksize);

  if (tag) *tag       = addr >> log2_blocksize;
  if (offset) *offset = addr & (m_blocksize - 1);

  IntPtr linear_addr    = m_ahl ? m_ahl->getLinearAddress(addr) : addr;
  IntPtr superblock_num = linear_addr >> log2_blocksize;

  if (supertag) *supertag = superblock_num >> log2_superblock_size;
  if (block_id) *block_id = superblock_num & (SUPERBLOCK_SIZE - 1);

  IntPtr block_num = superblock_num >> SUPERBLOCK_SIZE;

  UInt32 tmp_set_index;
  switch (m_hash) {
    case CacheBase::HASH_MASK:
      tmp_set_index = block_num & (m_num_sets - 1);
      break;
    default:
      LOG_PRINT_ERROR("Invalid or unsupported hash function %d", m_hash);
      assert(false);
  }

  if (set_index) *set_index = tmp_set_index;
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

void Cache::enable() { m_enabled = true; }
void Cache::disable() { m_enabled = false; }
