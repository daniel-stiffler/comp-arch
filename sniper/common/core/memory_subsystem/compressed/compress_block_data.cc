#include <algorithm>
#include <cassert>

#include "compress_block_data.h"
#include "fixed_types.h"
#include "log.h"

bool lookupDictEntry(UInt32 value, UInt8* ptr) {
  for (const auto& entry : m_dict) {
    if (entry->second == value) {
      if (ptr != nullptr) ptr = entry->first;
      return true;
    }
  }

  return false;
}

UInt8 CompressBlockData::insertDictEntry(UInt32 value) {
  UInt8 ptr;

  if (lookupDictEntry(value, &ptr)) {
    // Dictionary already contained entry mapping to value
    return ptr;
  } else {
    // Ensure that the dictionary contains free entries
    if (m_free_ptrs.empty()) {
      LOG_PRINT_ERROR("Attempted to insert %u into full dictionary", value);
      assert(false);
    }

    ptr = *m_free_ptrs.begin();              // Get a new pointer
    m_free_ptrs.erase(m_free_ptrs.begin());  // Remove from free list
    m_dict.insert({ptr, value});             // Add dictionary entry
    m_used_ptrs.insert(ptr);                 // Add to used list

    return ptr;
  }
}

void CompressBlockData::removeDictEntry(UInt8 ptr) {
  if (m_used_ptrs.find(ptr) == m_used_ptrs.end()) {
    LOG_PRINT_ERROR("Attempted to remove invalid dict entry at %u", ptr);
    assert(false);
  } else {
    m_used_ptrs.erase(ptr);   // Remove from used list
    m_dict.erase(ptr);        // Remove dictionary entry
    m_free_ptrs.insert(ptr);  // Add to free list
  }
}

CompressBlockData::CompressBlockData(UInt32 blocksize)
    : m_blocksize{blocksize},
      compressed{false},
      m_valid{false},
      m_data_uncompressed{0},
      m_dict(MAX_DISH_ENTRIES),
      m_free_ptrs(MAX_DISH_ENTRIES),
      m_used_ptrs(MAX_DISH_ENTRIES),
      m_data_uncompressed{0} {
  LOG_ASSERT_ERROR(blocksize == DISH_BLOCKSIZE_BYTES,
                   "DISH compressed cache must use a blocksize of %u",
                   DISH_BLOCKSIZE_BYTES);

  // Add all pointers to free list
  for (UInt8 i = 0; i < MAX_DISH_ENTRIES; ++i) {
    m_free_ptrs.insert(i);
  }
}

CompressBlockData::~CompressBlockData() {}

