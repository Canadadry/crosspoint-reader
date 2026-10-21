#pragma once

#include <cstddef>
#include <cstdint>

// IEEE 802.3 CRC-32 (reflected, polynomial 0xEDB88320), as zlib's crc32().
class Crc32 {
 public:
  // Chains like zlib: start from 0, pass each result back in.
  static uint32_t update(uint32_t crc, const uint8_t* data, size_t len);
  static uint32_t compute(const uint8_t* data, size_t len) { return update(0, data, len); }
};
