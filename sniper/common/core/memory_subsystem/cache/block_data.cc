#include <algorithm>
#include <cassert>

#include "block_data.h"
#include "fixed_types.h"
#include "log.h"

bool BlockData::lookupDictEntry(UInt32 value, UInt8* ptr) {
  for (const auto& entry : m_dict) {
    if (entry.second == value) {
      if (ptr != nullptr) *ptr = entry.first;
      return true;
    }
  }

  return false;
}

UInt8 BlockData::insertDictEntry(UInt32 value) {
  UInt8 ptr;

  if (lookupDictEntry(value, &ptr)) {
    // Dictionary already contained entry mapping to value
    return ptr;
  } else {
    // Ensure that the dictionary contains free entries
    if (m_free_ptrs.empty()) {
      LOG_PRINT_ERROR("Attempted to insert %u into full dictionary", value);
    }

    ptr = *m_free_ptrs.begin();              // Get a new pointer
    m_free_ptrs.erase(m_free_ptrs.begin());  // Remove from free list
    m_dict.insert({ptr, value});             // Add dictionary entry
    m_used_ptrs.insert(ptr);                 // Add to used list

    return ptr;
  }
}

void BlockData::removeDictEntry(UInt8 ptr) {
  if (m_used_ptrs.find(ptr) == m_used_ptrs.end()) {
    LOG_PRINT_ERROR("Attempted to remove invalid dict entry at %u", ptr);
    assert(false);
  } else {
    m_used_ptrs.erase(ptr);   // Remove from used list
    m_dict.erase(ptr);        // Remove dictionary entry
    m_free_ptrs.insert(ptr);  // Add to free list
  }
}

bool BlockData::initScheme(DISH::scheme_t new_scheme) {
  switch (new_scheme) {
    case DISH::scheme_t::UNCOMPRESSED:
      break;

    case DISH::scheme_t::SCHEME1:
      // Add all pointers to free list
      for (UInt8 i = 0; i < DISH::SCHEME1_DICT_SIZE; ++i) {
        m_free_ptrs.insert(i);
      }
      break;

    case DISH::scheme_t::SCHEME2:
      // Add all pointers to free list
      for (UInt8 i = 0; i < DISH::SCHEME2_DICT_SIZE; ++i) {
        m_free_ptrs.insert(i);
      }
      break;
  }

  return true;
}

BlockData::BlockData(UInt32 blocksize)
    : m_blocksize{blocksize},
      m_scheme{DISH::scheme_t::UNCOMPRESSED},
      m_valid{false},
      m_data{{0}},
      m_dict(DISH::SCHEME1_DICT_SIZE),       // Maximum number of buckets
      m_free_ptrs(DISH::SCHEME1_DICT_SIZE),  // Maximum number of buckets
      m_used_ptrs(DISH::SCHEME1_DICT_SIZE),  // Maximum number of buckets
      m_data_ptrs{{0}},
      m_data_offsets{{0}} {
  LOG_ASSERT_ERROR(blocksize == BLOCKSIZE_BYTES,
                   "DISH compressed cache must use a blocksize of %u",
                   BLOCKSIZE_BYTES);

  initScheme(m_scheme);
}

BlockData::~BlockData() {}