bool CompressBlockData::isCompressible(UInt32 block_id, UInt32 offset,
                                       const Byte* wr_data, UInt32 bytes) {

  assert(offset + bytes <= m_blocksize);
  assert(block_id < MAX_SUPERBLOCK_SIZE);
  assert(wr_data != nullptr);

  if (!isValid()) {
    return true;  // Superblocks is empty and no compression needed
  } else {
    if (compressed) {
      // Line is already in the compressed format
      //   NOTE: DISH algorithm does not specify that the dictionary is be
      //   recomputed each time, so check compression based on previous entries

      // Cast to a 4-word type, which has the same granularity of cache blocks
      // in DISH
      const UInt32* wr_data_chunks = static_cast<UInt32*>(wr_data);
      UInt32 start_chunk           = offset / DISH_GRANULARITY_BYTES;
      UInt32 end_chunk = (offset + bytes + DISH_GRANULARITY_BYTES - 1) /
                         DISH_GRANULARITY_BYTES;

      UInt32 tmp_vacancies = m_free_ptrs.size();
      for (UInt32 i = start_chunk; i < end_chunk; ++i) {
        if (!lookupDictEntry(wr_data_chunks[i]) {
          if (tmp_vacancies == 0) {
            return false;  // Early stopping condition
          } else {
            --tmp_dict_vacancies;
          }
        }
      }

      return true;
    } else {  // Need to check compression with the currently uncompressed line
      std::unordered_set<UInt32> unique_vals;

      const UInt32* data_uncompressed_chunks =
          static_cast<UInt32*>(m_data_uncompressed);

      // Add all chunks from currently uncompressed line to set
      for (UInt32 i = 0; i < DISH_POINTERS; ++i) {
        unique_vals.insert(data_uncompressed_chunks[i]);
      }

      // Cast to a 4-word type, which has the same granularity of cache blocks
      // in DISH
      const UInt32* wr_data_chunks = static_cast<UInt32*>(wr_data);
      UInt32 start_chunk           = offset / DISH_GRANULARITY_BYTES;
      UInt32 end_chunk = (offset + bytes + DISH_GRANULARITY_BYTES - 1) /
                         DISH_GRANULARITY_BYTES;

      // Add all chunks from wr_datauncompressed line to set
      for (UInt32 i = start_chunk; i < end_chunk; ++i) {
        unique_vals.insert(wr_data_chunks[i]);
      }

      return unique_vals.size() <= MAX_DISH_ENTRIES;
    }
  }

  void CompressBlockData::compact() {
    if (!compressed) return;  // No compression needed

    for (auto entry_it = m_dict.begin(); entry_it != m_dict.end();) {
      bool ptr_used = false;
      for (UInt32 block_id = 0; block_id < MAX_SUPERBLOCK_SIZE; ++block_id) {
        if (ptr_used) break;  // Early stopping condition

        if (!m_valid[block_id]) {
          continue;  // Do not search invalid blocks for value
        }

        for (UInt32 i = 0; i < DISH_POINTERS; ++i) {
          if (m_data_compressed[i] == entry_it->second) {
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
}

void CompressBlockData::compress(UInt32 block_id, UInt32 offset,
                                 const Byte* wr_data, UInt32 bytes) {

  assert(offset + bytes <= m_blocksize);
  assert(block_id < MAX_SUPERBLOCK_SIZE);
  assert(wr_data != nullptr);

  // Overwriting an uncompressed block is just copying.  We can do this by
  // overwriting the currently uncompressed block or if no blocks are valid
  if (!compressed && (m_valid[block_id] || !isValid()) {
    std::copy_n(wr_data, bytes, &m_data_uncompressed[offset]);
    return;
  } else {
    LOG_PRINT_ERROR(
        "Invalid attempt to compress new block %u with currently uncompressed "
        "block",
        block_id);
    assert(false);
  }

  LOG_ASSERT_ERROR(
      isCompressible(block_id, offset, wr_data, bytes),
      "Invalid attempt to compress %u bytes from offset %u into block %u",
      bytes, offset, block_id);

  // Overwriting part of a compressed block does not result in recompression,
  // because doing so would be an enormous power burden
  Uint8* m_block_compressed = &m_data_compressed[block_id];

  // Cast to a 4-word type, which has the same granularity of cache blocks in
  // DISH
  const UInt32* wr_data_chunks = static_cast<UInt32*>(wr_data);
  UInt32 start_chunk           = offset / DISH_GRANULARITY_BYTES;
  UInt32 end_chunk =
      (offset + bytes + DISH_GRANULARITY_BYTES - 1) / DISH_GRANULARITY_BYTES;

  for (UInt32 i = start_chunk; i < end_chunk; ++i) {
    m_block_compressed[i] = insertDictEntry(wr_data_chunks[i]);
  }
}

void CompressBlockData::decompress(UInt32 block_id, Byte* rd_data) const {
  assert(block_id < MAX_SUPERBLOCK_SIZE);
  assert(rd_data != nullptr);

  LOG_ASSERT_ERROR(m_valid[block_id],
                   "Attempted to decompress an invalid block %u", block_id);

  if (!compressed) {
    std::copy_n(m_data_uncompressed, DISH_BLOCKSIZE_BYTES, rd_data);
  } else {
    const Uint8* m_block_compressed = &m_data_compressed[block_id];

    // Cast to a 4-word type, which has the same granularity of cache blocks in
    // DISH
    UInt32* rd_data_chunks = static_cast<UInt32*>(rd_data);

    for (UInt32 i = 0; i < DISH_POINTERS; ++i) {
      rd_data_chunks[i] = m_dict.find(m_block_compressed[i])->second;
    }
  }
}

void CompressBlockData::evictBlockData(UInt32 block_id, Byte* evict_data) {
  assert(block_id < MAX_SUPERBLOCK_SIZE);
  assert(evict_data != nullptr);

  LOG_ASSERT_ERROR(m_valid[block_id], "Attempted to evict an invalid block %u",
                   block_id);

  if (!compressed) {
    std::copy_n(m_data_uncompressed, DISH_BLOCKSIZE_BYTES, evict_data);

    m_valid[block_id] = false;
    std::fill_n(m_data_uncompressed, DISH_BLOCKSIZE_BYTES, 0);
  } else {
    decompress(block_id, evict_data);

    m_valid[block_id] = false;
    std::fill_n(&m_data_compressed[block_id], DISH_POINTERS, 0);
    compact();  // Free any unused pointers (possibly all of them)

    // Check to see if this was the last block in the superblock.  If it was,
    // mark it as uncompressed for future operations
    if (!isValid()) compressed = false;
  }
}

void CompressBlockData::insertBlockData(UInt32 block_id, const Byte* wr_data) {
  assert(block_id < MAX_SUPERBLOCK_SIZE);
  assert(wr_data != nullptr);

  LOG_ASSERT_ERROR(!m_valid[block_id],
                   "Attempted to insert block %u on top of an existing one",
                   block_id);

  if (!isValid()) {
    compress(block_id, 0, wr_data, DISH_BLOCKSIZE_BYTES);
    m_valid[block_id] = true;
  } else {
    if (compressed) {
      // If block is already compressed, simply update the new block by
      // compressing it
      compress(block_id, 0, wr_data, DISH_BLOCKSIZE_BYTES);
      m_valid[block_id] = true;
    } else {
      // If there is an uncompressed block, need to compress it first then merge
      // with the new one
      compress(block_id, 0, m_data_uncompressed, DISH_BLOCKSIZE_BYTES);
      compressed = true;
      std::fill_n(m_data_uncompressed, DISH_BLOCKSIZE_BYTES, 0);

      // Merge the new block with the now-compressed existing one
      compress(block_id, 0, wr_data, DISH_BLOCKSIZE_BYTES);
      m_valid[block_id] = true;
    }
  }
}
