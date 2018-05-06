#include "compress_utils.h"

#include <sstream>
#include <iostream>

std::string printBytes(const Byte* data, UInt32 size) {
  std::stringstream info_ss;

  info_ss << "( " << std::hex; 
  for (UInt32 i = 0; i < size; ++i) {
    info_ss << static_cast<UInt32>(data[i]) << " ";
  }
  info_ss << " )";

  return info_ss.str();
}

std::string printChunks(const UInt32* data, UInt32 size) {
  std::stringstream info_ss;

  info_ss << "( " << std::hex; 
  for (UInt32 i = 0; i < size; ++i) {
    info_ss << data[i] << " ";
  }
  info_ss << " )";

  return info_ss.str();
}