bool BlockData::isScheme1Compressible(UInt32 block_id, UInt32 offset,
                                      const Byte* wr_data, UInt32 bytes) {

  assert(isValid());
  assert((wr_data != nullptr) || (wr_data == nullptr && bytes == 0));

  if (m_scheme == DISH::scheme_t::SCHEME1) {
    if (wr_data != nullptr && bytes != 0) {
      // Cast to a 4-word type, which has the same granularity of cache blocks
      // in DISH
      const UInt32* wr_data_chunks = reinterpret_cast<const UInt32*>(wr_data);
      UInt32 start_chunk           = offset / DISH::GRANULARITY_BYTES;
      UInt32 end_chunk = (offset + bytes + DISH::GRANULARITY_BYTES - 1) /
                         DISH::GRANULARITY_BYTES;

      UInt32 tmp_vacancies = m_free_ptrs.size();
      for (UInt32 i = start_chunk; i < end_chunk; ++i) {
        if (!lookupDictEntry(wr_data_chunks[i])) {
          if (tmp_vacancies == 0) {
            return false;  // Early stopping condition
          } else {
            --tmp_vacancies;
          }
        }
      }
    }

    return true;
  } else if (m_scheme == DISH::scheme_t::SCHEME2) {
    // Do not convert between compression schemes on-the-fly
    return false;
  } else if (m_scheme == DISH::scheme_t::UNCOMPRESSED) {
    // Need to check compression with the currently uncompressed line
    std::unordered_set<UInt32> unique_chunks;

    // Find the uncompressed block_id.  Guaranteed to be valid due to
    // pre-condition of function
    UInt32 uncompressed_block_id = getFirstValid();
    assert(uncompressed_block_id != SUPERBLOCK_SIZE);  // Not necessary

    const UInt32* data_uncompressed_chunks =
        reinterpret_cast<const UInt32*>(m_data[uncompressed_block_id]);

    // Add all chunks from currently uncompressed line to set
    for (UInt32 i = 0; i < DISH::BLOCK_ENTRIES; ++i) {
      unique_chunks.insert(data_uncompressed_chunks[i]);
    }

    if (wr_data != nullptr && bytes != 0) {
      // Cast to a 4-word type, which has the same granularity of cache blocks
      // in DISH
      const UInt32* wr_data_chunks = reinterpret_cast<const UInt32*>(wr_data);
      UInt32 start_chunk           = offset / DISH::GRANULARITY_BYTES;
      UInt32 end_chunk = (offset + bytes + DISH::GRANULARITY_BYTES - 1) /
                         DISH::GRANULARITY_BYTES;

      // Add all chunks from wr_data_chunks line to set
      for (UInt32 i = start_chunk; i < end_chunk; ++i) {
        unique_chunks.insert(wr_data_chunks[i]);
      }
    }

    return unique_chunks.size() <= DISH::SCHEME1_DICT_SIZE;
  }

  return false;
}

bool BlockData::isScheme2Compressible(UInt32 block_id, UInt32 offset,
                                      const Byte* wr_data, UInt32 bytes) {

  assert(isValid());
  assert((wr_data != nullptr) || (wr_data == nullptr && bytes == 0));

  if (m_scheme == DISH::scheme_t::SCHEME1) {
    // Do not convert between compression schemes on-the-fly
    return false;
  } else if (m_scheme == DISH::scheme_t::SCHEME2) {
    if (wr_data != nullptr && bytes != 0) {
      // Cast to a 4-word type, which has the same granularity of cache blocks
      // in DISH
      const UInt32* wr_data_chunks = reinterpret_cast<const UInt32*>(wr_data);
      UInt32 start_chunk           = offset / DISH::GRANULARITY_BYTES;
      UInt32 end_chunk = (offset + bytes + DISH::GRANULARITY_BYTES - 1) /
                         DISH::GRANULARITY_BYTES;

      UInt32 tmp_vacancies = m_free_ptrs.size();
      for (UInt32 i = start_chunk; i < end_chunk; ++i) {
        if (!lookupDictEntry(wr_data_chunks[i] >> 4)) {
          if (tmp_vacancies == 0) {
            return false;  // Early stopping condition
          } else {
            --tmp_vacancies;
          }
        }
      }
    }

    return true;
  } else if (m_scheme == DISH::scheme_t::UNCOMPRESSED) {
    // Need to check compression with the currently uncompressed line
    std::unordered_set<UInt32> unique_chunks;

    // Find the uncompressed block_id.  Guaranteed to be valid due to
    // pre-condition of function
    UInt32 uncompressed_block_id = getFirstValid();
    assert(uncompressed_block_id != SUPERBLOCK_SIZE);  // Not necessary

    const UInt32* data_uncompressed_chunks =
        reinterpret_cast<const UInt32*>(m_data[uncompressed_block_id]);

    // Add all chunks from currently uncompressed line to set
    for (UInt32 i = 0; i < DISH::BLOCK_ENTRIES; ++i) {
      unique_chunks.insert(data_uncompressed_chunks[i] >> 4);
    }

    if (wr_data != nullptr && bytes != 0) {
      // Cast to a 4-word type, which has the same granularity of cache blocks
      // in DISH
      const UInt32* wr_data_chunks = reinterpret_cast<const UInt32*>(wr_data);
      UInt32 start_chunk           = offset / DISH::GRANULARITY_BYTES;
      UInt32 end_chunk = (offset + bytes + DISH::GRANULARITY_BYTES - 1) /
                         DISH::GRANULARITY_BYTES;

      // Add all chunks from wr_data_chunks line to set
      for (UInt32 i = start_chunk; i < end_chunk; ++i) {
        unique_chunks.insert(wr_data_chunks[i] >> 4);
      }
    }

    return unique_chunks.size() <= DISH::SCHEME2_DICT_SIZE;
  }

  return false;
}

