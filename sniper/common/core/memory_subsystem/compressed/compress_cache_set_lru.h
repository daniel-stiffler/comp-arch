#ifndef __COMPRESS_CACHE_SET_LRU_H__
#define __COMPRESS_CACHE_SET_LRU_H__

#include "compress_cache_set.h"

#include "cache_set.h"
#include "cache_set_lru.h"
#include "fixed_types.h"

class CompressCacheSetLRU : public CompressCacheSet {
 public:
  CompressCacheSetLRU(CacheBase::cache_t cache_type, UInt32 associativity,
                      UInt32 blocksize, CacheSetInfoLRU* set_info,
                      UInt8 num_attempts);
  virtual ~CompressCacheSetLRU();

  virtual UInt32 getReplacementIndex(CacheCntlr* cntlr);
  void updateReplacementIndex(UInt32 accessed_way);

 protected:
  const UInt8 m_num_attempts;
  std::vector<UInt8> m_lru_priorities;
  CacheSetInfoLRU* m_set_info;

  void moveToMRU(UInt32 accessed_way);
};

#endif /* _COMPRESS_CACHE_SET_LRU_H__ */
