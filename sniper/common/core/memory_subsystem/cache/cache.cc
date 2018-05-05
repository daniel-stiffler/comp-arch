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
    : CacheBase(name, core_id, num_sets, associativity, blocksize, hash, ahl),
      m_enabled(false),
      m_num_accesses(0),
      m_num_hits(0),
      m_cache_type(cache_type),
      m_fault_injector(fault_injector),
      m_compress_cntlr(new CacheCompressionCntlr(
          compressible, change_scheme_otf, prune_dish_entries)) {

  m_set_info =
      CacheSet::createCacheSetInfo(name, cfgname, core_id, replacement_policy,
                                   m_associativity, m_compress_cntlr.get());

  // Populate m_sets with newly-constructed objects
  m_sets.reserve(m_num_sets);
  for (UInt32 i = 0; i < m_num_sets; i++) {
    // CacheSet::createCacheSet is a factory function for CacheSet objects, so
    // use move semantics to push the unique pointers into the vector
    m_sets.push_back(std::move(CacheSet::createCacheSet(
        i, cfgname, core_id, replacement_policy, m_cache_type, m_associativity,
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

bool Cache::isCompressible() const { return m_compress_cntlr->canCompress(); }
UInt32 Cache::getSuperblockSize() const { return SUPERBLOCK_SIZE; }

void Cache::invalidateSingleLine(IntPtr addr) {
  IntPtr tag;
  UInt32 set_index;
  UInt32 block_id;
  splitAddress(addr, &tag, nullptr, &set_index, &block_id);

  LOG_PRINT(
      "(%s->%p): Invalidating single line addr: %lx (tag: %lx set_index: %u "
      "block_id: %u)",
      m_name.c_str(), this, addr, tag, set_index, block_id);

  m_sets[set_index]->invalidate(tag, block_id);
}

CacheBlockInfo* Cache::accessSingleLine(IntPtr addr, access_t access_type,
                                        Byte* acc_data, UInt32 bytes,
                                        SubsecondTime now,
                                        bool update_replacement,
                                        WritebackLines* writebacks,
                                        CacheCntlr* cntlr) {

  /*
   * Due to the way SNIPER and PIN paritition the address spaces and monitor
   * memory accesses, the system does not pass real program data down the
   * hierarchy.  The root cause is on line 48 of DynamicInstruction, where the
   * simulator initiates an Cache::accessMemory with NULL as the data_buffer
   * parameter.  The way the latter function has been modified from Graphite
   * basically causes a memcpy with the program data then forces the pointer to
   * be null -- invalidating the copy operation.  There is _NO WORKAROUND_ for
   * multithreaded application, which host threads in separate address spaces
   * (sometimes), but we can dereference ca_address to get the data in the main
   * thread.
   *
   * Case 1: Memory accesses made using the Nehalem controller pass nullptr
   * data_buf down through the hierarchy with a proper data_length.  This is
   * done to ensure that usage bits get updated appropriately.  If this happens,
   * the object needs to dereference addr to get the real data.
   *
   * Case 2: Whenever accessSingleLine is used to handle writebacks in the cache
   * hierarchy, it should contain a data buffer that we created.  Therefore, we
   * can use the actual (possibly stale) contents instead of getting the real
   * data.
   *
   * Case 3: Cache objects with blocksize 1 are used to model i and d TLBs in
   * addition to actual memory.  They have a tell-tale call signature, however,
   * since bytes is always 0.  Unfortunately, they have to use this function as
   * an interface too.
   */

  IntPtr tag;
  UInt32 set_index;
  UInt32 block_id;
  UInt32 offset;
  splitAddress(addr, &tag, nullptr, &set_index, &block_id, &offset);
  const Byte* real_data =
      reinterpret_cast<const Byte*>(addr);  // EXTREME DANGER!!!

  CacheSet* set = m_sets[set_index].get();

  UInt32 init_way;
  CacheBlockInfo* block_info = set->find(tag, block_id, &init_way);
  if (block_info == nullptr) return nullptr;  // Cache MISS

  if (access_type == LOAD) {
    LOG_PRINT(
        "(%s->%p): Loading single line addr: %lx (tag: %lx set_index: %u way: "
        "%u"
        "block_id: %u offset: %u) into acc_data: %p bytes: %u",
        m_name.c_str(), this, addr, tag, set_index, init_way, block_id,
        acc_data, bytes);

    set->readLine(tag, block_id, offset, bytes, update_replacement, acc_data);
  } else {
    assert(writebacks != nullptr);
    assert(cntlr != nullptr);

    const Byte* wr_data_mux = nullptr;  // Proxy for write data buffer

    if (acc_data == nullptr && bytes != 0) {
      const UInt32* tmp = reinterpret_cast<const UInt32*>(real_data);
      LOG_PRINT("(%s->%p): Fetching real data for write addr: %lx %s",
                m_name.c_str(), this, addr,
                printChunks(tmp, m_blocksize / 4).c_str());

      wr_data_mux = real_data;
    } else if (acc_data != nullptr && bytes != 0) {
      LOG_PRINT("(%s->%p): Using hierarchy data for write addr: %lx %s",
                m_name.c_str(), this, addr,
                printBytes(acc_data + offset, bytes).c_str());

      wr_data_mux = acc_data;
    } else if (bytes == 0) {
      wr_data_mux = acc_data;
    }

    LOG_PRINT(
        "(%s->%p): Storing single line addr: %lx (tag: %lx set_index: %u "
        "init_way: %u block_id: %u offset: %u) from wr_data: %p bytes: %u",
        m_name.c_str(), this, addr, tag, set_index, init_way, block_id, offset,
        wr_data_mux, bytes);

    set->writeLine(tag, block_id, offset, wr_data_mux, bytes,
                   update_replacement, writebacks, cntlr);
  }

  return block_info;
}

void Cache::insertSingleLine(IntPtr addr, const Byte* ins_data,
                             SubsecondTime now, bool is_fill,
                             WritebackLines* writebacks, CacheCntlr* cntlr) {

  /*
   * Due to the way SNIPER and PIN paritition the address spaces and monitor
   * memory accesses, the system does not pass real program data down the
   * hierarchy.  The root cause is on line 48 of DynamicInstruction, where the
   * simulator initiates an Cache::accessMemory with NULL as the data_buffer
   * parameter.  The way the latter function has been modified from Graphite
   * basically causes a memcpy with the program data then forces the pointer to
   * be null -- invalidating the copy operation.  There is _NO WORKAROUND_ for
   * multithreaded application, which host threads in separate address spaces
   * (sometimes), but we can dereference ca_address to get the data in the main
   * thread.
   *
   * Case 1: Memory accesses made using the Nehalem controller pass nullptr
   * data_buf down through the hierarchy with a proper data_length.  This is
   * done to ensure that usage bits get updated appropriately.  If this happens,
   * the object needs to dereference addr to get the real data.
   *
   * Case 2: Whenever accessSingleLine is used to handle writebacks in the cache
   * hierarchy, it should contain a data buffer that we created.  Therefore, we
   * can use the actual (possibly stale) contents instead of getting the real
   * data.
   *
   * Case 3: Cache objects with blocksize 1 are used to model i and d TLBs in
   * addition to actual memory.  They have a tell-tale call signature, however,
   * since cntlr is always nullptr.
   */

  assert(writebacks != nullptr);

  IntPtr tag;
  UInt32 set_index;
  UInt32 block_id;
  splitAddress(addr, &tag, nullptr, &set_index, &block_id);
  const Byte* real_data =
      reinterpret_cast<const Byte*>(addr);  // EXTREME DANGER!!!

  const Byte* ins_data_mux = nullptr;

  if (cntlr != nullptr) {  // Mux insert data for data caches only
    if (is_fill) {
      assert(ins_data != nullptr);

      const UInt32* tmp = reinterpret_cast<const UInt32*>(ins_data);
      LOG_PRINT("(%s->%p): Using hierarchy data for write addr: %lx %s",
                m_name.c_str(), this, addr,
                printChunks(tmp, m_blocksize / 4).c_str());

      ins_data_mux = ins_data;
    } else {
      const UInt32* tmp = reinterpret_cast<const UInt32*>(real_data);
      LOG_PRINT("(%s->%p): Fetching real data for insertion addr: %lx %s",
                m_name.c_str(), this, addr,
                printChunks(tmp, m_blocksize / 4).c_str());

      ins_data_mux = real_data;
    }
  } else {  // TLB accesses only
    if (ins_data != nullptr) {
      const UInt32* tmp = reinterpret_cast<const UInt32*>(ins_data);
      LOG_PRINT_WARNING(
          "(%p %s): Attempting insertion without a cache controller reference "
          "%s",
          m_name.c_str(), this, printChunks(tmp, m_blocksize / 4).c_str());
    }
  }

  LOG_PRINT(
      "(%s->%p): Inserting single line addr: %lx (tag: %lx set_index: %u "
      "block_id: %u) from ins_data_mux: %p",
      m_name.c_str(), this, addr, tag, set_index, block_id, ins_data_mux);

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