bool BlockData::isCompressible(UInt32 block_id, UInt32 offset,
                               const Byte* wr_data, UInt32 bytes,
                               DISH::scheme_t try_scheme) {

  // NOTE: DISH algorithm does not specify that the dictionary is be recomputed
  // each time, so check compression based on previous entries
  assert(offset + bytes <= m_blocksize);
  assert(block_id < SUPERBLOCK_SIZE);

  if (!isValid()) {
    // Superblock is empty and no compression is needed
    return true;
  } else {
    // Line is already in the compressed format
    switch (try_scheme) {
      case DISH::scheme_t::SCHEME1:
        return isScheme1Compressible(block_id, offset, wr_data, bytes);

      case DISH::scheme_t::SCHEME2:
        return isScheme2Compressible(block_id, offset, wr_data, bytes);

      case DISH::scheme_t::UNCOMPRESSED:
        return m_valid[block_id];

      default:
        LOG_PRINT_ERROR("Cannot compress with invalid scheme");
        assert(false);
    }
  }

  assert(false);
}

void BlockData::compactScheme1() {
  for (auto entry_it = m_dict.begin(); entry_it != m_dict.end();) {
    bool entry_used = false;
    for (UInt32 block_id = 0; block_id < SUPERBLOCK_SIZE; ++block_id) {
      if (entry_used) break;  // Early stopping condition

      if (!m_valid[block_id]) {
        continue;  // Do not search invalid blocks for value
      }

      const UInt32* data_chunks =
          reinterpret_cast<const UInt32*>(&m_data[block_id][0]);

      for (UInt32 i = 0; i < DISH::BLOCK_ENTRIES; ++i) {
        if (entry_it->second == data_chunks[i]) {
          entry_used = true;
          break;
        }
      }
    }

    if (!entry_used) {
      UInt8 ptr = entry_it->first;

      // Must use erase-remove idiom here, not removeDictEntry
      m_used_ptrs.erase(ptr);             // Remove from used list
      entry_it = m_dict.erase(entry_it);  // Remove the dictionary entry
      m_free_ptrs.insert(ptr);            // Add to free list
    } else {
      ++entry_it;
    }
  }
}

