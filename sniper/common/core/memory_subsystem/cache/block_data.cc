#include <algorithm>
#include <cassert>

#include "block_data.h"
#include "cache.h"
#include "fixed_types.h"
#include "log.h"

bool BlockData::lookupDictEntry(UInt32 value, UInt8* ptr) const {
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

void BlockData::changeScheme(DISH::scheme_t new_scheme) {
  if (m_scheme == DISH::scheme_t::UNCOMPRESSED) {
    if (new_scheme == DISH::scheme_t::SCHEME1) {
      // Add all pointers to free list
      for (UInt8 p = 0; p < DISH::SCHEME1_DICT_SIZE; ++p) {
        m_free_ptrs.insert(p);
      }
    } else if (new_scheme == DISH::scheme_t::SCHEME2) {
      // Add all pointers to free list
      for (UInt8 p = 0; p < DISH::SCHEME2_DICT_SIZE; ++p) {
        m_free_ptrs.insert(p);
      }
    }
  } else if (m_scheme == DISH::scheme_t::SCHEME1) {
    if (new_scheme == DISH::scheme_t::UNCOMPRESSED) {
      m_dict.clear();
      m_free_ptrs.clear();
      m_used_ptrs.clear();
    } else if (new_scheme == DISH::scheme_t::SCHEME2) {
      m_dict.clear();
      m_free_ptrs.clear();
      m_used_ptrs.clear();
      // Add all pointers to free list
      for (UInt8 p = 0; p < DISH::SCHEME2_DICT_SIZE; ++p) {
        m_free_ptrs.insert(p);
      }
      //LOG_PRINT_ERROR(
      //    "Invalid attempt to change compression scheme on-the-fly");
    }
  } else if (m_scheme == DISH::scheme_t::SCHEME2) {
    if (new_scheme == DISH::scheme_t::UNCOMPRESSED) {
      m_dict.clear();
      m_free_ptrs.clear();
      m_used_ptrs.clear();
    } else if (new_scheme == DISH::scheme_t::SCHEME1) {
      m_dict.clear();
      m_free_ptrs.clear();
      m_used_ptrs.clear();
      // Add all pointers to free list
      for (UInt8 p = 0; p < DISH::SCHEME1_DICT_SIZE; ++p) {
        m_free_ptrs.insert(p);
      }
      //LOG_PRINT_ERROR(
      //    "Invalid attempt to change compression scheme on-the-fly");
    }
  }
}

UInt32 BlockData::getFirstValid() const {
  for (UInt32 i = 0; i < SUPERBLOCK_SIZE; ++i) {
    if (m_valid[i]) return i;
  }

  // None of the blocks were valid
  return SUPERBLOCK_SIZE;
}

BlockData::BlockData(UInt32 blocksize)
    : m_blocksize{blocksize},
      m_chunks_per_block{blocksize / DISH::GRANULARITY_BYTES},
      m_scheme{DISH::scheme_t::UNCOMPRESSED},
      m_valid{false},
      m_dict(DISH::SCHEME1_DICT_SIZE),       // Maximum number of buckets
      m_free_ptrs(DISH::SCHEME1_DICT_SIZE),  // Maximum number of buckets
      m_used_ptrs(DISH::SCHEME1_DICT_SIZE),  // Maximum number of buckets
      m_data_ptrs{{0}},
      m_data_offsets{{0}} {

  for (auto& e : m_data) {
    e.resize(m_blocksize);
  }

  changeScheme(m_scheme);
}

BlockData::~BlockData() {}

bool BlockData::isScheme1Compressible(
    UInt32 block_id, UInt32 offset, const Byte* wr_data, UInt32 bytes,
    CacheCompressionCntlr* compress_cntlr) const {

  assert((wr_data != nullptr) || (wr_data == nullptr && bytes == 0));

  // Handle trivial cases explicitly
  if (!isValid()) return false;
  if (!compress_cntlr->canCompress()) {
    return false;
  }

  assert((!isValid(block_id) && offset == 0 &&
          (bytes == m_blocksize || bytes == 0)) ||
         isValid(block_id));

  if (m_scheme == DISH::scheme_t::SCHEME1) {
    if (wr_data != nullptr && bytes != 0) {
      std::vector<UInt8> test_data(m_blocksize);

      std::copy_n(&m_data[block_id][0], offset, &test_data[0]);
      std::copy_n(wr_data, bytes, &test_data[offset]);
      std::copy_n(&m_data[block_id][offset + bytes],
                  m_blocksize - (offset + bytes), &test_data[offset + bytes]);

      // Cast to a 4-word type, which has the same granularity of cache blocks
      // in DISH
      const UInt32* test_data_chunks =
          reinterpret_cast<const UInt32*>(&test_data[0]);

      UInt32 tmp_vacancies = m_free_ptrs.size();
      for (UInt32 i = 0; i < m_chunks_per_block; ++i) {
        if (!lookupDictEntry(test_data_chunks[i])) {
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
    if (compress_cntlr->canChangeSchemeOTF()) {
      return false;  // not implemented yet
    } else {
      // Do not convert between compression schemes on-the-fly
      return false;
    }
  } else if (m_scheme == DISH::scheme_t::UNCOMPRESSED) {
    // Need to check compression with the currently uncompressed line
    std::unordered_set<UInt32> unique_chunks;

    // Find the uncompressed block_id.  Guaranteed to be valid due to
    // pre-condition of function
    UInt32 uncompressed_block_id = getFirstValid();

    if (uncompressed_block_id != block_id) {
      const UInt32* data_uncompressed_chunks =
          reinterpret_cast<const UInt32*>(&m_data[uncompressed_block_id][0]);

      // Add all chunks from currently uncompressed line to set
      for (UInt32 i = 0; i < m_chunks_per_block; ++i) {
        unique_chunks.insert(data_uncompressed_chunks[i]);
      }
    }

    if (wr_data != nullptr && bytes != 0) {
      // Cast to a 4-word type, which has the same granularity of cache blocks
      // in DISH
      const UInt32* wr_data_chunks = reinterpret_cast<const UInt32*>(wr_data);

      // Add all chunks from wr_data_chunks line to set
      UInt32 n_chunks = bytes / DISH::GRANULARITY_BYTES;
      for (UInt32 i = 0; i < n_chunks; ++i) {
        unique_chunks.insert(wr_data_chunks[i]);
      }
    }

    return unique_chunks.size() <= DISH::SCHEME1_DICT_SIZE;
  }

  return false;
}

bool BlockData::isScheme2Compressible(
    UInt32 block_id, UInt32 offset, const Byte* wr_data, UInt32 bytes,
    CacheCompressionCntlr* compress_cntlr) const {

  assert((wr_data != nullptr) || (wr_data == nullptr && bytes == 0));

  // Handle trivial cases explicitly
  if (!isValid()) return false;
  if (!compress_cntlr->canCompress()) {
    return false;
  }

  assert((!isValid(block_id) && offset == 0 &&
          (bytes == m_blocksize || bytes == 0)) ||
         isValid(block_id));

  if (m_scheme == DISH::scheme_t::SCHEME1) {
    if (compress_cntlr->canChangeSchemeOTF()) {
      return false;  // not implemented yet
    } else {
      // Do not convert between compression schemes on-the-fly
      return false;
    }
  } else if (m_scheme == DISH::scheme_t::SCHEME2) {
    if (wr_data != nullptr && bytes != 0) {
      std::vector<UInt8> test_data(m_blocksize);

      std::copy_n(&m_data[block_id][0], offset, &test_data[0]);
      std::copy_n(wr_data, bytes, &test_data[offset]);
      std::copy_n(&m_data[block_id][offset + bytes],
                  m_blocksize - (offset + bytes), &test_data[offset + bytes]);

      // Cast to a 4-word type, which has the same granularity of cache blocks
      // in DISH
      const UInt32* test_data_chunks =
          reinterpret_cast<const UInt32*>(&test_data[0]);

      UInt32 tmp_vacancies = m_free_ptrs.size();
      UInt32 n_chunks      = m_blocksize / DISH::GRANULARITY_BYTES;
      for (UInt32 i = 0; i < n_chunks; ++i) {
        UInt32 tmp = test_data_chunks[i] >> DISH::SCHEME2_OFFSET_BITS;

        if (!lookupDictEntry(tmp)) {
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

    if (uncompressed_block_id != block_id) {
      const UInt32* data_uncompressed_chunks =
          reinterpret_cast<const UInt32*>(m_data[uncompressed_block_id][0]);

      // Add all chunks from currently uncompressed line to set
      for (UInt32 i = 0; i < m_chunks_per_block; ++i) {
        unique_chunks.insert(data_uncompressed_chunks[i] >> 4);
      }
    }

    if (wr_data != nullptr && bytes != 0) {
      // Cast to a 4-word type, which has the same granularity of cache blocks
      // in DISH
      const UInt32* wr_data_chunks = reinterpret_cast<const UInt32*>(wr_data);

      // Add all chunks from wr_data_chunks line to set
      UInt32 n_chunks = bytes / DISH::GRANULARITY_BYTES;
      for (UInt32 i = 0; i < n_chunks; ++i) {
        UInt32 tmp = wr_data_chunks[i] >> DISH::SCHEME2_OFFSET_BITS;
        unique_chunks.insert(tmp);
      }
    }

    return unique_chunks.size() <= DISH::SCHEME2_DICT_SIZE;
  }

  return false;
}

bool BlockData::isCompressible(UInt32 block_id, UInt32 offset,
                               const Byte* wr_data, UInt32 bytes,
                               DISH::scheme_t try_scheme,
                               CacheCompressionCntlr* compress_cntlr) const {

  // NOTE: DISH algorithm does not specify that the dictionary is be recomputed
  // each time, so check compression based on previous entries
  assert(offset + bytes <= m_blocksize);
  assert(block_id < SUPERBLOCK_SIZE);

  if (compress_cntlr->canCompress()) {
    if (!isValid()) {
      // Superblock is empty and no compression is needed
      return true;
    } else {
      // Line is already in the compressed format
      switch (try_scheme) {
        case DISH::scheme_t::SCHEME1:
          return isScheme1Compressible(block_id, offset, wr_data, bytes,
                                       compress_cntlr);

        case DISH::scheme_t::SCHEME2:
          return isScheme2Compressible(block_id, offset, wr_data, bytes,
                                       compress_cntlr);

        case DISH::scheme_t::UNCOMPRESSED:
          return m_valid[block_id];

        default:
          LOG_PRINT_ERROR("Cannot compress with invalid scheme");
          assert(false);
      }
    }
  } else {
    if (try_scheme == DISH::scheme_t::UNCOMPRESSED) {
      return m_valid[block_id];
    } else {
      return false;
    }
  }
  assert(false);
}

bool BlockData::isValid() const {
  for (UInt32 i = 0; i < SUPERBLOCK_SIZE; ++i) {
    if (m_valid[i]) return true;
  }

  return false;
}
bool BlockData::isValid(UInt32 block_id) const {
  assert(block_id < SUPERBLOCK_SIZE);

  return m_valid[block_id];
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

      for (UInt32 i = 0; i < m_chunks_per_block; ++i) {
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

      for (UInt32 i = 0; i < m_chunks_per_block; ++i) {
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
      LOG_PRINT_WARNING("Compaction called on an UNCOMPRESSED block");
      break;
    case DISH::scheme_t::INVALID:
      assert(false);
  }
}

void BlockData::compressScheme1(UInt32 block_id, UInt32 offset,
                                const Byte* wr_data, UInt32 bytes,
                                CacheCompressionCntlr* compress_cntlr) {

  assert((wr_data != nullptr) || (wr_data == nullptr && bytes == 0));
  assert(isValid());  // Cannot compress line from invalid state

  // Handle trivial case explicitly
  if (wr_data == nullptr || bytes == 0) {
    return;
  }

  assert((!isValid(block_id) && offset == 0 && bytes == m_blocksize) ||
         isValid(block_id));
  assert((m_scheme == DISH::scheme_t::UNCOMPRESSED && offset == 0 &&
          bytes == m_blocksize) ||
         (m_scheme != DISH::scheme_t::UNCOMPRESSED));

  LOG_ASSERT_ERROR(
      isScheme1Compressible(block_id, offset, wr_data, bytes, compress_cntlr),
      "Invalid attempt to compress %u bytes from offset %u into block %u",
      bytes, offset, block_id);

  if (m_scheme == DISH::scheme_t::SCHEME1) {
    // Cast to a 4-word type, which has the same granularity of cache blocks in
    // DISH

    if(compress_cntlr->shouldPruneDISHEntries()){
      compactScheme1();
    }

    const UInt32* wr_data_chunks = reinterpret_cast<const UInt32*>(wr_data);

    UInt32 n_chunks      = bytes / DISH::GRANULARITY_BYTES;
    UInt32 offset_chunks = offset / DISH::GRANULARITY_BYTES;
    for (UInt32 i = 0; i < n_chunks; ++i) {
      m_data_ptrs[block_id][i + offset_chunks] =
          insertDictEntry(wr_data_chunks[i]);
    }
  } else if (m_scheme == DISH::scheme_t::UNCOMPRESSED) {
    compress_cntlr->insert(DISH::scheme_t::SCHEME1);
    changeScheme(DISH::scheme_t::SCHEME1);

    // Find the uncompressed block_id.  Guaranteed to be valid due to
    // pre-condition of function.
    UInt32 uncompressed_block_id = getFirstValid();

    if (uncompressed_block_id != block_id) {
      const UInt32* data_uncompressed_chunks =
          reinterpret_cast<const UInt32*>(&m_data[uncompressed_block_id][0]);

      for (UInt32 i = 0; i < m_chunks_per_block; ++i) {
        m_data_ptrs[uncompressed_block_id][i] =
            insertDictEntry(data_uncompressed_chunks[i]);
      }
    }

    // Cast to a 4-word type, which has the same granularity of cache blocks in
    // DISH
    const UInt32* wr_data_chunks = reinterpret_cast<const UInt32*>(wr_data);

    for (UInt32 i = 0; i < m_chunks_per_block; ++i) {
      m_data_ptrs[block_id][i] = insertDictEntry(wr_data_chunks[i]);
    }
  } else if (m_scheme == DISH::scheme_t::SCHEME2) {
    // TODO: OTF scheme switching
    LOG_PRINT_ERROR("Invalid attempt to change compression scheme on-the-fly");
  } else {
    assert(false);
  }

  // Copy raw data into the uncompressed array for fast access
  std::copy_n(wr_data, bytes, &m_data[block_id][offset]);
}

void BlockData::compressScheme2(UInt32 block_id, UInt32 offset,
                                const Byte* wr_data, UInt32 bytes,
                                CacheCompressionCntlr* compress_cntlr) {

  assert((wr_data != nullptr) || (wr_data == nullptr && bytes == 0));
  assert(isValid());  // Cannot compress line from invalid state

  // Handle trivial case explicitly
  if (wr_data == nullptr || bytes == 0) {
    return;
  }

  assert((!isValid(block_id) && offset == 0 && bytes == m_blocksize) ||
         isValid(block_id));
  assert((m_scheme == DISH::scheme_t::UNCOMPRESSED && offset == 0 &&
          bytes == m_blocksize) ||
         (m_scheme != DISH::scheme_t::UNCOMPRESSED));

  LOG_ASSERT_ERROR(
      isScheme2Compressible(block_id, offset, wr_data, bytes, compress_cntlr),
      "Invalid attempt to compress %u bytes from offset %u into block %u",
      bytes, offset, block_id);

  if (m_scheme == DISH::scheme_t::SCHEME1) {
    // TODO: OTF scheme switching
    LOG_PRINT_ERROR("Invalid attempt to change compression scheme on-the-fly");
  } else if (m_scheme == DISH::scheme_t::SCHEME2) {
    std::vector<UInt8> merge_data(m_blocksize);

    std::copy_n(&m_data[block_id][0], offset, &merge_data[0]);
    std::copy_n(wr_data, bytes, &merge_data[offset]);
    std::copy_n(&m_data[block_id][offset + bytes],
                m_blocksize - (offset + bytes), &merge_data[offset + bytes]);

    // Cast to a 4-word type, which has the same granularity of cache blocks
    // in DISH
    const UInt32* merge_data_chunks =
        reinterpret_cast<const UInt32*>(&merge_data[0]);

    for (UInt32 i = 0; i < m_chunks_per_block; ++i) {
      UInt32 tmp = merge_data_chunks[i] >> DISH::SCHEME2_OFFSET_BITS;
      m_data_ptrs[block_id][i] = insertDictEntry(tmp);
      m_data_offsets[block_id][i] =
          merge_data_chunks[i] & DISH::SCHEME2_OFFSET_MASK;
    }
  } else if (m_scheme == DISH::scheme_t::UNCOMPRESSED) {
    compress_cntlr->insert(DISH::scheme_t::SCHEME2);
    changeScheme(DISH::scheme_t::SCHEME2);

    // Find the uncompressed block_id.  Guaranteed to be valid due to
    // pre-condition of function
    UInt32 uncompressed_block_id = getFirstValid();

    if (uncompressed_block_id != block_id) {
      const UInt32* data_uncompressed_chunks =
          reinterpret_cast<const UInt32*>(&m_data[uncompressed_block_id][0]);

      for (UInt32 i = 0; i < m_chunks_per_block; ++i) {
        UInt32 tmp = data_uncompressed_chunks[i] >> DISH::SCHEME2_OFFSET_BITS;
        m_data_ptrs[uncompressed_block_id][i] = insertDictEntry(tmp);
        m_data_offsets[uncompressed_block_id][i] =
            data_uncompressed_chunks[i] & DISH::SCHEME2_OFFSET_MASK;
      }
    }

    // Cast to a 4-word type, which has the same granularity of cache blocks in
    // DISH
    const UInt32* wr_data_chunks = reinterpret_cast<const UInt32*>(wr_data);

    for (UInt32 i = 0; i < m_chunks_per_block; ++i) {
      m_data_ptrs[block_id][i]    = insertDictEntry(wr_data_chunks[i] >> 4);
      m_data_offsets[block_id][i] = wr_data_chunks[i] & DISH::SCHEME2_OFFSET_MASK;
    }
  } else {
    assert(false);
  }

  // Copy raw data into the uncompressed array for fast access
  std::copy_n(wr_data, bytes, &m_data[block_id][offset]);
}

void BlockData::compress(UInt32 block_id, UInt32 offset, const Byte* wr_data,
                         UInt32 bytes, CacheCompressionCntlr* compress_cntlr, DISH::scheme_t new_scheme) {

  assert(offset + bytes <= m_blocksize);
  assert(block_id < SUPERBLOCK_SIZE);
  switch (new_scheme) {
    case DISH::scheme_t::UNCOMPRESSED:
      if(m_scheme != new_scheme) {
        assert(false);
      }
      std::copy_n(wr_data, bytes, &m_data[block_id][offset]);
      break;
    case DISH::scheme_t::SCHEME1:
      compressScheme1(block_id, offset, wr_data, bytes, compress_cntlr);
      break;

    case DISH::scheme_t::SCHEME2:
      compressScheme2(block_id, offset, wr_data, bytes, compress_cntlr);
      break;

    default:
      assert(false);
      break;
  }
  //fivijvoijdoivj; // break compile as this function needs to be fixed
  /*
  if (!isValid()) {
    // make sure the scheme is Uncompressed
    if (m_scheme != DISH::scheme_t::scheme_t::UNCOMPRESSED) {
      if (m_scheme != DISH::scheme_t::INVALID) {
        compress_cntlr->evict(m_scheme);
      } else {
        assert(false);
      }
      changeScheme(DISH::scheme_t::UNCOMPRESSED);
    }

    // Current superblock is empty, so just copy new data into the buffer
    std::copy_n(wr_data, bytes, &m_data[block_id][offset]);
  } else if (m_scheme == DISH::scheme_t::UNCOMPRESSED) {
    if (m_valid[block_id]) {
      // Current superblock contains the line in the uncompressed scheme
      std::copy_n(wr_data, bytes, &m_data[block_id][offset]);
    } else {
      // Query the compression controller for the schemes to try in order
      DISH::scheme_t first_scheme = compress_cntlr->getDefaultScheme();

      if (first_scheme == DISH::scheme_t::SCHEME1) {
        if (isScheme1Compressible(block_id, offset, wr_data, bytes,
                                  compress_cntlr)) {
          changeScheme(DISH)
          compressScheme1(block_id, offset, wr_data, bytes, compress_cntlr);
        } else if (isScheme2Compressible(block_id, offset, wr_data, bytes,
                                         compress_cntlr)) {
          compressScheme2(block_id, offset, wr_data, bytes, compress_cntlr);
        } else {
          LOG_PRINT_ERROR("Unable to compress new line with existing one");
        }
      } else {
        if (isScheme2Compressible(block_id, offset, wr_data, bytes,
                                  compress_cntlr)) {
          compressScheme2(block_id, offset, wr_data, bytes, compress_cntlr);
        } else if (isScheme1Compressible(block_id, offset, wr_data, bytes,
                                         compress_cntlr)) {
          compressScheme1(block_id, offset, wr_data, bytes, compress_cntlr);
        } else {
          LOG_PRINT_ERROR("Unable to compress new line with existing one");
        }
      }
    }
  } else if (m_scheme == DISH::scheme_t::SCHEME1) {
    // TODO: OTF scheme change
    if (isScheme1Compressible(block_id, offset, wr_data, bytes,
                              compress_cntlr)) {
      compressScheme1(block_id, offset, wr_data, bytes, compress_cntlr);
    } else {
      if (isScheme2Compressible(block_id, offset, wr_data, bytes,
                                compress_cntlr)) {
        compressScheme2(block_id, offset, wr_data, bytes, compress_cntlr);
      } else {
        LOG_PRINT_ERROR(
            "Unable to compress line with existing one using SCHEME 1");
      }
    }
  } else if (m_scheme == DISH::scheme_t::SCHEME2) {
    if (isScheme2Compressible(block_id, offset, wr_data, bytes,
                              compress_cntlr)) {
      compressScheme2(block_id, offset, wr_data, bytes, compress_cntlr);
    } else {
      LOG_PRINT_ERROR(
          "Unable to compress line with existing one using SCHEME 2");
    }
  } else {
    assert(false);
  }*/
}

void BlockData::readBlockData(UInt32 block_id, UInt32 offset, UInt32 bytes,
                           Byte* rd_data) const {

  assert(offset + bytes < m_blocksize);
  assert(block_id < SUPERBLOCK_SIZE);
  assert(bytes == 0 || rd_data != nullptr);

  if (rd_data != nullptr) {
    LOG_ASSERT_ERROR(m_valid[block_id],
                     "Attempted to decompress an invalid block %u", block_id);

    std::copy_n(&m_data[block_id][offset], bytes, rd_data);
  }
}

void BlockData::evictBlockData(UInt32 block_id, Byte* evict_data,
                               CacheCompressionCntlr* compress_cntlr) {

  assert(block_id < SUPERBLOCK_SIZE);
  assert(evict_data != nullptr);

  LOG_ASSERT_ERROR(m_valid[block_id], "Attempted to evict an invalid block %u",
                   block_id);

  std::copy_n(&m_data[block_id][0], m_blocksize, evict_data);

  m_valid[block_id] = false;
  std::fill_n(&m_data[block_id][0], m_blocksize, 0);

  // Check to see if this was the last block in the superblock.  If it was,
  // mark it as uncompressed for future operations
  if (!isValid()) {
    compress_cntlr->evict(m_scheme);
    changeScheme(DISH::scheme_t::UNCOMPRESSED);
  } else if(compress_cntlr->shouldPruneDISHEntries()){
    compact();
  }
}

DISH::scheme_t BlockData::getSchemeForWrite(UInt32 block_id, UInt32 offset,
                                 const Byte* wr_data, UInt32 bytes,
                                 CacheCompressionCntlr* compress_cntlr) const {
  if (!isValid() || !m_valid[block_id]) {
    return DISH::scheme_t::INVALID;
  } else {
    switch (m_scheme) {
      case DISH::scheme_t::UNCOMPRESSED:
        return DISH::scheme_t::UNCOMPRESSED;
      case DISH::scheme_t::SCHEME1:
        if (isScheme1Compressible(block_id, offset, wr_data, bytes, compress_cntlr)) {
          return DISH::scheme_t::SCHEME1;
        } else if (compress_cntlr->canChangeSchemeOTF() &&
                   isScheme2Compressible(block_id, offset, wr_data, bytes, compress_cntlr)) {
          return DISH::scheme_t::SCHEME2;
        } else {
          return DISH::scheme_t::INVALID;
        }

      case DISH::scheme_t::SCHEME2:
        if (isScheme2Compressible(block_id, offset, wr_data, bytes, compress_cntlr)) {
          return DISH::scheme_t::SCHEME2;
        } else if (compress_cntlr->canChangeSchemeOTF() &&
                   isScheme1Compressible(block_id, offset, wr_data, bytes, compress_cntlr)) {
          return DISH::scheme_t::SCHEME1;
        } else {
          return DISH::scheme_t::INVALID;
        }
      default:
        assert(false);
        return DISH::scheme_t::INVALID;
    }
  }
}

void BlockData::writeBlockData(UInt32 block_id, UInt32 offset, const Byte* wr_data,
                    UInt32 bytes, CacheCompressionCntlr* compress_cntlr) {
  assert(block_id < SUPERBLOCK_SIZE);
  assert(wr_data != nullptr);

  DISH::scheme_t new_scheme = getSchemeForWrite(block_id, offset, wr_data,
                                                bytes, compress_cntlr);
  assert(new_scheme != DISH::scheme_t::INVALID);
  compress(block_id, offset, wr_data, bytes, compress_cntlr, new_scheme);


  if (compress_cntlr->shouldPruneDISHEntries()) {
    compact();
  }
}

bool BlockData::canWriteBlockData(UInt32 block_id, UInt32 offset, const Byte* wr_data,
                       UInt32 bytes, CacheCompressionCntlr* compress_cntlr) const {
  assert(block_id < SUPERBLOCK_SIZE);
  assert(wr_data != nullptr);
  if (!m_valid[block_id]) {
    return false;
  }
  return getSchemeForWrite(block_id, offset, wr_data, bytes, compress_cntlr) != DISH::scheme_t::INVALID;
}

DISH::scheme_t BlockData::getSchemeForInsertion(UInt32 block_id, const Byte* wr_data,
                                     CacheCompressionCntlr* compress_cntlr) const {
  if (compress_cntlr->canCompress()) {
    if (isValid()) {
      if (m_valid[block_id]) {
        return DISH::scheme_t::INVALID;
      } else {
        DISH::scheme_t default_scheme;
        switch (m_scheme) {

          // currently uncompressed
          case DISH::scheme_t::UNCOMPRESSED:
            default_scheme = compress_cntlr->getDefaultScheme();
            if (default_scheme == DISH::scheme_t::SCHEME1) {
              if(isScheme1Compressible(block_id, 0, wr_data, m_blocksize, compress_cntlr)) {
                return DISH::scheme_t::SCHEME1;
              } else if (isScheme2Compressible(block_id, 0, wr_data, m_blocksize, compress_cntlr)) {
                return DISH::scheme_t::SCHEME2;
              } else {
                return DISH::scheme_t::INVALID;
              }
            } else if(default_scheme == DISH::scheme_t::SCHEME2){
              if(isScheme2Compressible(block_id, 0, wr_data, m_blocksize, compress_cntlr)) {
                return DISH::scheme_t::SCHEME2;
              } else if (isScheme2Compressible(block_id, 0, wr_data, m_blocksize, compress_cntlr)) {
                return DISH::scheme_t::SCHEME1;
              } else {
                return DISH::scheme_t::INVALID;
              }
            } else {
              assert(false);
              return DISH::scheme_t::INVALID;
            }
            break;

          // currently in scheme 1
          case DISH::scheme_t::SCHEME1:
            if (isScheme1Compressible(block_id, 0, wr_data,
                                      m_blocksize, compress_cntlr)) {
              return DISH::scheme_t::SCHEME1;
            } else if (compress_cntlr->canChangeSchemeOTF() &&
                       isScheme2Compressible(block_id, 0, wr_data,
                                             m_blocksize, compress_cntlr)) {
              return DISH::scheme_t::SCHEME2;
            } else {
              return DISH::scheme_t::INVALID;
            }
            break;

          // currently in scheme 2
          case DISH::scheme_t::SCHEME2:
            if (isScheme2Compressible(block_id, 0, wr_data,
                                      m_blocksize, compress_cntlr)) {
              return DISH::scheme_t::SCHEME2;
            } else if (compress_cntlr->canChangeSchemeOTF() &&
                       isScheme1Compressible(block_id, 0, wr_data, m_blocksize, compress_cntlr)) {
              return DISH::scheme_t::SCHEME1;
            } else {
              return DISH::scheme_t::INVALID;
            }
            break;

          // should be unreachable
          default:
            assert(false);
            return DISH::scheme_t::INVALID;
        }
      }
    } else {
      return DISH::scheme_t::UNCOMPRESSED;
    }
  } else {
    return (isValid() ? DISH::scheme_t::INVALID : DISH::scheme_t::UNCOMPRESSED);
  }
}

bool BlockData::canInsertBlockData(UInt32 block_id, const Byte* wr_data, CacheCompressionCntlr* compress_cntlr) const {
  assert(block_id < SUPERBLOCK_SIZE);
  assert(wr_data != nullptr);
  if (m_valid[block_id]) {
    return false;
  }
  return (getSchemeForInsertion(block_id, wr_data, compress_cntlr) != DISH::scheme_t::INVALID);
}

void BlockData::insertBlockData(UInt32 block_id, const Byte* wr_data,
                                CacheCompressionCntlr* compress_cntlr) {

  assert(block_id < SUPERBLOCK_SIZE);
  assert(wr_data != nullptr);

  LOG_ASSERT_ERROR(!m_valid[block_id],
                   "Attempted to insert block %u on top of an existing one",
                   block_id);

  DISH::scheme_t new_scheme = getSchemeForInsertion(block_id, wr_data, compress_cntlr);
  assert(new_scheme != DISH::scheme_t::INVALID);

  compress(block_id, 0, wr_data, m_blocksize, compress_cntlr, new_scheme);

  m_valid[block_id] = true;
  if (compress_cntlr->shouldPruneDISHEntries()) {
    compact();
  }
}
