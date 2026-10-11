#pragma once

#include <cstddef>
#include <cstdint>

#include "Crc32.h"
#include "GitBlobHasher.h"
#include "SyncPath.h"

// Sync record file, format version 1: an 8-byte header, then checksummed frames.
// All integers are little-endian, assembled byte by byte (no unaligned load on RISC-V).
//   header: magic "GSRF", version u8, kind u8, reserved u16 = 0
//   frame:  kind u8, body length u16, CRC-32 u32 over kind + length + body, then the body
//   'P' entry: sha[20], size u64, replaces u32, path bytes (1..MAX_SYNC_PATH_BYTES, no terminator)
//   'D' drop:  target u32
//   'S' seal:  count u32, the number of entry frames before it
// An offset is the position of a frame's first byte in the file.
constexpr uint8_t RECORD_FILE_VERSION = 1;
constexpr uint8_t RECORD_FILE_MAGIC[4] = {'G', 'S', 'R', 'F'};
constexpr size_t RECORD_HEADER_SIZE = 8;
constexpr size_t FRAME_HEADER_SIZE = 7;
// Entry body field positions; the path starts at ENTRY_FIXED_BODY_SIZE.
constexpr size_t ENTRY_SIZE_FIELD = GitBlobHasher::SHA_SIZE;
constexpr size_t ENTRY_REPLACES_FIELD = ENTRY_SIZE_FIELD + 8;
constexpr size_t ENTRY_FIXED_BODY_SIZE = ENTRY_REPLACES_FIELD + 4;
constexpr size_t OFFSET_BODY_SIZE = 4;
constexpr size_t MAX_FRAME_SIZE = FRAME_HEADER_SIZE + ENTRY_FIXED_BODY_SIZE + MAX_SYNC_PATH_BYTES;
constexpr uint32_t NO_OFFSET = 0xFFFFFFFFu;

enum class RecordFileKind : uint8_t { Tree = 1, Manifest = 2 };

enum class RecordFrameKind : uint8_t { Entry = 'P', Drop = 'D', Seal = 'S' };

enum class RecordStatus : uint8_t { Record, End, Truncated, Corrupt, WrongVersion, IoError };

// An entry or a drop as read. path is '\0'-terminated and valid until the reader's next call.
struct SyncRecord {
  RecordFrameKind kind;
  uint32_t offset;
  bool inJournal;
  // Entry only.
  uint8_t sha[GitBlobHasher::SHA_SIZE];
  uint64_t size;
  uint32_t replaces;
  const char* path;
  size_t pathLen;
  // Drop only.
  uint32_t target;
};

inline void recordStoreLe(uint8_t* out, uint64_t value, size_t bytes) {
  for (size_t i = 0; i < bytes; ++i) {
    out[i] = static_cast<uint8_t>(value >> (8 * i));
  }
}

inline uint64_t recordLoadLe(const uint8_t* in, size_t bytes) {
  uint64_t value = 0;
  for (size_t i = 0; i < bytes; ++i) {
    value |= static_cast<uint64_t>(in[i]) << (8 * i);
  }
  return value;
}

// frame holds the frame header followed by bodyLen body bytes; the CRC field itself is skipped.
inline uint32_t recordFrameCrc(const uint8_t* frame, size_t bodyLen) {
  const uint32_t crc = Crc32::update(0, frame, 3);
  return Crc32::update(crc, frame + FRAME_HEADER_SIZE, bodyLen);
}