void BlockData::compactScheme2() {
  for (auto entry_it = m_dict.begin(); entry_it != m_dict.end();) {
    bool entry_used = false;
    for (UInt32 block_id = 0; block_id < SUPERBLOCK_SIZE; ++block_id) {
      if (entry_used) break;  // Early stopping condition

      if (!m_valid[block_id]) {
        continue;  // Do not search invalid blocks for value
      }

      const UInt32* data_chunks =
          reinterpret_cast<const UInt32*>(&m_data[block_id][0]);

      for (UInt32 i = 0; i < DISH::BLOCK_ENTRIES; ++i) {
        if (entry_it->second == data_chunks[i]) {
          entry_used = true;
          break;
        }
      }
    }

    if (!entry_used) {
      UInt8 ptr = entry_it->first;

      // Must use erase-remove idiom here, not removeDictEntry
      m_used_ptrs.erase(ptr);             // Remove from used list
      entry_it = m_dict.erase(entry_it);  // Remove the dictionary entry
      m_free_ptrs.insert(ptr);            // Add to free list
    } else {
      ++entry_it;
    }
  }
}

void BlockData::compact() {
  if (!isValid()) return;

  switch (m_scheme) {
    case DISH::scheme_t::SCHEME1:
      compactScheme1();
      break;

    case DISH::scheme_t::SCHEME2:
      compactScheme2();
      break;

    case DISH::scheme_t::UNCOMPRESSED:
      // No compaction is necessary
      break;
  }
}

void BlockData::compressScheme1(UInt32 block_id, UInt32 offset,
                                const Byte* wr_data, UInt32 bytes) {

  assert((wr_data != nullptr) || (wr_data == nullptr && bytes == 0));

  LOG_ASSERT_ERROR(
      isCompressible(block_id, offset, wr_data, bytes),
      "Invalid attempt to compress %u bytes from offset %u into block %u",
      bytes, offset, block_id);

  // Handle trivial case explicitly
  if (wr_data == nullptr || bytes == 0) return;

  if (!isValid()) {
    initScheme(DISH::scheme_t::SCHEME1);

    // Cast to a 4-word type, which has the same granularity of cache blocks in
    // DISH
    const UInt32* wr_data_chunks = reinterpret_cast<const UInt32*>(wr_data);
    UInt32 start_chunk           = offset / DISH::GRANULARITY_BYTES;
    UInt32 end_chunk = (offset + bytes + DISH::GRANULARITY_BYTES - 1) /
                       DISH::GRANULARITY_BYTES;

    for (UInt32 i = start_chunk; i < end_chunk; ++i) {
      m_data_ptrs[block_id][i] = insertDictEntry(wr_data_chunks[i]);
    }

    // Copy raw data into the uncompressed array for fast access
    std::copy_n(wr_data, bytes, &m_data[block_id][offset]);
  } else if (m_scheme == DISH::scheme_t::SCHEME1) {
    // Cast to a 4-word type, which has the same granularity of cache blocks in
    // DISH
    const UInt32* wr_data_chunks = reinterpret_cast<const UInt32*>(wr_data);
    UInt32 start_chunk           = offset / DISH::GRANULARITY_BYTES;
    UInt32 end_chunk = (offset + bytes + DISH::GRANULARITY_BYTES - 1) /
                       DISH::GRANULARITY_BYTES;

    for (UInt32 i = start_chunk; i < end_chunk; ++i) {
      m_data_ptrs[block_id][i] = insertDictEntry(wr_data_chunks[i]);
    }

    // Copy raw data into the uncompressed array for fast access
    std::copy_n(wr_data, bytes, &m_data[block_id][offset]);
  } else if (m_scheme == DISH::scheme_t::UNCOMPRESSED) {
    // Find the uncompressed block_id.  Guaranteed to be valid due to
    // pre-condition of function
    UInt32 uncompressed_block_id = getFirstValid();
    assert(uncompressed_block_id != SUPERBLOCK_SIZE);  // Not necessary

    const UInt32* data_uncompressed_chunks =
        reinterpret_cast<const UInt32*>(&m_data[uncompressed_block_id][0]);

    for (UInt32 i = 0; i < DISH::BLOCK_ENTRIES; ++i) {
      m_data_ptrs[uncompressed_block_id][i] =
          insertDictEntry(data_uncompressed_chunks[i]);
    }

    // Cast to a 4-word type, which has the same granularity of cache blocks in
    // DISH
    const UInt32* wr_data_chunks = reinterpret_cast<const UInt32*>(wr_data);
    UInt32 start_chunk           = offset / DISH::GRANULARITY_BYTES;
    UInt32 end_chunk = (offset + bytes + DISH::GRANULARITY_BYTES - 1) /
                       DISH::GRANULARITY_BYTES;

    for (UInt32 i = start_chunk; i < end_chunk; ++i) {
      m_data_ptrs[block_id][i] = insertDictEntry(wr_data_chunks[i]);
    }

    // Copy raw data into the uncompressed array for fast access
    std::copy_n(wr_data, bytes, &m_data[block_id][offset]);
  } else if (m_scheme == DISH::scheme_t::SCHEME2) {
    LOG_PRINT_ERROR("Invalid attempt to change compression scheme on-the-fly");
  } else {
    assert(false);
  }
}

