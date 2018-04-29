#pragma once

#include <array>
#include <cassert>

#include "cache_block_info.h"
#include "compress_utils.h"

class SuperblockInfo {
 private:
  std::array<CacheBlockInfoUPtr, SUPERBLOCK_SIZE> m_block_infos;
  IntPtr m_supertag;

 public:
  SuperblockInfo();
  virtual ~SuperblockInfo();

  CacheBlockInfo* peekBlock(UInt32 block_id) const;

  bool canInsertBlockInfo(IntPtr supertag, UInt32 block_id,
                          const CacheBlockInfo* ins_block_info) const;

  bool isValid() const;
  bool isValid(UInt32 block_id) const;

  void swapBlockInfo(UInt32 block_id, CacheBlockInfoUPtr& inout_block_info);
  CacheBlockInfoUPtr evictBlockInfo(UInt32 block_id);
  void insertBlockInfo(IntPtr supertag, UInt32 block_id,
                       CacheBlockInfoUPtr ins_block_info);

  bool compareTags(IntPtr tag, UInt32* block_id = nullptr) const;

  bool isValidReplacement() const;
  bool invalidate(IntPtr tag);
};
