#ifndef __DISH_UTILS_H__
#define __DISH_UTILS_H__

#include <memory>
#include <vector>

#include "cache_block_info.h"
#include "fixed_types.h"

constexpr IntPtr TAG_UNUSED      = static_cast<IntPtr>(~0);
constexpr UInt32 SUPERBLOCK_SIZE = 4;
constexpr UInt32 BLOCKSIZE_BYTES = 64;

namespace DISH {
constexpr UInt32 SCHEME1_DICT_SIZE = 8;
constexpr UInt32 SCHEME2_DICT_SIZE = 4;
constexpr UInt32 GRANULARITY_BYTES = 4;
constexpr UInt32 BLOCK_ENTRIES     = 16;

enum class scheme_t { UNCOMPRESSED, SCHEME1, SCHEME2 };
}  // namespace DISH

typedef std::unique_ptr<CacheBlockInfo> CacheBlockInfoUPtr;
typedef std::tuple<CacheBlockInfoUPtr, std::unique_ptr<Byte>> WritebackTuple;
typedef std::vector<WritebackTuple> WritebackLines;

#endif /* __DISH_UTILS_H__ */