void BlockData::compressScheme2(UInt32 block_id, UInt32 offset,
                                const Byte* wr_data, UInt32 bytes) {

  assert((wr_data != nullptr) || (wr_data == nullptr && bytes == 0));

  LOG_ASSERT_ERROR(
      isCompressible(block_id, offset, wr_data, bytes),
      "Invalid attempt to compress %u bytes from offset %u into block %u",
      bytes, offset, block_id);

  // Handle trivial case explicitly
  if (wr_data == nullptr || bytes == 0) return;

  if (!isValid()) {
    initScheme(DISH::scheme_t::SCHEME2);

    // Cast to a 4-word type, which has the same granularity of cache blocks in
    // DISH
    const UInt32* wr_data_chunks = reinterpret_cast<const UInt32*>(wr_data);
    UInt32 start_chunk           = offset / DISH::GRANULARITY_BYTES;
    UInt32 end_chunk = (offset + bytes + DISH::GRANULARITY_BYTES - 1) /
                       DISH::GRANULARITY_BYTES;

    for (UInt32 i = start_chunk; i < end_chunk; ++i) {
      m_data_ptrs[block_id][i]    = insertDictEntry(wr_data_chunks[i] >> 4);
      m_data_offsets[block_id][i] = wr_data_chunks[i] & DISH::SCHEME2_MASK;
    }

    // Copy raw data into the uncompressed array for fast access
    std::copy_n(wr_data, bytes, &m_data[block_id][offset]);
  } else if (m_scheme == DISH::scheme_t::SCHEME1) {
    LOG_PRINT_ERROR("Invalid attempt to change compression scheme on-the-fly");
  } else if (m_scheme == DISH::scheme_t::SCHEME2) {
    // Cast to a 4-word type, which has the same granularity of cache blocks in
    // DISH
    const UInt32* wr_data_chunks = reinterpret_cast<const UInt32*>(wr_data);
    UInt32 start_chunk           = offset / DISH::GRANULARITY_BYTES;
    UInt32 end_chunk = (offset + bytes + DISH::GRANULARITY_BYTES - 1) /
                       DISH::GRANULARITY_BYTES;

    for (UInt32 i = start_chunk; i < end_chunk; ++i) {
      m_data_ptrs[block_id][i]    = insertDictEntry(wr_data_chunks[i] >> 4);
      m_data_offsets[block_id][i] = wr_data_chunks[i] & DISH::SCHEME2_MASK;
    }
  } else if (m_scheme == DISH::scheme_t::UNCOMPRESSED) {
    // Find the uncompressed block_id.  Guaranteed to be valid due to
    // pre-condition of function
    UInt32 uncompressed_block_id = getFirstValid();
    assert(uncompressed_block_id != SUPERBLOCK_SIZE);  // Not necessary

    const UInt32* data_uncompressed_chunks =
        reinterpret_cast<const UInt32*>(&m_data[uncompressed_block_id][0]);

    for (UInt32 i = 0; i < DISH::BLOCK_ENTRIES; ++i) {
      m_data_ptrs[uncompressed_block_id][i] =
          insertDictEntry(data_uncompressed_chunks[i] >> 4);
      m_data_offsets[uncompressed_block_id][i] =
          data_uncompressed_chunks[i] & DISH::SCHEME2_MASK;
    }

    // Cast to a 4-word type, which has the same granularity of cache blocks in
    // DISH
    const UInt32* wr_data_chunks = reinterpret_cast<const UInt32*>(wr_data);
    UInt32 start_chunk           = offset / DISH::GRANULARITY_BYTES;
    UInt32 end_chunk = (offset + bytes + DISH::GRANULARITY_BYTES - 1) /
                       DISH::GRANULARITY_BYTES;

    for (UInt32 i = start_chunk; i < end_chunk; ++i) {
      m_data_ptrs[block_id][i]    = insertDictEntry(wr_data_chunks[i] >> 4);
      m_data_offsets[block_id][i] = wr_data_chunks[i] & DISH::SCHEME2_MASK;
    }

    // Copy raw data into the uncompressed array for fast access
    std::copy_n(wr_data, bytes, &m_data[block_id][offset]);
  } else {
    assert(false);
  }
}

