#include "block_data.h"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <sstream>

#include "cache.h"
#include "cache.h"
#include "log.h"
#include "stats.h"

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
  } else {
    m_used_ptrs.erase(ptr);   // Remove from used list
    m_dict.erase(ptr);        // Remove dictionary entry
    m_free_ptrs.insert(ptr);  // Add to free list
  }
}

void BlockData::changeScheme(DISH::scheme_t new_scheme) {
  if (m_scheme != new_scheme) {
    LOG_PRINT("Changing scheme from %s to %s", DISH::scheme2name.at(m_scheme),
              DISH::scheme2name.at(new_scheme));
  }

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
      ++m_otf_switch;
      m_dict.clear();
      m_free_ptrs.clear();
      m_used_ptrs.clear();
      // Add all pointers to free list
      for (UInt8 p = 0; p < DISH::SCHEME2_DICT_SIZE; ++p) {
        m_free_ptrs.insert(p);
      }
    }
  } else if (m_scheme == DISH::scheme_t::SCHEME2) {
    if (new_scheme == DISH::scheme_t::UNCOMPRESSED) {
      m_dict.clear();
      m_free_ptrs.clear();
      m_used_ptrs.clear();
    } else if (new_scheme == DISH::scheme_t::SCHEME1) {
      ++m_otf_switch;
      m_dict.clear();
      m_free_ptrs.clear();
      m_used_ptrs.clear();
      // Add all pointers to free list
      for (UInt8 p = 0; p < DISH::SCHEME1_DICT_SIZE; ++p) {
        m_free_ptrs.insert(p);
      }
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

BlockData::BlockData(UInt32 way, UInt32 set_index, UInt32 blocksize,
                     const Cache* parent_cache, bool is_compressible)
    : m_blocksize{blocksize},
      m_chunks_per_block{blocksize / DISH::GRANULARITY_BYTES},
      m_scheme{DISH::scheme_t::UNCOMPRESSED},
      m_valid{false},
      m_dict(DISH::SCHEME1_DICT_SIZE),       // Maximum number of buckets
      m_free_ptrs(DISH::SCHEME1_DICT_SIZE),  // Maximum number of buckets
      m_used_ptrs(DISH::SCHEME1_DICT_SIZE),  // Maximum number of buckets
      m_data_ptrs{{0}},
      m_data_offsets{{0}},
      m_parent_cache{parent_cache},
      m_otf_switch{0},
      m_scheme1_1x{0},
      m_scheme1_2x{0},
      m_scheme1_3x{0},
      m_scheme1_4x{0},
      m_scheme2_1x{0},
      m_scheme2_2x{0},
      m_scheme2_3x{0},
      m_scheme2_4x{0},
      m_uncompressed_1x{0} {

  for (auto& e : m_data) {
    e.resize(m_blocksize);
  }

  changeScheme(m_scheme);

  std::string stat_name;
  std::string specifier = (std::string("_s") + std::to_string(set_index)) +
                          (std::string("_w") + std::to_string(way));
  core_id_t core_id = m_parent_cache->getCoreId();
  String cache_name = m_parent_cache->getName();

  stat_name.assign(std::string("uncompressed_1x") + specifier);
  registerStatsMetric(cache_name, core_id, stat_name.c_str(),
                      &m_uncompressed_1x);

  if (is_compressible) {
    stat_name.assign(std::string("otf_switch") + specifier);
    registerStatsMetric(cache_name, core_id, stat_name.c_str(), &m_otf_switch);

    stat_name.assign(std::string("scheme1_1x") + specifier);
    registerStatsMetric(cache_name, core_id, stat_name.c_str(), &m_scheme1_1x);

    stat_name.assign(std::string("scheme1_2x") + specifier);
    registerStatsMetric(cache_name, core_id, stat_name.c_str(), &m_scheme1_2x);

    stat_name.assign(std::string("scheme1_3x") + specifier);
    registerStatsMetric(cache_name, core_id, stat_name.c_str(), &m_scheme1_3x);

    stat_name.assign(std::string("scheme1_4x") + specifier);
    registerStatsMetric(cache_name, core_id, stat_name.c_str(), &m_scheme1_4x);

    stat_name.assign(std::string("scheme2_1x") + specifier);
    registerStatsMetric(cache_name, core_id, stat_name.c_str(), &m_scheme2_1x);

    stat_name.assign(std::string("scheme2_2x") + specifier);
    registerStatsMetric(cache_name, core_id, stat_name.c_str(), &m_scheme2_2x);

    stat_name.assign(std::string("scheme2_3x") + specifier);
    registerStatsMetric(cache_name, core_id, stat_name.c_str(), &m_scheme2_3x);

    stat_name.assign(std::string("scheme2_4x") + specifier);
    registerStatsMetric(cache_name, core_id, stat_name.c_str(), &m_scheme2_4x);
  }
}

BlockData::~BlockData() {}

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
      }
    }
  } else {
    if (try_scheme == DISH::scheme_t::UNCOMPRESSED) {
      return m_valid[block_id];
    } else {
      return false;
    }
  }
}

