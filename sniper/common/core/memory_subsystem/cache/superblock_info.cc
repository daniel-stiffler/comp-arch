#include "superblock_info.h"

#include <cassert>
#include <utility>

#include "log.h"

SuperblockInfo::SuperblockInfo() : m_supertag(TAG_UNUSED) {}

SuperblockInfo::~SuperblockInfo() {
  // RAII takes care of destructing everything for us
}

CacheBlockInfo* SuperblockInfo::peekBlock(UInt32 block_id) const {
  assert(block_id < SUPERBLOCK_SIZE);

  return m_block_infos[block_id].get();
}

bool SuperblockInfo::canInsertBlockInfo(
    IntPtr supertag, UInt32 block_id,
    const CacheBlockInfo* ins_block_info) const {

  (void)ins_block_info;  // TODO

  assert(block_id < SUPERBLOCK_SIZE);

  LOG_PRINT(
      "SuperblockInfo checking supertag: %lx block_id: %u ptr: %p valid: "
      "{%d%d%d%d}",
      supertag, block_id, ins_block_info, isValid(0), isValid(1), isValid(2),
      isValid(3));

  if (!isValid()) {
    return true;
  } else if (!isValid(block_id) && supertag == m_supertag) {
    return true;
  } else {
    return false;
  }
}

bool SuperblockInfo::isValid() const {
  for (const auto& e : m_block_infos) {
    if (e.get() != nullptr) return true;
  }

  return false;
}

bool SuperblockInfo::isValid(UInt32 block_id) const {
  assert(block_id < SUPERBLOCK_SIZE);

  return (m_block_infos[block_id].get() != nullptr);
}

void SuperblockInfo::swapBlockInfo(UInt32 block_id,
                                   CacheBlockInfoUPtr& inout_block_info) {

  assert(block_id < SUPERBLOCK_SIZE);

  m_block_infos[block_id].swap(inout_block_info);
}

CacheBlockInfoUPtr SuperblockInfo::evictBlockInfo(UInt32 block_id) {
  LOG_PRINT("SuperblockInfo evicting CacheBlockInfo %p",
            m_block_infos[block_id].get());

  assert(block_id < SUPERBLOCK_SIZE);

  CacheBlockInfoUPtr evict_block = std::move(m_block_infos[block_id]);

  if (!isValid()) m_supertag = TAG_UNUSED;

  return evict_block;
}

void SuperblockInfo::insertBlockInfo(IntPtr supertag, UInt32 block_id,
                                     CacheBlockInfoUPtr ins_block_info) {

  LOG_PRINT(
      "SuperblockInfo inserting CacheBlockInfo supertag: %lx block_id: %u "
      "ins_block_info: %p",
      supertag, block_id, ins_block_info.get());

  assert(canInsertBlockInfo(supertag, block_id, ins_block_info.get()));

  if (!isValid()) {
    m_supertag = supertag;
  }

  m_block_infos[block_id] = std::move(ins_block_info);
}

bool SuperblockInfo::compareTags(IntPtr tag, UInt32* block_id) const {
  for (UInt32 i = 0; i < SUPERBLOCK_SIZE; ++i) {
    const CacheBlockInfo* block_info = m_block_infos[i].get();

    if (block_info != nullptr && tag == block_info->getTag()) {
      if (block_id != nullptr) *block_id = i;

      return true;
    }
  }

  return false;
}

bool SuperblockInfo::isValidReplacement() const {
  for (UInt32 i = 0; i < SUPERBLOCK_SIZE; ++i) {
    const CacheBlockInfo* block_info = m_block_infos[i].get();

    if (block_info != nullptr &&
        block_info->getCState() == CacheState::SHARED_UPGRADING) {
      return false;
    }
  }

  return true;
}

void SuperblockInfo::invalidateBlockInfo(IntPtr tag, UInt32 block_id) {
  CacheBlockInfoUPtr& inv_block_info = m_block_infos[block_id];

  IntPtr inv_tag = inv_block_info->getTag();
  LOG_ASSERT_ERROR(
      tag == inv_tag,
      "SuperblockInfo attempting invalidation but tags did not match %lx %lx",
      tag, inv_tag);

  inv_block_info.reset(nullptr);

  LOG_PRINT("SuperblockInfo invalidating CacheBlockInfo tag: %lx block_id: %u ",
            tag, block_id);
}
