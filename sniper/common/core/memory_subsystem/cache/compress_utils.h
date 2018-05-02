#pragma once

#include <cmath>
#include <map>
#include <string>
#include <memory>
#include <vector>

#include "cache_block_info.h"
#include "fixed_types.h"

constexpr IntPtr TAG_UNUSED      = static_cast<IntPtr>(~0);
constexpr UInt32 SUPERBLOCK_SIZE = 4;

namespace DISH {
constexpr UInt32 SCHEME1_DICT_SIZE = 8;
constexpr UInt32 SCHEME2_DICT_SIZE = 4;
constexpr UInt32 GRANULARITY_BYTES = 4;
constexpr UInt32 BLOCK_ENTRIES     = 16;

constexpr UInt32 SCHEME2_OFFSET_BITS = 4;
constexpr UInt32 SCHEME2_OFFSET_MASK = 0xf;

enum class scheme_t { INVALID, UNCOMPRESSED, SCHEME1, SCHEME2 };
const std::map<scheme_t, const char *> scheme2name{
    {scheme_t::INVALID, "INVALID"},
    {scheme_t::UNCOMPRESSED, "UNCOMPRESSED"},
    {scheme_t::SCHEME1, "SCHEME1"},
    {scheme_t::SCHEME2, "SCHEME2"}};

}  // namespace DISH

typedef std::unique_ptr<CacheBlockInfo> CacheBlockInfoUPtr;
typedef std::tuple<IntPtr, CacheBlockInfoUPtr, std::unique_ptr<Byte>>
    WritebackTuple;
typedef std::vector<WritebackTuple> WritebackLines;

// Utility functions
std::string printBytes(const Byte* data, UInt32 size);
std::string printChunks(const UInt32* data, UInt32 size);
