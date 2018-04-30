#include "tlb.h"

#include <utility>

#include "compress_utils.h"
#include "stats.h"

namespace ParametricDramDirectoryMSI {

TLB::TLB(String name, String cfgname, core_id_t core_id, UInt32 num_entries,
         UInt32 associativity, TLB* next_level)
    : m_size(num_entries),
      m_associativity(associativity),
      m_cache(name + "_cache", cfgname, core_id, num_entries / associativity,
              associativity, 1 /* Must not be 0 for address splitting */,
              false /* TLB cannot be compressed */,
              "lru", CacheBase::PR_L1_CACHE),
      m_next_level(next_level),
      m_access(0),
      m_miss(0) {

  LOG_ASSERT_ERROR((num_entries / associativity) * associativity == num_entries,
                   "Invalid TLB configuration: num_entries(%d) must be a "
                   "multiple of the associativity(%d)",
                   num_entries, associativity);

  registerStatsMetric(name, core_id, "access", &m_access);
  registerStatsMetric(name, core_id, "miss", &m_miss);
}

bool TLB::lookup(IntPtr address, SubsecondTime now, bool allocate_on_miss) {
  IntPtr vpn = address >> SIM_PAGE_SHIFT;

  LOG_PRINT("TLB accessing line with address: %lx vpn: %lx", address, vpn);
  CacheBlockInfo* tlb_block_info = m_cache.accessSingleLine(vpn, Cache::LOAD, nullptr, 0, now,
                                      true, nullptr);

  bool hit = (tlb_block_info != nullptr);

  m_access++;

  if (hit) {
    return true;
  } else {

    m_miss++;

    if (m_next_level) {
      // Lookup without allocation
      hit = m_next_level->lookup(address, now, false);
    }

    if (allocate_on_miss) {
      allocate(address, now);
    }

    return hit;
  }
}

void TLB::allocate(IntPtr address, SubsecondTime now) {
  IntPtr vpn = address >> SIM_PAGE_SHIFT;

  WritebackLines writebacks;
  writebacks.reserve(1);

  m_cache.insertSingleLine(vpn, nullptr, now, &writebacks);

  // Use next level as a victim cache
  if (!writebacks.empty() && m_next_level) {
    for (const auto& e : writebacks) {
      IntPtr evict_addr = std::get<0>(e);
      m_next_level->allocate(evict_addr, now);
    }
  }
}

}  // namespace ParametricDramDirectoryMSI