bool BlockData::isScheme1Compressible(
    UInt32 block_id, UInt32 offset, const Byte* wr_data, UInt32 bytes,
    CacheCompressionCntlr* compress_cntlr) const {

  assert(wr_data != nullptr);

  // Handle trivial cases explicitly
  if (!isValid()) return false;
  if (!compress_cntlr->canCompress()) return false;

  /*
   * Case 1: if the specified block is not valid, then the changes must
   * write a full new line
   *
   * Case 2: if the specified block is valid, any legal combination of offset
   * and bytes is possible
   */
  assert((!isValid(block_id) && offset == 0 && bytes == m_blocksize) ||
         isValid(block_id));

  if (m_scheme == DISH::scheme_t::SCHEME1) {
    std::vector<UInt8> test_data(m_blocksize);

    // Populate the test_data vector with the bytes from m_data and wr_data
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

    return true;
  } else if (m_scheme == DISH::scheme_t::SCHEME2) {
    if (compress_cntlr->canChangeSchemeOTF()) {
      // Need to check compression with the currently valid lines
      std::unordered_set<UInt32> unique_chunks;

      for (UInt32 i = 0; i < SUPERBLOCK_SIZE; i++) {
        if (m_valid[i] && i != block_id) {
          const UInt32* data_uncompressed_chunks =
              reinterpret_cast<const UInt32*>(&m_data[i][0]);

          // Add all chunks from currently uncompressed line to set
          for (UInt32 j = 0; j < m_chunks_per_block; ++j) {
            unique_chunks.insert(data_uncompressed_chunks[i]);
          }
        }
      }

      // Cast to a 4-word type, which has the same granularity of cache blocks
      // in DISH
      const UInt32* wr_data_chunks = reinterpret_cast<const UInt32*>(wr_data);

      // Add all chunks from wr_data_chunks line to set
      UInt32 n_chunks = bytes / DISH::GRANULARITY_BYTES;
      for (UInt32 i = 0; i < n_chunks; ++i) {
        unique_chunks.insert(wr_data_chunks[i]);
      }

      return unique_chunks.size() <= DISH::SCHEME1_DICT_SIZE;
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

    // If the new data completely overwrites the current, uncompressed line,
    // only consider the dictionary entries that it would generate
    if (uncompressed_block_id != block_id) {
      const UInt32* data_uncompressed_chunks =
          reinterpret_cast<const UInt32*>(&m_data[uncompressed_block_id][0]);

      // Add all chunks from currently uncompressed line to set
      for (UInt32 i = 0; i < m_chunks_per_block; ++i) {
        unique_chunks.insert(data_uncompressed_chunks[i]);
      }
    }

    // Cast to a 4-word type, which has the same granularity of cache blocks
    // in DISH
    const UInt32* wr_data_chunks = reinterpret_cast<const UInt32*>(wr_data);

    // Add all chunks from wr_data_chunks line to set
    UInt32 n_chunks = bytes / DISH::GRANULARITY_BYTES;
    for (UInt32 i = 0; i < n_chunks; ++i) {
      unique_chunks.insert(wr_data_chunks[i]);
    }

    return unique_chunks.size() <= DISH::SCHEME1_DICT_SIZE;
  }

  return false;
}

bool BlockData::isScheme2Compressible(
    UInt32 block_id, UInt32 offset, const Byte* wr_data, UInt32 bytes,
    CacheCompressionCntlr* compress_cntlr) const {

  assert(wr_data != nullptr);

  // Handle trivial cases explicitly
  if (!isValid()) return false;
  if (!compress_cntlr->canCompress()) return false;

  /*
   * Case 1: if the specified block is not valid, then the changes must write
   * a full new line
   *
   * Case 2: if the specified block is valid, any legal combination of offset
   * and bytes is possible
   */
  assert((!isValid(block_id) && offset == 0 && bytes == m_blocksize) ||
         isValid(block_id));

  if (m_scheme == DISH::scheme_t::SCHEME1) {
    if (compress_cntlr->canChangeSchemeOTF()) {
      // Need to check compression with the currently valid lines
      std::unordered_set<UInt32> unique_chunks;

      for (UInt32 i = 0; i < SUPERBLOCK_SIZE; i++) {
        if (m_valid[i] && i != block_id) {
          const UInt32* data_uncompressed_chunks =
              reinterpret_cast<const UInt32*>(&m_data[i][0]);

          // Add all chunks from the currently valid lines to set
          for (UInt32 j = 0; j < m_chunks_per_block; ++j) {
            unique_chunks.insert(data_uncompressed_chunks[j] >>
                                 DISH::SCHEME2_OFFSET_BITS);
          }
        }
      }

      // Cast to a 4-word type, which has the same granularity of cache blocks
      // in DISH
      const UInt32* wr_data_chunks = reinterpret_cast<const UInt32*>(wr_data);

      // Add all chunks from wr_data_chunks line to set
      UInt32 n_chunks = bytes / DISH::GRANULARITY_BYTES;
      for (UInt32 i = 0; i < n_chunks; ++i) {
        UInt32 tmp = wr_data_chunks[i] >> DISH::SCHEME2_OFFSET_BITS;
        unique_chunks.insert(tmp);
      }

      return unique_chunks.size() <= DISH::SCHEME2_DICT_SIZE;
    } else {
      // Do not convert between compression schemes on-the-fly
      return false;
    }
  } else if (m_scheme == DISH::scheme_t::SCHEME2) {
    std::vector<UInt8> test_data(m_blocksize);

    // Populate the test_data vector with the bytes from m_data and wr_data
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

    return true;
  } else if (m_scheme == DISH::scheme_t::UNCOMPRESSED) {
    // Need to check compression with the currently uncompressed line
    std::unordered_set<UInt32> unique_chunks;

    // Find the uncompressed block_id.  Guaranteed to be valid due to
    // pre-condition of function
    UInt32 uncompressed_block_id = getFirstValid();

    // If the new data completely overwrites the current, uncompressed line,
    // only consider the dictionary entries that it would generate
    if (uncompressed_block_id != block_id) {
      const UInt32* data_uncompressed_chunks =
          reinterpret_cast<const UInt32*>(&m_data[uncompressed_block_id][0]);

      // Add all chunks from currently uncompressed line to set
      for (UInt32 i = 0; i < m_chunks_per_block; ++i) {
        unique_chunks.insert(data_uncompressed_chunks[i] >>
                             DISH::SCHEME2_OFFSET_BITS);
      }
    }

    // Cast to a 4-word type, which has the same granularity of cache blocks
    // in DISH
    const UInt32* wr_data_chunks = reinterpret_cast<const UInt32*>(wr_data);

    // Add all chunks from wr_data_chunks line to set
    UInt32 n_chunks = bytes / DISH::GRANULARITY_BYTES;
    for (UInt32 i = 0; i < n_chunks; ++i) {
      UInt32 tmp = wr_data_chunks[i] >> DISH::SCHEME2_OFFSET_BITS;
      unique_chunks.insert(tmp);
    }

    return unique_chunks.size() <= DISH::SCHEME2_DICT_SIZE;
  }

  return false;
}

void BlockData::compact() {
  LOG_PRINT("Compacting BlockData in scheme %s",
            DISH::scheme2name.at(m_scheme));

  if (!isValid()) return;

  switch (m_scheme) {
    case DISH::scheme_t::SCHEME1:
      compactScheme1();
      break;

    case DISH::scheme_t::SCHEME2:
      compactScheme2();
      break;

    case DISH::scheme_t::UNCOMPRESSED:
      // Do nothing
      break;
    default:
      assert(false);
  }
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

void BlockData::compressScheme1(UInt32 block_id, UInt32 offset,
                                const Byte* wr_data, UInt32 bytes,
                                CacheCompressionCntlr* compress_cntlr) {

  LOG_PRINT(
      "Compressing BlockData (%s) to %s\n"
      "block_id: %u offset: %u wr_data: %p bytes: %u",
      DISH::scheme2name.at(m_scheme),
      DISH::scheme2name.at(DISH::scheme_t::SCHEME1), block_id, offset, wr_data,
      bytes);

  assert(wr_data != nullptr);
  assert(isValid());  // Cannot compress line from invalid state

  assert((!isValid(block_id) && offset == 0 && bytes == m_blocksize) ||
         isValid(block_id));
  assert((m_scheme == DISH::scheme_t::UNCOMPRESSED && offset == 0 &&
          bytes == m_blocksize) ||
         (m_scheme != DISH::scheme_t::UNCOMPRESSED));

  LOG_ASSERT_ERROR(
      isScheme1Compressible(block_id, offset, wr_data, bytes, compress_cntlr),
      "Invalid attempt to compress data using scheme %s",
      DISH::scheme2name.at(DISH::scheme_t::SCHEME1));

  if (m_scheme == DISH::scheme_t::SCHEME1) {
    // Cast to a 4-word type, which has the same granularity of cache blocks in
    // DISH

    if (compress_cntlr->shouldPruneDISHEntries()) {
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
    if (compress_cntlr->canChangeSchemeOTF()) {
      compress_cntlr->evict(DISH::scheme_t::SCHEME2);
      compress_cntlr->insert(DISH::scheme_t::SCHEME1);
      changeScheme(DISH::scheme_t::SCHEME1);

      for (UInt32 i = 0; i < SUPERBLOCK_SIZE; i++) {
        if (m_valid[i] && i != block_id) {
          const UInt32* data_uncompressed_chunks =
              reinterpret_cast<const UInt32*>(&m_data[i][0]);

          for (UInt32 j = 0; j < m_chunks_per_block; ++j) {
            m_data_ptrs[i][j] = insertDictEntry(data_uncompressed_chunks[j]);
          }
        }
      }

      // Cast to a 4-word type, which has the same granularity of cache blocks
      // in
      // DISH
      const UInt32* wr_data_chunks = reinterpret_cast<const UInt32*>(wr_data);

      for (UInt32 i = 0; i < m_chunks_per_block; ++i) {
        m_data_ptrs[block_id][i] = insertDictEntry(wr_data_chunks[i]);
      }
    } else {
      LOG_PRINT_ERROR(
          "Invalid attempt to change compression scheme on-the-fly");
    }
  } else {
    assert(false);
  }

  // Copy raw data into the uncompressed array for fast access
  std::copy_n(wr_data, bytes, &m_data[block_id][offset]);
}

void BlockData::compressScheme2(UInt32 block_id, UInt32 offset,
                                const Byte* wr_data, UInt32 bytes,
                                CacheCompressionCntlr* compress_cntlr) {

  LOG_PRINT(
      "Compressing BlockData (%s) to %s\n"
      "block_id: %u offset: %u wr_data: %p bytes: %u",
      DISH::scheme2name.at(m_scheme),
      DISH::scheme2name.at(DISH::scheme_t::SCHEME2), block_id, offset, wr_data,
      bytes);

  assert(wr_data != nullptr);
  assert(isValid());  // Cannot compress line from invalid state

  assert((!isValid(block_id) && offset == 0 && bytes == m_blocksize) ||
         isValid(block_id));
  assert((m_scheme == DISH::scheme_t::UNCOMPRESSED && offset == 0 &&
          bytes == m_blocksize) ||
         (m_scheme != DISH::scheme_t::UNCOMPRESSED));

  LOG_ASSERT_ERROR(
      isScheme2Compressible(block_id, offset, wr_data, bytes, compress_cntlr),
      "Invalid attempt to compress data using scheme %s",
      DISH::scheme_t::SCHEME2);

  if (m_scheme == DISH::scheme_t::SCHEME1) {
    if (compress_cntlr->canChangeSchemeOTF()) {
      compress_cntlr->evict(DISH::scheme_t::SCHEME1);
      compress_cntlr->insert(DISH::scheme_t::SCHEME2);
      changeScheme(DISH::scheme_t::SCHEME2);

      for (UInt32 i = 0; i < SUPERBLOCK_SIZE; i++) {
        if (m_valid[i] && i != block_id) {
          const UInt32* data_uncompressed_chunks =
              reinterpret_cast<const UInt32*>(&m_data[i][0]);

          for (UInt32 j = 0; j < m_chunks_per_block; ++j) {
            UInt32 tmp =
                data_uncompressed_chunks[j] >> DISH::SCHEME2_OFFSET_BITS;

            m_data_ptrs[i][j] = insertDictEntry(tmp);
            m_data_offsets[i][j] =
                data_uncompressed_chunks[j] & DISH::SCHEME2_OFFSET_MASK;
          }
        }
      }

      // Cast to a 4-word type, which has the same granularity of cache blocks
      // in
      // DISH
      const UInt32* wr_data_chunks = reinterpret_cast<const UInt32*>(wr_data);

      for (UInt32 i = 0; i < m_chunks_per_block; ++i) {
        m_data_ptrs[block_id][i] = insertDictEntry(wr_data_chunks[i] >> 4);
        m_data_offsets[block_id][i] =
            wr_data_chunks[i] & DISH::SCHEME2_OFFSET_MASK;
      }
    } else {
      LOG_PRINT_ERROR(
          "Invalid attempt to change compression scheme on-the-fly");
    }
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
      m_data_ptrs[block_id][i] = insertDictEntry(wr_data_chunks[i] >> 4);
      m_data_offsets[block_id][i] =
          wr_data_chunks[i] & DISH::SCHEME2_OFFSET_MASK;
    }
  } else {
    assert(false);
  }

  // Copy raw data into the uncompressed array for fast access
  std::copy_n(wr_data, bytes, &m_data[block_id][offset]);
}

DISH::scheme_t BlockData::getSchemeForWrite(
    UInt32 block_id, UInt32 offset, const Byte* wr_data, UInt32 bytes,
    CacheCompressionCntlr* compress_cntlr) const {

  if (!isValid() || !m_valid[block_id]) {
    return DISH::scheme_t::INVALID;
  } else {
    switch (m_scheme) {
      case DISH::scheme_t::UNCOMPRESSED:
        return DISH::scheme_t::UNCOMPRESSED;
      case DISH::scheme_t::SCHEME1:
        if (isScheme1Compressible(block_id, offset, wr_data, bytes,
                                  compress_cntlr)) {
          return DISH::scheme_t::SCHEME1;
        } else if (compress_cntlr->canChangeSchemeOTF() &&
                   isScheme2Compressible(block_id, offset, wr_data, bytes,
                                         compress_cntlr)) {
          return DISH::scheme_t::SCHEME2;
        } else {
          return DISH::scheme_t::INVALID;
        }

      case DISH::scheme_t::SCHEME2:
        if (isScheme2Compressible(block_id, offset, wr_data, bytes,
                                  compress_cntlr)) {
          return DISH::scheme_t::SCHEME2;
        } else if (compress_cntlr->canChangeSchemeOTF() &&
                   isScheme1Compressible(block_id, offset, wr_data, bytes,
                                         compress_cntlr)) {
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

DISH::scheme_t BlockData::getSchemeForInsertion(
    UInt32 block_id, const Byte* wr_data,
    CacheCompressionCntlr* compress_cntlr) const {

  if (wr_data != nullptr) {
    LOG_PRINT(
        "BlockData(%p) getting scheme for insertion (%d) size: %u block_id: %u "
        "wr_data: %s",
        this, compress_cntlr->canCompress(),
        m_blocksize / DISH::GRANULARITY_BYTES, block_id,
        printChunks(reinterpret_cast<const UInt32*>(wr_data),
                    m_blocksize / DISH::GRANULARITY_BYTES)
            .c_str());
  }

  if (compress_cntlr->canCompress()) {
    if (isValid()) {
      if (m_valid[block_id]) {
        return DISH::scheme_t::INVALID;
      } else {
        DISH::scheme_t default_scheme;

        if (m_scheme == DISH::scheme_t::UNCOMPRESSED) {
          default_scheme = compress_cntlr->getDefaultScheme();

          LOG_PRINT("BlockData(%p) getting default_scheme: %s", this,
                    DISH::scheme2name.at(default_scheme));

          if (default_scheme == DISH::scheme_t::SCHEME1) {
            if (isScheme1Compressible(block_id, 0, wr_data, m_blocksize,
                                      compress_cntlr)) {
              return DISH::scheme_t::SCHEME1;
            } else if (isScheme2Compressible(block_id, 0, wr_data, m_blocksize,
                                             compress_cntlr)) {
              return DISH::scheme_t::SCHEME2;
            } else {
              return DISH::scheme_t::INVALID;
            }
          } else if (default_scheme == DISH::scheme_t::SCHEME2) {
            if (isScheme2Compressible(block_id, 0, wr_data, m_blocksize,
                                      compress_cntlr)) {
              return DISH::scheme_t::SCHEME2;
            } else if (isScheme2Compressible(block_id, 0, wr_data, m_blocksize,
                                             compress_cntlr)) {
              return DISH::scheme_t::SCHEME1;
            } else {
              return DISH::scheme_t::INVALID;
            }
          } else {
            assert(false);
          }
        } else if (m_scheme == DISH::scheme_t::SCHEME1) {
          if (isScheme1Compressible(block_id, 0, wr_data, m_blocksize,
                                    compress_cntlr)) {
            return DISH::scheme_t::SCHEME1;
          } else if (compress_cntlr->canChangeSchemeOTF() &&
                     isScheme2Compressible(block_id, 0, wr_data, m_blocksize,
                                           compress_cntlr)) {
            return DISH::scheme_t::SCHEME2;
          } else {
            return DISH::scheme_t::INVALID;
          }
        } else if (m_scheme == DISH::scheme_t::SCHEME2) {
          if (isScheme2Compressible(block_id, 0, wr_data, m_blocksize,
                                    compress_cntlr)) {
            return DISH::scheme_t::SCHEME2;
          } else if (compress_cntlr->canChangeSchemeOTF() &&
                     isScheme1Compressible(block_id, 0, wr_data, m_blocksize,
                                           compress_cntlr)) {
            return DISH::scheme_t::SCHEME1;
          } else {
            return DISH::scheme_t::INVALID;
          }
        } else {
          assert(false);
        }
      }
    } else {
      // Block is compressible, but there is no data currently inside it
      return DISH::scheme_t::UNCOMPRESSED;
    }
  } else {
    // Block is not compressible, so if there is currently data in the block
    // it is not possible to insert more

    if (isValid())
      return DISH::scheme_t::INVALID;
    else
      return DISH::scheme_t::UNCOMPRESSED;
  }
}

bool BlockData::canWriteBlockData(UInt32 block_id, UInt32 offset,
                                  const Byte* wr_data, UInt32 bytes,
                                  CacheCompressionCntlr* compress_cntlr) const {

  assert((wr_data == nullptr && offset == 0 && bytes == 0) ||
         (wr_data != nullptr));

  if (!m_valid[block_id])
    return false;
  else if (wr_data == nullptr)
    return true;

  DISH::scheme_t try_scheme =
      getSchemeForWrite(block_id, offset, wr_data, bytes, compress_cntlr);

  return try_scheme != DISH::scheme_t::INVALID;
}

void BlockData::writeBlockData(UInt32 block_id, UInt32 offset,
                               const Byte* wr_data, UInt32 bytes,
                               CacheCompressionCntlr* compress_cntlr) {

  LOG_PRINT(
      "BlockData(%p) writing block_id: %u offset: %u wr_data: %p bytes: %u",
      this, block_id, offset, wr_data, bytes);

  assert(wr_data != nullptr || (wr_data == nullptr && bytes == 0));

  if (wr_data != nullptr) {
    // Query getSchemeForWrite to see if the block will cause OTF scheme change
    DISH::scheme_t new_scheme =
        getSchemeForWrite(block_id, offset, wr_data, bytes, compress_cntlr);

    switch (new_scheme) {
      case DISH::scheme_t::UNCOMPRESSED:
        std::copy_n(wr_data, bytes, &m_data[block_id][offset]);
        break;

      case DISH::scheme_t::SCHEME1:
        compressScheme1(block_id, offset, wr_data, bytes, compress_cntlr);
        break;

      case DISH::scheme_t::SCHEME2:
        compressScheme2(block_id, offset, wr_data, bytes, compress_cntlr);
        break;

      default:
        LOG_PRINT_ERROR("Invalid attempt to insert line");
    }
  }

  if (compress_cntlr->shouldPruneDISHEntries()) compact();
}

void BlockData::readBlockData(UInt32 block_id, UInt32 offset, UInt32 bytes,
                              Byte* rd_data) const {

  LOG_PRINT(
      "BlockData(%p) reading block_id: %u offset: %u rd_data: %p bytes: %u",
      this, block_id, offset, rd_data, bytes);

  assert(rd_data != nullptr || (rd_data == nullptr && bytes == 0));

  if (rd_data != nullptr) {
    LOG_ASSERT_ERROR(m_valid[block_id],
                     "Attempted to decompress an invalid block %u", block_id);

    std::copy_n(&m_data[block_id][offset], bytes, rd_data);
  }
}

bool BlockData::canInsertBlockData(
    UInt32 block_id, const Byte* ins_data,
    CacheCompressionCntlr* compress_cntlr) const {

  if (m_valid[block_id])
    return false;
  else if (ins_data == nullptr)
    return true;

  DISH::scheme_t try_scheme =
      getSchemeForInsertion(block_id, ins_data, compress_cntlr);

  return try_scheme != DISH::scheme_t::INVALID;
}

void BlockData::insertBlockData(UInt32 block_id, const Byte* ins_data,
                                CacheCompressionCntlr* compress_cntlr) {

  LOG_PRINT(
      "BlockData(%s) inserting block_id: %u ins_data: %p m_scheme: %s m_valid: "
      "{%d%d%d%d}",
      m_parent_cache->getName().c_str(), block_id, ins_data,
      DISH::scheme2name.at(m_scheme), m_valid[0], m_valid[1], m_valid[2],
      m_valid[3]);

  LOG_ASSERT_ERROR(!m_valid[block_id],
                   "Attempted to insert block on top of an existing one");

  if (ins_data != nullptr) {
    DISH::scheme_t new_scheme =
        getSchemeForInsertion(block_id, ins_data, compress_cntlr);

    switch (new_scheme) {
      case DISH::scheme_t::UNCOMPRESSED:
        std::copy_n(ins_data, m_blocksize, &m_data[block_id][0]);
        break;

      case DISH::scheme_t::SCHEME1:
        compressScheme1(block_id, 0, ins_data, m_blocksize, compress_cntlr);
        break;

      case DISH::scheme_t::SCHEME2:
        compressScheme2(block_id, 0, ins_data, m_blocksize, compress_cntlr);
        break;

      default:
        LOG_PRINT_ERROR("Invalid attempt to insert line");
    }
  }

  m_valid[block_id] = true;

  if (compress_cntlr->shouldPruneDISHEntries()) compact();

  updateStatistics();
}

void BlockData::evictBlockData(UInt32 block_id, Byte* evict_data,
                               CacheCompressionCntlr* compress_cntlr) {

  LOG_ASSERT_ERROR(m_valid[block_id], "Attempted to evict an invalid block %u",
                   block_id);

  if (evict_data != nullptr) {
    std::copy_n(&m_data[block_id][0], m_blocksize, evict_data);
  }

  m_valid[block_id] = false;
  std::fill_n(&m_data[block_id][0], m_blocksize, 0);

  // Check to see if this was the last block in the superblock.  If it was,
  // mark it as uncompressed for future operations
  if (!isValid()) {
    compress_cntlr->evict(m_scheme);
    changeScheme(DISH::scheme_t::UNCOMPRESSED);
  } else if (compress_cntlr->shouldPruneDISHEntries()) {
    compact();
  }
}

void BlockData::invalidateBlockData(UInt32 block_id,
                                    CacheCompressionCntlr* compress_cntlr) {

  LOG_ASSERT_ERROR(m_valid[block_id], "Attempted to evict an invalid block %u",
                   block_id);

  m_valid[block_id] = false;
  std::fill_n(&m_data[block_id][0], m_blocksize, 0);

  // Check to see if this was the last block in the superblock.  If it was,
  // mark it as uncompressed for future operations
  if (!isValid()) {
    compress_cntlr->evict(m_scheme);
    changeScheme(DISH::scheme_t::UNCOMPRESSED);
  }
}

std::string BlockData::dump() const {
  std::stringstream info_ss;

  info_ss << "BlockData(" << DISH::scheme2name.at(m_scheme);

  info_ss << " valid: ";
  for (UInt32 i = 0; i < SUPERBLOCK_SIZE; ++i) {
    info_ss << m_valid[i];
  }

  info_ss << ")->m_data{ ";

  for (UInt32 i = 0; i < SUPERBLOCK_SIZE; ++i) {
    info_ss << printChunks(reinterpret_cast<const UInt32*>(&m_data[i][0]),
                           m_blocksize / DISH::GRANULARITY_BYTES);
  }

  info_ss << " }";

  return info_ss.str();
}

int BlockData::getNumValid() {
  int res = 0;
  for (UInt32 i = 0; i < SUPERBLOCK_SIZE; i++) {
    if (m_valid[i]) {
      res++;
    }
  }
  return res;
}

void BlockData::updateStatistics() {
  switch (m_scheme) {
    case DISH::scheme_t::UNCOMPRESSED:
      if (isValid()) {
        m_uncompressed_1x++;
        LOG_PRINT("BlockData(%s) m_uncompressed_1x: %lu",
                  m_parent_cache->getName().c_str(), m_uncompressed_1x);
      }
      break;
    case DISH::scheme_t::SCHEME1:
      switch (getNumValid()) {
        case 1:
          m_scheme1_1x++;
          break;
        case 2:
          m_scheme1_2x++;
          break;
        case 3:
          m_scheme1_3x++;
          break;
        case 4:
          m_scheme1_4x++;
          break;
        default:
          break;
      }
      break;
    case DISH::scheme_t::SCHEME2:
      switch (getNumValid()) {
        case 1:
          m_scheme2_1x++;
          break;
        case 2:
          m_scheme2_2x++;
          break;
        case 3:
          ++m_scheme2_3x;
          break;
        case 4:
          m_scheme2_4x++;
          break;
        default:
          break;
      }
      break;
    default:
      break;
  }
}
