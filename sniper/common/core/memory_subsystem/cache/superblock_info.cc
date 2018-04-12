#include "superblock_info.h"

#include <cassert>
#include <utility>

SuperblockInfo::SuperblockInfo(IntPtr supertag)
    : m_supertag(supertag) {}

SuperblockInfo::~SuperblockInfo() {
  // RAII takes care of destructing everything for us
}

void SuperblockInfo::swapBlockInfo(UInt32 block_id,
                                   CacheBlockInfoUPtr& inout_block_info) {

  assert(block_id < SUPERBLOCK_SIZE);

  m_block_infos[block_id].swap(inout_block_info);
}

CacheBlockInfoUPtr SuperblockInfo::evictBlockInfo(UInt32 block_id) {

  assert(block_id < SUPERBLOCK_SIZE);

  CacheBlockInfoUPtr evict_block = std::move(m_block_infos[block_id]);

  return evict_block;
}

bool SuperblockInfo::compareTags(UInt32 tag, UInt32* block_id) const {
  for (UInt32 i = 0; i < SUPERBLOCK_SIZE; ++i) {
    const CacheBlockInfo* block_info = m_block_infos[i].get();

    if (block_info->isValid() && tag == block_info->getTag()) {
      if (block_id != nullptr) *block_id = i;
      return true;
    }
  }

  return false;
}

bool SuperblockInfo::isValidReplacement() const {
  for (UInt32 i = 0; i < SUPERBLOCK_SIZE; ++i) {
    const CacheBlockInfo* block_info = m_block_infos[i].get();

    if (block_info->isValid() &&
        block_info->getCState() == CacheState::SHARED_UPGRADING) {
      return false;
    }
  }

  return true;
}

bool SuperblockInfo::invalidate(UInt32 tag) {
  for (UInt32 i = 0; i < SUPERBLOCK_SIZE; ++i) {
    CacheBlockInfo* block_info = m_block_infos[i].get();

    if (block_info->isValid() && tag == block_info->getTag()) {
      block_info->invalidate();

      return true;
    }
  }

  return false;
}
