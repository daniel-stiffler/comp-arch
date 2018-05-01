#include "superblock_info.h"

#include <sstream>
#include <utility>

#include "log.h"

SuperblockInfo::SuperblockInfo() : m_supertag(TAG_UNUSED) {}

SuperblockInfo::~SuperblockInfo() {
  // RAII takes care of destructing everything for us
}

CacheBlockInfo* SuperblockInfo::peekBlock(UInt32 block_id) const {
  return m_block_infos[block_id].get();
}

bool SuperblockInfo::canInsertBlockInfo(
    IntPtr supertag, UInt32 block_id,
    const CacheBlockInfo* ins_block_info) const {

  (void)ins_block_info;  // TODO

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
    if (e.get() != nullptr && e->isValid()) return true;
  }

  return false;
}

bool SuperblockInfo::isValid(UInt32 block_id) const {
  const CacheBlockInfo* tmp_block_info = m_block_infos[block_id].get();

  return (tmp_block_info != nullptr && tmp_block_info->isValid());
}

void SuperblockInfo::swapBlockInfo(UInt32 block_id,
                                   CacheBlockInfoUPtr& inout_block_info) {

  m_block_infos[block_id].swap(inout_block_info);
}

CacheBlockInfoUPtr SuperblockInfo::evictBlockInfo(UInt32 block_id) {
  CacheBlockInfoUPtr evict_block = std::move(m_block_infos[block_id]);

  if (!isValid()) m_supertag = TAG_UNUSED;

  return evict_block;
}

void SuperblockInfo::insertBlockInfo(IntPtr supertag, UInt32 block_id,
                                     CacheBlockInfoUPtr ins_block_info) {

  if (!isValid()) m_supertag = supertag;

  m_block_infos[block_id] = std::move(ins_block_info);
}

bool SuperblockInfo::compareTags(IntPtr tag, UInt32* block_id) const {
  for (UInt32 i = 0; i < SUPERBLOCK_SIZE; ++i) {
    const CacheBlockInfo* block_info = m_block_infos[i].get();

    if (block_info != nullptr && block_info->isValid() &&
        tag == block_info->getTag()) {

      if (block_id != nullptr) *block_id = i;

      return true;
    }
  }

  return false;
}

bool SuperblockInfo::isValidReplacement() const {
  for (UInt32 i = 0; i < SUPERBLOCK_SIZE; ++i) {
    const CacheBlockInfo* block_info = m_block_infos[i].get();

    if (block_info != nullptr && block_info->isValid() &&
        block_info->getCState() == CacheState::SHARED_UPGRADING) {

      return false;
    }
  }

  return true;
}

void SuperblockInfo::invalidateBlockInfo(IntPtr tag, UInt32 block_id) {

  CacheBlockInfo* inv_block_info = m_block_infos[block_id].get();

  LOG_ASSERT_WARNING(
      inv_block_info != nullptr && inv_block_info->isValid(),
      "SuperblockInfo attempting invalidation on already invalid block %p", tag,
      block_id, inv_block_info);

  IntPtr inv_tag = inv_block_info->getTag();
  LOG_ASSERT_ERROR(
      tag == inv_tag,
      "SuperblockInfo attempting invalidation but tags did not match (%lx %lx)",
      tag, inv_tag);

  inv_block_info->invalidate();

  /* TODO: SNIPER simulator race conditions when two threads attempt to invalidate at the same time */
  /*
  LOG_PRINT("SuperblockInfo(%p) invalidating CacheBlockInfo tag: %lx block_id: %u ptr: %p",
            this, tag, block_id, inv_block_info);
  */

  if (!isValid()) m_supertag = TAG_UNUSED;
}

std::string SuperblockInfo::dump() const {
  std::stringstream info_ss;

  info_ss << "SuperblockInfo(" << reinterpret_cast<void*>(m_supertag)
          << " valid: " << isValid() << ")";

  info_ss << "->m_block_infos{ ";
  for (UInt32 i = 0; i < SUPERBLOCK_SIZE; ++i) {
    const CacheBlockInfo* tmp_block_info = m_block_infos[i].get();

    IntPtr tmp_tag;
    if (tmp_block_info != nullptr) tmp_tag = tmp_block_info->getTag();
    else tmp_tag = TAG_UNUSED;

    bool tmp_valid;
    if (tmp_block_info != nullptr) tmp_valid = tmp_block_info->isValid();
    else tmp_valid = false;

    info_ss << "(" << tmp_block_info
            << " tag: " << reinterpret_cast<void*>(tmp_tag)
            << " valid: " << tmp_valid;

    info_ss << ") ";
  }

  info_ss << "}";

  return info_ss.str();
}
