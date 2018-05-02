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
      m_compress_cntlr(new CacheCompressionCntlr(
          compressible, change_scheme_otf, prune_dish_entries)),
      m_core_id(core_id) {

  m_set_info =
      CacheSet::createCacheSetInfo(name, cfgname, core_id, replacement_policy,
                                   m_associativity, m_compress_cntlr.get());

  // Populate m_sets with newly-constructed objects
  m_sets.reserve(m_num_sets);
  for (UInt32 i = 0; i < m_num_sets; i++) {
    // CacheSet::createCacheSet is a factory function for CacheSet objects, so
    // use move semantics to push the unique pointers into the vector
    m_sets.push_back(std::move(CacheSet::createCacheSet(
        cfgname, core_id, replacement_policy, m_cache_type, m_associativity,
        m_blocksize, m_compress_cntlr.get(), this, m_set_info.get())));
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

bool Cache::isCompressible() { return m_compress_cntlr->canCompress(); }
UInt32 Cache::getSuperblockSize() { return SUPERBLOCK_SIZE; }

void Cache::invalidateSingleLine(IntPtr addr) {
  IntPtr tag;
  UInt32 set_index;
  UInt32 block_id;
  splitAddress(addr, &tag, nullptr, &set_index, &block_id);

  m_sets[set_index]->invalidate(tag, block_id);
}

CacheBlockInfo* Cache::accessSingleLine(IntPtr addr, access_t access_type,
                                        Byte* acc_data, UInt32 bytes,
                                        SubsecondTime now,
                                        bool update_replacement,
                                        WritebackLines* writebacks,
                                        CacheCntlr* cntlr) {

  /*
   * Due to the way SNIPER and PIN paritition the address spaces and monitor memory accesses,
   * the system does not pass real program data down the hierarchy.  The root cause is on line 
   * 48 of DynamicInstruction, where the simulator initiates an Cache::accessMemory with NULL
   * as the data_buffer parameter.  The way the latter function has been modified from Graphite
   * basically causes a memcpy with the program data then forces the pointer to be null -- invalidating
   * the copy operation.  There is _NO WORKAROUND_ for multithreaded application, which host threads
   * in separate address spaces (sometimes), but we can dereference ca_address to get the data in the
   * main thread.  
   * 
   * Case 1: 
   * Memory accesses made using the Nehalem controller pass nullptr
   * data_buf down through the hierarchy with a proper data_length.  This is done to ensure that usage bits get updated
   * appropriately.  If this happens, the object needs to dereference addr to get the real data.
   * 
   * Case 2:
   * Whenever accessSingleLine is used to handle writebacks in the cache hierarchy, it should contain a data buffer
   * that we created.  Therefore, we can use the actual (possibly stale) contents instead of getting the real data.
   *
   * Case 3:
   * Cache objects with blocksize 1 are used to model i and d TLBs in addition to actual memory.  They have a tell-tale
   * call signature, however, since bytes is always 0.  Unfortunately, they have to use this function as an interface to 
   */

  IntPtr tag;
  UInt32 set_index;
  UInt32 block_id;
  UInt32 offset;
  splitAddress(addr, &tag, nullptr, &set_index, &block_id, &offset);
  const Byte* real_data = reinterpret_cast<const Byte*>(addr);   // EXTREME DANGER!!!

  CacheSet* set = m_sets[set_index].get();

  UInt32 way;
  CacheBlockInfo* block_info = set->find(tag, block_id, &way);
  if (block_info == nullptr) return nullptr;  // Cache MISS

  if (access_type == LOAD) {
    LOG_PRINT(
        "Cache(%p %s) accessing (LOAD) line addr: %lx tag: %lx set_index: %u "
        "block_id: %u "
        "offset: %u acc_data: %p",
        this, m_name.c_str(), addr, tag, set_index, block_id, offset, acc_data);

    set->readLine(way, block_id, offset, bytes, update_replacement, acc_data);
  } else {
    assert(writebacks != nullptr);
    assert(cntlr != nullptr);

    const Byte* wr_data = nullptr;  // Proxy for write data buffer

    if (acc_data == nullptr && bytes != 0) {
      IntPtr cl_real_addr = tagToAddress(tag);
      LOG_PRINT("Cache(%p %s) fetching real data for write %s", this, m_name.c_str(), printChunks(reinterpret_cast<const UInt32 *>(cl_real_addr), m_blocksize / 4).c_str());
      wr_data = real_data;
    } else if (acc_data != nullptr && bytes != 0) {
      LOG_PRINT("Cache(%p %s) using hierarchy data for write %s", this, m_name.c_str(), printBytes(acc_data, bytes).c_str());
      wr_data = acc_data;
    } else if (bytes == 0) {
      LOG_PRINT_WARNING("Cache(%p %s) used as TLB attempting a write", this, m_name.c_str());
      wr_data = acc_data;
    }

    LOG_PRINT(
        "Cache(%p %s) accessing (STORE) line addr: %lx tag: %lx set_index: %u "
        "block_id: %u "
        "offset: %u acc_data: %p bytes: %u",
        this, m_name.c_str(), addr, tag, set_index, block_id, offset, wr_data, bytes);

    set->writeLine(way, block_id, offset, wr_data, bytes, update_replacement,
                   writebacks, cntlr);
  }

  return block_info;
}

void Cache::insertSingleLine(IntPtr addr, const Byte* ins_data,
                             SubsecondTime now, bool is_fill, WritebackLines* writebacks,
                             CacheCntlr* cntlr) {

  /*
   * Due to the way SNIPER and PIN paritition the address spaces and monitor memory accesses,
   * the system does not pass real program data down the hierarchy.  The root cause is on line 
   * 48 of DynamicInstruction, where the simulator initiates an Cache::accessMemory with NULL
   * as the data_buffer parameter.  The way the latter function has been modified from Graphite
   * basically causes a memcpy with the program data then forces the pointer to be null -- invalidating
   * the copy operation.  There is _NO WORKAROUND_ for multithreaded application, which host threads
   * in separate address spaces (sometimes), but we can dereference ca_address to get the data in the
   * main thread.  
   * 
   * Case 1: 
   * Memory accesses made using the Nehalem controller pass nullptr
   * data_buf down through the hierarchy with a proper data_length.  This is done to ensure that usage bits get updated
   * appropriately.  If this happens, the object needs to dereference addr to get the real data.
   * 
   * Case 2:
   * Whenever accessSingleLine is used to handle writebacks in the cache hierarchy, it should contain a data buffer
   * that we created.  Therefore, we can use the actual (possibly stale) contents instead of getting the real data.
   *
   * Case 3:
   * Cache objects with blocksize 1 are used to model i and d TLBs in addition to actual memory.  They have a tell-tale
   * call signature, however, since cntlr is always nullptr.  
   */
  // TODO: check REAL DATA
  assert(writebacks != nullptr);

  IntPtr tag;
  UInt32 set_index;
  splitAddress(addr, &tag, nullptr, &set_index);
  const Byte* real_data = reinterpret_cast<const Byte*>(addr);   // EXTREME DANGER!!!

  const Byte* ins_data_mux = nullptr;

  if (cntlr != nullptr) {  // Mux insert data for data caches only
    if (is_fill) {
      assert(ins_data != nullptr);
      ins_data_mux = ins_data;
    } else {
      IntPtr cl_real_addr = tagToAddress(tag);
      LOG_PRINT("Cache(%p %s) fetching real data for insertion %s", this, m_name.c_str(), printChunks(reinterpret_cast<const UInt32 *>(cl_real_addr), m_blocksize / 4).c_str());
      ins_data_mux = real_data;
    }
  } else {  // TLB accesses only
    if (ins_data != nullptr) {
      LOG_PRINT_WARNING("Cache(%p %s) used as TLB attempting a insertion with data %s", this, m_name.c_str(), printBytes(ins_data, m_blocksize).c_str());
    } 
  }

  LOG_PRINT(
      "Cache(%p %s) inserting line addr: %lx tag: %lx set_index: %u ins_data: %p",
      this, m_name.c_str(), addr, tag, set_index, ins_data);

  CacheBlockInfoUPtr block_info = CacheBlockInfo::create(m_cache_type);
  block_info->setTag(tag);

  CacheSet* set = m_sets[set_index].get();
  set->insertLine(std::move(block_info), ins_data_mux, writebacks, cntlr);

#ifdef ENABLE_SET_USAGE_HIST
  ++m_set_usage_hist[set_index];
#endif
}

CacheBlockInfo* Cache::peekSingleLine(IntPtr addr) {
  IntPtr tag;
  UInt32 set_index;
  UInt32 block_id;
  splitAddress(addr, &tag, nullptr, &set_index, &block_id);

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
