#pragma once

#include <unordered_map>
#include <unordered_set>

#include "compress_utils.h"
#include "superblock_info.h"

class CacheCompressionCntlr;

class BlockData {
 protected:
  const UInt32 m_blocksize;
  const UInt32 m_chunks_per_block;
  DISH::scheme_t m_scheme;
  bool m_valid[SUPERBLOCK_SIZE];

  // Cache lines are stored as contiguous blocks of memory to reduce runtime
  // overhead of decompression
  std::array<std::vector<UInt8>, SUPERBLOCK_SIZE> m_data;

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
  bool lookupDictEntry(UInt32 value, UInt8* ptr = nullptr) const;
  UInt8 insertDictEntry(UInt32 value);
  void removeDictEntry(UInt8 ptr);
  void changeScheme(DISH::scheme_t new_scheme);
  UInt32 getFirstValid() const;

 public:
  BlockData(UInt32 blocksize);
  virtual ~BlockData();

  bool isValid() const;
  bool isValid(UInt32 block_id) const;

  bool isCompressible(UInt32 block_id, UInt32 offset, const Byte* wr_data,
                      UInt32 bytes, DISH::scheme_t try_scheme,
                      CacheCompressionCntlr* compress_cntlr) const;
 private:
  bool isScheme1Compressible(UInt32 block_id, UInt32 offset,
                             const Byte* wr_data, UInt32 bytes,
                             CacheCompressionCntlr* compress_cntlr) const;
  bool isScheme2Compressible(UInt32 block_id, UInt32 offset,
                             const Byte* wr_data, UInt32 bytes,
                             CacheCompressionCntlr* compress_cntlr) const;

private:
  void compact();
  void compactScheme1();
  void compactScheme2();

  void compress(UInt32 block_id, UInt32 offset, const Byte* wr_data,
                UInt32 bytes, CacheCompressionCntlr* compress_cntlr,
                DISH::scheme_t new_scheme);
  void compressScheme1(UInt32 block_id, UInt32 offset, const Byte* wr_data,
                       UInt32 bytes, CacheCompressionCntlr* compress_cntlr);
  void compressScheme2(UInt32 block_id, UInt32 offset, const Byte* wr_data,
                       UInt32 bytes, CacheCompressionCntlr* compress_cntlr);

  DISH::scheme_t getSchemeForWrite(UInt32 block_id, UInt32 offset,
                                   const Byte* wr_data, UInt32 bytes,
                                   CacheCompressionCntlr* compress_cntlr) const;

  DISH::scheme_t getSchemeForInsertion(UInt32 block_id, const Byte* wr_data,
                                       CacheCompressionCntlr* compress_cntlr) const;

public:
  bool canWriteBlockData(UInt32 block_id, UInt32 offset, const Byte* wr_data,
                     UInt32 bytes, CacheCompressionCntlr* compress_cntlr) const;
  void writeBlockData(UInt32 block_id, UInt32 offset, const Byte* wr_data,
                UInt32 bytes, CacheCompressionCntlr* compress_cntlr);

  void readBlockData(UInt32 block_id, UInt32 offset, UInt32 bytes,
                  Byte* rd_data) const;

  bool canInsertBlockData(UInt32 block_id, const Byte* wr_data,
                          CacheCompressionCntlr* compress_cntlr) const;
  void insertBlockData(UInt32 block_id, const Byte* wr_data,
                       CacheCompressionCntlr* compress_cntlr);

  void evictBlockData(UInt32 block_id, Byte* evict_data,
                      CacheCompressionCntlr* compress_cntlr);
};
