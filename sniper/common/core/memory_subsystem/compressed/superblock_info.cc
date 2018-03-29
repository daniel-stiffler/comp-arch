#include "superblock_info.h"

#include <cassert>
#include <utility>

#include "fixed_types.h"

SuperBlockInfo::SuperBlockInfo(IntPtr super_tag)
    : m_valid{false}, m_super_tag{super_tag} {}

void SuperBlockInfo::swapBlockInfo(
    UInt32 block_id, std::unique_ptr<CacheBlockInfo>& inout_block_info) {

  assert(block_id < MAX_SUPERBLOCK_SIZE);

  m_block_infos[block_id].swap(inout_block_info);
}

std::unique_ptr<CacheBlockInfo> SuperBlockInfo::evictBlockInfo(
    UInt32 block_id) {

  assert(block_id < MAX_SUPERBLOCK_SIZE);

  std::unique_ptr<CacheBlockInfo> evict_block =
      std::move(m_block_infos[block_id]);

  return evict_block;
}

bool SuperBlockInfo::compareTags(UInt32 tag, UInt32* block_id) const {
  assert(block_id < MAX_SUPERBLOCK_SIZE);

  for (UInt32 i = 0; i < MAX_SUPERBLOCK_SIZE; ++i) {
    const CacheBlockInfo& block_info = m_block_infos[i];

    if (m_valid[i] && tag == block_info.getTag()) {
      if (block_id != nullptr) *block_id = i;
      return true;
    }
  }

  return false;
}

bool SuperBlockInfo::invalidate(UInt32 tag) {
  for (UInt32 i = 0; i < MAX_SUPERBLOCK_SIZE; ++i) {
    CacheBlockInfo& block_info = m_block_infos[i];

    if (m_valid[i] && tag == block_info.getTag()) {
      block_info.invalidate();
      return true;
    }
  }

  return false;
}
