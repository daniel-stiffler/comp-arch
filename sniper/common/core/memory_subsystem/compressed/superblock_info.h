#ifndef __SUPERBLOCK_INFO_H__
#define __SUPERBLOCK_INFO_H__

#include <array>
#include <cassert>

#include "cache_block_info.h"
#include "dish_utils.h"

using DISH::CacheBlockInfoUPtr;

class SuperBlockInfo {
 private:
  std::array<CacheBlockInfoUPtr, DISH::SUPERBLOCK_SIZE> m_block_infos;
  bool m_valid[4];
  IntPtr m_super_tag;

 public:
  SuperBlockInfo(IntPtr super_tag = DISH::TAG_UNUSED);

  CacheBlockInfo* getBlockInfo(UInt32 block_id) const {
    assert(block_id < DISH::SUPERBLOCK_SIZE);

    if (m_valid[block_id])
      return m_block_infos[block_id].get();
    else
      return nullptr;
  }

  bool canInsertBlockInfo(const CacheBlockInfo* ins_block_info,
                          UInt32* block_id) const;

  bool isValid() const {
    for (UInt32 i = 0; i < DISH::SUPERBLOCK_SIZE; ++i) {
      if (m_valid[i]) return true;
    }

    return false;
  }
  bool isValid(UInt32 block_id) const {
    assert(block_id < DISH::SUPERBLOCK_SIZE);

    return m_valid[block_id];
  }

  void swapBlockInfo(UInt32 block_id, CacheBlockInfoUPtr& inout_block_info);
  CacheBlockInfoUPtr evictBlockInfo(UInt32 block_id);
  void insertBlockInfo(UInt32 block_id, CacheBlockInfoUPtr ins_block_info);

  bool compareTags(UInt32 tag, UInt32* block_id = nullptr) const;

  bool invalidate(UInt32 tag);
};

#endif /* __SUPERBLOCK_INFO_H__ */
