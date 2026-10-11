#include "SyncPathFold.h"

#include <cstdint>

namespace {

// Port of toUpcase() from SdFat 2.3.1 src/common/upcase.cpp, Copyright (c) 2011-2025 Bill Greiman, MIT License.
struct UpcaseRange {
  uint16_t key;
  int8_t off;
  uint8_t count;
};

struct UpcasePair {
  uint16_t key;
  uint16_t val;
};

constexpr UpcaseRange UPCASE_RANGES[] = {
    {0X0061, -32, 26}, {0X00E0, -32, 23}, {0X00F8, -32, 7},  {0X0100, 1, 48},   {0X0132, 1, 6},    {0X0139, 1, 16},
    {0X014A, 1, 46},   {0X0179, 1, 6},    {0X0182, 1, 4},    {0X01A0, 1, 6},    {0X01B3, 1, 4},    {0X01CD, 1, 16},
    {0X01DE, 1, 18},   {0X01F8, 1, 40},   {0X0222, 1, 18},   {0X0246, 1, 10},   {0X03AD, -37, 3},  {0X03B1, -32, 17},
    {0X03C3, -32, 9},  {0X03D8, 1, 24},   {0X0430, -32, 32}, {0X0450, -80, 16}, {0X0460, 1, 34},   {0X048A, 1, 54},
    {0X04C1, 1, 14},   {0X04D0, 1, 68},   {0X0561, -48, 38}, {0X1E00, 1, 150},  {0X1EA0, 1, 90},   {0X1F00, 8, 8},
    {0X1F10, 8, 6},    {0X1F20, 8, 8},    {0X1F30, 8, 8},    {0X1F40, 8, 6},    {0X1F60, 8, 8},    {0X1F70, 74, 2},
    {0X1F72, 86, 4},   {0X1F76, 100, 2},  {0X1F7A, 112, 2},  {0X1F7C, 126, 2},  {0X1F80, 8, 8},    {0X1F90, 8, 8},
    {0X1FA0, 8, 8},    {0X1FB0, 8, 2},    {0X1FD0, 8, 2},    {0X1FE0, 8, 2},    {0X2170, -16, 16}, {0X24D0, -26, 26},
    {0X2C30, -48, 47}, {0X2C67, 1, 6},    {0X2C80, 1, 100},  {0X2D00, 0, 38},   {0XFF41, -32, 26},
};

constexpr UpcasePair UPCASE_PAIRS[] = {
    {0X00FF, 0X0178}, {0X0180, 0X0243}, {0X0188, 0X0187}, {0X018C, 0X018B}, {0X0192, 0X0191}, {0X0195, 0X01F6},
    {0X0199, 0X0198}, {0X019A, 0X023D}, {0X019E, 0X0220}, {0X01A8, 0X01A7}, {0X01AD, 0X01AC}, {0X01B0, 0X01AF},
    {0X01B9, 0X01B8}, {0X01BD, 0X01BC}, {0X01BF, 0X01F7}, {0X01C6, 0X01C4}, {0X01C9, 0X01C7}, {0X01CC, 0X01CA},
    {0X01DD, 0X018E}, {0X01F3, 0X01F1}, {0X01F5, 0X01F4}, {0X023A, 0X2C65}, {0X023C, 0X023B}, {0X023E, 0X2C66},
    {0X0242, 0X0241}, {0X0253, 0X0181}, {0X0254, 0X0186}, {0X0256, 0X0189}, {0X0257, 0X018A}, {0X0259, 0X018F},
    {0X025B, 0X0190}, {0X0260, 0X0193}, {0X0263, 0X0194}, {0X0268, 0X0197}, {0X0269, 0X0196}, {0X026B, 0X2C62},
    {0X026F, 0X019C}, {0X0272, 0X019D}, {0X0275, 0X019F}, {0X027D, 0X2C64}, {0X0280, 0X01A6}, {0X0283, 0X01A9},
    {0X0288, 0X01AE}, {0X0289, 0X0244}, {0X028A, 0X01B1}, {0X028B, 0X01B2}, {0X028C, 0X0245}, {0X0292, 0X01B7},
    {0X037B, 0X03FD}, {0X037C, 0X03FE}, {0X037D, 0X03FF}, {0X03AC, 0X0386}, {0X03C2, 0X03A3}, {0X03CC, 0X038C},
    {0X03CD, 0X038E}, {0X03CE, 0X038F}, {0X03F2, 0X03F9}, {0X03F8, 0X03F7}, {0X03FB, 0X03FA}, {0X04CF, 0X04C0},
    {0X1D7D, 0X2C63}, {0X1F51, 0X1F59}, {0X1F53, 0X1F5B}, {0X1F55, 0X1F5D}, {0X1F57, 0X1F5F}, {0X1F78, 0X1FF8},
    {0X1F79, 0X1FF9}, {0X1FB3, 0X1FBC}, {0X1FCC, 0X1FC3}, {0X1FE5, 0X1FEC}, {0X1FFC, 0X1FF3}, {0X214E, 0X2132},
    {0X2184, 0X2183}, {0X2C61, 0X2C60}, {0X2C76, 0X2C75},
};

// The last entry whose key is <= key, or the first entry.
template <typename Entry, size_t N>
const Entry& searchLastNotAbove(const Entry (&table)[N], uint16_t key) {
  size_t left = 0;
  size_t right = N;
  while (right - left > 1) {
    const size_t mid = left + (right - left) / 2;
    if (table[mid].key <= key) {
      left = mid;
    } else {
      right = mid;
    }
  }
  return table[left];
}

uint16_t toUpcase(uint16_t chr) {
  if (chr < 127) return chr - ('a' <= chr && chr <= 'z' ? 'a' - 'A' : 0);

  const UpcaseRange& range = searchLastNotAbove(UPCASE_RANGES, chr);
  if (range.key <= chr && chr - range.key < range.count) {
    if (range.off == 1) return chr - ((chr - range.key) & 1);
    return chr + (range.off ? range.off : -0x1C60);
  }
  const UpcasePair& pair = searchLastNotAbove(UPCASE_PAIRS, chr);
  return pair.key == chr ? pair.val : chr;
}

// A byte outside any valid UTF-8 sequence, tagged so it never equals a code point.
constexpr uint32_t INVALID_BYTE_TAG = 0x80000000u;

// Decodes one strict UTF-8 code point at path[i] and folds it; advances i.
uint32_t nextFoldedUnit(const char* path, size_t len, size_t& i) {
  const auto lead = static_cast<uint8_t>(path[i]);
  if (lead < 0x80) {
    i++;
    return toUpcase(lead);
  }

  size_t extra = 0;
  uint8_t secondMin = 0x80;
  uint8_t secondMax = 0xBF;
  if (lead >= 0xC2 && lead <= 0xDF) {
    extra = 1;
  } else if (lead >= 0xE0 && lead <= 0xEF) {
    extra = 2;
    if (lead == 0xE0) secondMin = 0xA0;  // overlong
    if (lead == 0xED) secondMax = 0x9F;  // surrogate
  } else if (lead >= 0xF0 && lead <= 0xF4) {
    extra = 3;
    if (lead == 0xF0) secondMin = 0x90;  // overlong
    if (lead == 0xF4) secondMax = 0x8F;  // above U+10FFFF
  }

  bool valid = extra > 0 && len - i > extra;
  if (valid) {
    const auto second = static_cast<uint8_t>(path[i + 1]);
    valid = second >= secondMin && second <= secondMax;
  }
  uint32_t cp = lead & (0x7F >> (extra + 1));
  for (size_t k = 1; valid && k <= extra; k++) {
    const auto cont = static_cast<uint8_t>(path[i + k]);
    valid = (cont & 0xC0) == 0x80;
    cp = (cp << 6) | (cont & 0x3F);
  }
  if (!valid) {
    i++;
    return INVALID_BYTE_TAG | lead;
  }
  i += extra + 1;
  return cp <= 0xFFFF ? toUpcase(static_cast<uint16_t>(cp)) : cp;
}

}  // namespace

bool syncPathsEqualFolded(const char* a, size_t aLen, const char* b, size_t bLen) {
  size_t i = 0;
  size_t j = 0;
  while (i < aLen && j < bLen) {
    if (nextFoldedUnit(a, aLen, i) != nextFoldedUnit(b, bLen, j)) return false;
  }
  return i == aLen && j == bLen;
}

uint32_t syncPathKey(const char* path, size_t len) {
  constexpr uint32_t FNV_OFFSET_BASIS = 2166136261u;
  constexpr uint32_t FNV_PRIME = 16777619u;
  uint32_t hash = FNV_OFFSET_BASIS;
  size_t i = 0;
  while (i < len) {
    const uint32_t unit = nextFoldedUnit(path, len, i);
    for (int shift = 0; shift < 32; shift += 8) {
      hash ^= (unit >> shift) & 0xFF;
      hash *= FNV_PRIME;
    }
  }
  return hash;
}

bool isFatSafeName(const char* path, size_t len) {
  for (size_t i = 0; i < len; i++) {
    switch (path[i]) {
      case ':':
      case '*':
      case '?':
      case '"':
      case '<':
      case '>':
      case '|':
      case '\\':
        return false;
      default:
        break;
    }
  }
  return true;
}
