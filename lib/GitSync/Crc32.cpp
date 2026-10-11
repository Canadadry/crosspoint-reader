#include "Crc32.h"

#include <array>

namespace {

constexpr uint32_t POLYNOMIAL = 0xEDB88320u;

constexpr std::array<uint32_t, 256> makeTable() {
  std::array<uint32_t, 256> table{};
  for (uint32_t i = 0; i < 256; ++i) {
    uint32_t value = i;
    for (int bit = 0; bit < 8; ++bit) {
      value = (value & 1u) ? (value >> 1) ^ POLYNOMIAL : value >> 1;
    }
    table[i] = value;
  }
  return table;
}

constexpr std::array<uint32_t, 256> TABLE = makeTable();

}  // namespace

uint32_t Crc32::update(uint32_t crc, const uint8_t* data, size_t len) {
  crc = ~crc;
  for (size_t i = 0; i < len; ++i) {
    crc = TABLE[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
  }
  return ~crc;
}
