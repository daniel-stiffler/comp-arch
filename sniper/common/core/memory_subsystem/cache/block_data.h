#pragma once

#include <unordered_map>
#include <unordered_set>

#include "compress_utils.h"
#include "superblock_info.h"

class CacheCompressionCntlr;

class BlockData {
 protected:
  const UInt32 m_blocksize;
  DISH::scheme_t m_scheme;
  bool m_valid[SUPERBLOCK_SIZE];

  // Cache lines are stored as contiguous blocks of memory to reduce runtime
  // overhead of decompression
  UInt8 m_data[SUPERBLOCK_SIZE][BLOCKSIZE_BYTES];

  // 4-byte dictionary entries, used either as 4-byte values or 28-bit truncated
  // representation
  std::unordered_map<UInt8, UInt32> m_dict;
  // Dictionary entry free and used pointer lists
  std::unordered_set<UInt8> m_free_ptrs;
  std::unordered_set<UInt8> m_used_ptrs;
  // "Pointers" to elements in the dictionary
  //   - Scheme 1 uses log2(DISH::SCHEME1_DICT_SIZE) = 3-bit
  //   - Scheme 2 uses log2(DISH::SCHEME2_DICT_SIZE) = 2-bit
  UInt8 m_data_ptrs[SUPERBLOCK_SIZE][DISH::BLOCK_ENTRIES];
  // Dictionary pointer offsets for Scheme 2 compression
  UInt8 m_data_offsets[SUPERBLOCK_SIZE][DISH::BLOCK_ENTRIES];

 private:
  bool lookupDictEntry(UInt32 value, UInt8* ptr = nullptr);
  UInt8 insertDictEntry(UInt32 value);
  void removeDictEntry(UInt8 ptr);
  bool initScheme(DISH::scheme_t new_scheme);
  UInt32 getFirstValid() {
    for (UInt32 i = 0; i < SUPERBLOCK_SIZE; ++i) {
      if (m_valid[i]) return i;
    }

    return SUPERBLOCK_SIZE;
  }

  bool isScheme1Compressible(UInt32 block_id, UInt32 offset,
                             const Byte* wr_data, UInt32 bytes,
                             CacheCompressionCntlr* compress_cntlr);
  bool isScheme2Compressible(UInt32 block_id, UInt32 offset,
                             const Byte* wr_data, UInt32 bytes,
                             CacheCompressionCntlr* compress_cntlr);

  void compactScheme1();
  void compactScheme2();

  void compressScheme1(UInt32 block_id, UInt32 offset, const Byte* wr_data,
                       UInt32 bytes, CacheCompressionCntlr* compress_cntlr);
  void compressScheme2(UInt32 block_id, UInt32 offset, const Byte* wr_data,
                       UInt32 bytes, CacheCompressionCntlr* compress_cntlr);

 public:
  BlockData(UInt32 blocksize);
  virtual ~BlockData();

  bool isCompressible(UInt32 block_id, UInt32 offset, const Byte* wr_data,
                      UInt32 bytes,
                      DISH::scheme_t try_scheme,
                      CacheCompressionCntlr* compress_cntlr);

  bool isValid() const {
    for (UInt32 i = 0; i < SUPERBLOCK_SIZE; ++i) {
      if (m_valid[i]) return true;
    }

    return false;
  }
  bool isValid(UInt32 block_id) const {
    assert(block_id < SUPERBLOCK_SIZE);

    return m_valid[block_id];
  }

  void compact();
  void compress(UInt32 block_id, UInt32 offset, const Byte* wr_data,
                UInt32 bytes, CacheCompressionCntlr* compress_cntlr);
  void decompress(UInt32 block_id, UInt32 offset, UInt32 bytes,
                  Byte* rd_data) const;

  void evictBlockData(UInt32 block_id, Byte* evict_data, CacheCompressionCntlr* compress_cntlr);
  void insertBlockData(UInt32 block_id, const Byte* wr_data, CacheCompressionCntlr* compress_cntlr);
  bool canInsertBlockData(UInt32 block_id, const Byte* wr_data, CacheCompressionCntlr* compress_cntlr);
  
};
