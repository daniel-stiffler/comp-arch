#pragma once

#include "cache_state.h"
#include "cache_block_info.h"

class PrL1CacheBlockInfo : public CacheBlockInfo
{
   public:
      PrL1CacheBlockInfo(IntPtr tag = ~0,
            CacheState::cstate_t cstate = CacheState::INVALID):
         CacheBlockInfo(tag, cstate)
      {}

      ~PrL1CacheBlockInfo() {}
};