void BlockData::compress(UInt32 block_id, UInt32 offset, const Byte* wr_data,
                         UInt32 bytes) {

  assert(offset + bytes <= m_blocksize);
  assert(block_id < SUPERBLOCK_SIZE);

  if (!isValid()) {
    // Current superblock is empty, so just copy new data into the buffer
    std::copy_n(wr_data, bytes, &m_data[block_id][offset]);
  } else if (m_scheme == DISH::scheme_t::UNCOMPRESSED) {
    if (m_valid[block_id]) {
      // Current superblock contains the line in the uncompressed scheme
      std::copy_n(wr_data, bytes, &m_data[block_id][offset]);
    } else {
      // TODO: arbitrate between scheme 1 and 2 compression on the new block
      compressScheme1(block_id, offset, wr_data, bytes);
    }
  } else if (m_scheme == DISH::scheme_t::SCHEME1) {
    compressScheme1(block_id, offset, wr_data, bytes);
  } else if (m_scheme == DISH::scheme_t::SCHEME2) {
    compressScheme2(block_id, offset, wr_data, bytes);
  } else {
    assert(false);
  }
}

void BlockData::decompress(UInt32 block_id, UInt32 offset, UInt32 bytes,
                           Byte* rd_data) const {

  assert(offset + bytes < BLOCKSIZE_BYTES);
  assert(block_id < SUPERBLOCK_SIZE);
  assert(rd_data != nullptr);

  LOG_ASSERT_ERROR(m_valid[block_id],
                   "Attempted to decompress an invalid block %u", block_id);

  std::copy_n(&m_data[block_id][offset], bytes, rd_data);
}

void BlockData::evictBlockData(UInt32 block_id, Byte* evict_data) {
  assert(block_id < SUPERBLOCK_SIZE);
  assert(evict_data != nullptr);

  LOG_ASSERT_ERROR(m_valid[block_id], "Attempted to evict an invalid block %u",
                   block_id);

  decompress(block_id, 0, BLOCKSIZE_BYTES, evict_data);
  m_valid[block_id] = false;
  std::fill_n(&m_data[block_id][0], BLOCKSIZE_BYTES, 0);

  // Check to see if this was the last block in the superblock.  If it was,
  // mark it as uncompressed for future operations
  if (!isValid()) {
    initScheme(DISH::scheme_t::UNCOMPRESSED);
  }
}

void BlockData::insertBlockData(UInt32 block_id, const Byte* wr_data) {
  assert(block_id < SUPERBLOCK_SIZE);
  assert(wr_data != nullptr);

  LOG_ASSERT_ERROR(!m_valid[block_id],
                   "Attempted to insert block %u on top of an existing one",
                   block_id);

  compress(block_id, 0, wr_data, BLOCKSIZE_BYTES);
  m_valid[block_id] = true;
}
