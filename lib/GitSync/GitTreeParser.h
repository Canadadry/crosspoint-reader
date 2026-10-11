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
  // Ignores every byte after an error, an abort, or once the top-level object has closed.
  void feed(const char* data, size_t len);

  // Top-level object closed, valid tree `sha`, `tree` array closed, boolean `truncated`, no error, not aborted.
  bool isComplete() const;
  bool isTruncated() const { return truncated; }
  bool hasError() const;
  bool wasAborted() const { return aborted; }
  // Meaningful only when isComplete().
  const uint8_t* treeSha() const { return sha; }
  size_t emittedCount() const { return emitted; }
  // Blobs whose path overflowed the JSON token buffer, so arrived with no value.
  size_t skippedLongPathCount() const { return skippedLongPath; }
  // Blobs whose decoded path holds a backslash: `\uXXXX` is not decoded, so the name would be wrong.
  size_t skippedEscapedPathCount() const { return skippedEscapedPath; }

 private:
  enum class PendingKey : uint8_t { None, Sha, Tree, Truncated, EntryPath, EntryType, EntrySha, EntrySize };
  // Ignored: `tree` (directory) and `commit` (submodule). Invalid: missing, not a string, or unknown.
  enum class EntryType : uint8_t { Invalid, Blob, Ignored };

  static void sOnKey(void* ctx, const char* key, size_t len);
  static void sOnString(void* ctx, const char* value, size_t len);
  static void sOnNumber(void* ctx, const char* value, size_t len);
  static void sOnBool(void* ctx, bool value);
  static void sOnNull(void* ctx);
  static void sOnObjectStart(void* ctx);
  static void sOnObjectEnd(void* ctx);
  static void sOnArrayStart(void* ctx);
  static void sOnArrayEnd(void* ctx);

  bool stopped() const { return error || aborted || rootClosed; }
  bool atTreeElement() const { return inTreeArray && depth == 2; }
  bool inEntry() const { return inTreeArray && depth == 3; }
  // Returns the key the value belongs to and clears it, so a value is never attributed twice.
  PendingKey takePendingKey();
  // A wrong-typed value fails a top-level key or a `tree` element; on an entry key it leaves the field unread.
  void rejectValue(PendingKey key);
  void onKey(const char* key, size_t len);
  void onString(const char* value, size_t len);
  void onNumber(const char* value, size_t len);
  void onBool(bool value);
  void onScalar();
  void onContainerStart(bool isArray);
  void onContainerEnd();
  void beginEntry();
  void endEntry();
  void skipEntry(const char* reason, size_t& counter);

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
  bool aborted;
  uint8_t sha[GitBlobHasher::SHA_SIZE];
  size_t emitted;
  size_t skippedLongPath;
  size_t skippedEscapedPath;

  // Fields of the entry being read, acted on at its object end.
  EntryType entryType;
  bool entryPathRead;
  bool entryPathOverflowed;
  bool entryShaRead;
  bool entrySizeRead;
  size_t entryPathLen;
  uint64_t entrySize;
  uint8_t entrySha[GitBlobHasher::SHA_SIZE];
  // Sized like the JSON token buffer, so every path it delivers fits with its terminator.
  char entryPath[StreamingJsonParser::TOKEN_BUF_SIZE];
};
