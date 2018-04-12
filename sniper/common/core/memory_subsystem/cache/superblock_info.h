#ifndef __SUPERBLOCK_INFO_H__
#define __SUPERBLOCK_INFO_H__

#include <array>
#include <cassert>

#include "cache_block_info.h"
#include "compress_utils.h"

class SuperblockInfo {
 private:
  std::array<CacheBlockInfoUPtr, SUPERBLOCK_SIZE> m_block_infos;
  IntPtr m_supertag;

 public:
  SuperblockInfo(IntPtr supertag = TAG_UNUSED);
  virtual ~SuperblockInfo();

  CacheBlockInfo* peekBlock(UInt32 block_id) const {
    assert(block_id < SUPERBLOCK_SIZE);

    CacheBlockInfo* block_info = m_block_infos[block_id].get();

    if (block_info->isValid()) {
      return block_info;
    } else {
      return nullptr;
    }
  }

  bool canInsertBlockInfo(const CacheBlockInfo* ins_block_info,
                          UInt32* block_id) const;

  bool isValid() const {
    for (const auto& e : m_block_infos) {
      if (e->isValid()) return true;
    }

    return false;
  }
  bool isValid(UInt32 block_id) const {
    assert(block_id < SUPERBLOCK_SIZE);

    return m_block_infos[block_id]->isValid();
  }

  void swapBlockInfo(UInt32 block_id, CacheBlockInfoUPtr& inout_block_info);
  CacheBlockInfoUPtr evictBlockInfo(UInt32 block_id);
  void insertBlockInfo(UInt32 block_id, CacheBlockInfoUPtr ins_block_info);

  bool compareTags(UInt32 tag, UInt32* block_id = nullptr) const;

  bool isValidReplacement() const;
  bool invalidate(UInt32 tag);
};

#endif /* __SUPERBLOCK_INFO_H__ */
