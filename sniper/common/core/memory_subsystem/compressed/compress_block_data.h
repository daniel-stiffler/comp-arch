#ifndef __COMPRESS_BLOCK_DATA_H__
#define __COMPRESS_BLOCK_DATA_H__

#include <unordered_map>
#include <unordered_set>

#include "fixed_types.h"
#include "superblock_info.h"

constexpr UInt32 MAX_DISH_ENTRIES       = 8;
constexpr UInt32 DISH_GRANULARITY_BYTES = 4;
constexpr UInt32 DISH_BLOCKSIZE_BYTES   = 64;
constexpr UInt32 DISH_POINTERS          = 16;

class CompressBlockData {
 protected:
  UInt32 m_blocksize;
  bool compressed;
  bool m_valid[MAX_SUPERBLOCK_SIZE];

  // CF=1 representation: cache line is stored as a contiguous block of memory
  UInt8 m_data_uncompressed[DISH_BLOCKSIZE_BYTES];

  // CF=2,4 representation: cache lines are stored as sixteen 3-bit pointers to
  // dictionary entries, along with a line valid bit

  // Eight 4-byte dictionary entries
  std::unordered_map<UInt8, UInt32> m_dict;
  // Dictionary entry free and used pointer lists
  std::unordered_set<UInt8> m_free_ptrs;
  std::unordered_set<UInt8> m_used_ptrs;
  // "Pointers" to elements in the dictionary
  UInt8 m_data_compressed[MAX_SUPERBLOCK_SIZE][DISH_POINTERS];

 private:
  bool lookupDictEntry(UInt32 value, UInt8* ptr = nullptr);
  UInt8 insertDictEntry(UInt32 value);
  void removeDictEntry(UInt8 ptr);

 public:
  CompressBlockData(UInt32 blocksize);
  virtual ~CompressBlockData();

  bool isCompressible(UInt32 block_id, UInt32 offset, const Byte* wr_data,
                      UInt32 bytes);

  bool isValid() const {
    for (UInt32 i = 0; i < MAX_SUPERBLOCK_SIZE; ++i) {
      if (m_valid[i]) return true;
    }

    return false;
  }
  bool isValid(UInt32 block_id) const {
    assert(block_id < MAX_SUPERBLOCK_SIZE);

    return m_valid[block_id];
  }

  void compact();
  void compress(UInt32 block_id, UInt32 offset, const Byte* wr_data,
                UInt32 bytes);
  void decompress(UInt32 block_id, Byte* rd_data) const;

  void evictBlockData(UInt32 block_id, Byte* evict_data);
  void insertBlockData(UInt32 block_id, const Byte* wr_data);
}

#endif /* __COMPRESS_BLOCK_DATA_H__ */
