#include "cache_base.h"
#include "utils.h"
#include "log.h"
#include "rng.h"
#include "address_home_lookup.h"

CacheBase::CacheBase(
   String name, core_id_t core_id, UInt32 num_sets, UInt32 associativity, UInt32 blocksize,
   CacheBase::hash_t hash, AddressHomeLookup *ahl)
:
   m_name(name),
   m_core_id(core_id),
   m_cache_size(UInt64(num_sets) * associativity * blocksize),
   m_associativity(associativity),
   m_blocksize(blocksize),
   m_hash(hash),
   m_num_sets(num_sets),
   m_ahl(ahl)
{
   m_log_blocksize = floorLog2(m_blocksize);

   LOG_ASSERT_ERROR((m_num_sets == (1UL << floorLog2(m_num_sets))) || (hash != CacheBase::HASH_MASK),
      "Caches of non-power of 2 size need funky hash function");
}

CacheBase::~CacheBase()
{}

// utilities
CacheBase::hash_t
CacheBase::parseAddressHash(String hash_name)
{
   if (hash_name == "mask")
      return CacheBase::HASH_MASK;
   else if (hash_name == "mod")
      return CacheBase::HASH_MOD;
   else if (hash_name == "rng1_mod")
      return CacheBase::HASH_RNG1_MOD;
   else if (hash_name == "rng2_mod")
      return CacheBase::HASH_RNG2_MOD;
   else
      LOG_PRINT_ERROR("Invalid address hash function %s", hash_name.c_str());
}
