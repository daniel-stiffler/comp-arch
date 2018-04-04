#ifndef __DISH_UTILS_H__
#define __DISH_UTILS_H__

#include <memory>

#include "cache_block_info.h"
#include "fixed_types.h"

namespace DISH {
constexpr IntPtr TAG_UNUSED        = static_cast<IntPtr>(~0);
constexpr UInt32 SUPERBLOCK_SIZE   = 4;
constexpr UInt32 SCHEME1_DICT_SIZE = 8;
constexpr UInt32 SCHEME2_DICT_SIZE = 4;
constexpr UInt32 GRANULARITY_BYTES = 4;
constexpr UInt32 BLOCKSIZE_BYTES   = 64;
constexpr UInt32 BLOCK_ENTRIES     = 16;

enum class scheme {UNCOMPRESSED, SCHEME1, SCHEME2};

typedef std::unique_ptr<CacheBlockInfo> CacheBlockInfoUPtr;
}  // namespace DISH

#endif /* __DISH_UTILS_H__ */
