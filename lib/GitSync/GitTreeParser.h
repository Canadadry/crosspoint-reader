#pragma once

#include <cstddef>
#include <cstdint>

#include "GitBlobHasher.h"
#include "StreamingJsonParser.h"

// One blob of the trees response. Valid only during the callback.
struct GitTreeRecord {
  const char* path;  // '\0'-terminated
  size_t pathLen;    // bytes, terminator excluded
  uint8_t sha[GitBlobHasher::SHA_SIZE];
  uint64_t size;
};

// Returning false stops the parser.
using GitTreeRecordFn = bool (*)(void* ctx, const GitTreeRecord& record);

// Streams GitHub's `git/trees/{branch}?recursive=1` body, fed in chunks of any size.
// The caller uses the records only when isComplete() && !isTruncated(): `truncated` arrives after
// every record, so anything else must abort the sync with no change.
// About 1.1 KB and no heap: hold it as a member or through makeUniqueNoThrow, never as a local.
class GitTreeParser {
 public:
  GitTreeParser(void* ctx, GitTreeRecordFn onRecord);

  GitTreeParser(const GitTreeParser&) = delete;
  GitTreeParser& operator=(const GitTreeParser&) = delete;

  void reset();
  // Ignores every byte after an error or once the top-level object has closed.
  void feed(const char* data, size_t len);

  // Top-level object closed, valid tree `sha`, `tree` array closed, boolean `truncated`, no error.
  bool isComplete() const;
  bool isTruncated() const { return truncated; }
  bool hasError() const;
  // Meaningful only when isComplete().
  const uint8_t* treeSha() const { return sha; }

 private:
  enum class PendingKey : uint8_t { None, Sha, Tree, Truncated };

  static void sOnKey(void* ctx, const char* key, size_t len);
  static void sOnString(void* ctx, const char* value, size_t len);
  static void sOnNumber(void* ctx, const char* value, size_t len);
  static void sOnBool(void* ctx, bool value);
  static void sOnNull(void* ctx);
  static void sOnObjectStart(void* ctx);
  static void sOnObjectEnd(void* ctx);
  static void sOnArrayStart(void* ctx);
  static void sOnArrayEnd(void* ctx);

  bool stopped() const { return error || rootClosed; }
  // Returns the key the value belongs to and clears it, so a value is never attributed twice.
  PendingKey takePendingKey();
  void onKey(const char* key, size_t len);
  void onString(const char* value, size_t len);
  void onScalar();
  void onBool(bool value);
  void onContainerStart(bool isArray);
  void onContainerEnd();

  StreamingJsonParser json;
  void* recordCtx;
  GitTreeRecordFn onRecord;

  // 1 = top-level object, 2 = `tree` array, 3 = entry object.
  uint8_t depth;
  PendingKey pendingKey;
  bool inTreeArray;
  bool shaRead;
  bool treeClosed;
  bool truncatedRead;
  bool truncated;
  bool rootClosed;
  bool error;
  uint8_t sha[GitBlobHasher::SHA_SIZE];
};
