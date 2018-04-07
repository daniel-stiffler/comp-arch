#include "tlb.h"
#include "stats.h"

namespace ParametricDramDirectoryMSI
{

TLB::TLB(String name, String cfgname, core_id_t core_id, UInt32 num_entries, UInt32 associativity, TLB *next_level)
   : m_size(num_entries)
   , m_associativity(associativity)
   , m_cache(name + "_cache", cfgname, core_id, num_entries / associativity, associativity, false ,SIM_PAGE_SIZE, "lru", CacheBase::PR_L1_CACHE)
     // compressed TLB are not supported
   , m_next_level(next_level)
   , m_access(0)
   , m_miss(0)
{
   LOG_ASSERT_ERROR((num_entries / associativity) * associativity == num_entries, "Invalid TLB configuration: num_entries(%d) must be a multiple of the associativity(%d)", num_entries, associativity);

   registerStatsMetric(name, core_id, "access", &m_access);
   registerStatsMetric(name, core_id, "miss", &m_miss);
}

bool
TLB::lookup(IntPtr address, SubsecondTime now, bool allocate_on_miss)
{
   WritebackLines evictions;
   std::vector<IntPtr> eviction_addrs;
   bool hit = m_cache.accessSingleLine(address, Cache::LOAD, NULL, 0, now, true, &eviction_addrs, &evictions);

   m_access++;

   if (hit)
      return true;

   m_miss++;

   if (m_next_level)
   {
      hit = m_next_level->lookup(address, now, false /* no allocation */);
   }

   if (allocate_on_miss)
   {
      allocate(address, now);
   }
  
   // Use next level as a victim cache
   if (!eviction_addrs.empty() && m_next_level)
   {
      for(auto& eviction_addr : eviction_addrs)
      {
         m_next_level->allocate(eviction_addr, now);
      }
   }


   return hit;
}

void
TLB::allocate(IntPtr address, SubsecondTime now)
{
   WritebackLines evictions;
   std::vector<IntPtr> eviction_addrs;
   m_cache.insertSingleLine(address, NULL, now, &eviction_addrs, &evictions, NULL);

   // Use next level as a victim cache
   if (!eviction_addrs.empty() && m_next_level)
   {
      for(auto& eviction_addr : eviction_addrs)
      {
         m_next_level->allocate(eviction_addr, now);
      }
   }
}

}
