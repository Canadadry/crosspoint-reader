#pragma once

#include <cstddef>
#include <cstdint>

#include "RecordStream.h"

// On-card paths of the sync. manifest.new is not a *.tmp file: prepare() may need to promote it.
constexpr char SYNC_WORK_DIR[] = "/.crosspoint/gitsync";
constexpr char SYNC_MANIFEST_PATH[] = "/.crosspoint/gitsync/manifest.bin";
constexpr char SYNC_MANIFEST_NEW_PATH[] = "/.crosspoint/gitsync/manifest.new";
constexpr char SYNC_DOWNLOAD_TMP_PATH[] = "/.crosspoint/gitsync/download.tmp";
constexpr char SYNC_TREE_PATH[] = "/.crosspoint/gitsync/tree.tmp";

enum class SyncOpenMode : uint8_t {
  Read,      // existing file, read-only
  Truncate,  // created or truncated, write-only; the parent must exist
  Append,    // existing file, reads anywhere through seek, every write lands at the end
};

class SyncFile : public RecordSource, public RecordSink {
 public:
  // False when buffered bytes could not reach the card.
  virtual bool flush() = 0;
  virtual uint64_t size() = 0;
};

// The card as the sync executor sees it, shaped like HalStorage so the device adapter only forwards.
// Contract:
// - paths are absolute and '\0'-terminated UTF-8;
// - lookup ignores letter case like FAT long names;
// - a name holding a FAT-forbidden character is refused by every call that would create it;
// - rename fails when the destination exists under any letter case;
// - an open file cannot be renamed or removed;
// - mutating calls are open with Truncate, write, rename, remove, mkdirs and rmdir.
class SyncStorage {
 public:
  // Manifest, tree and download temp.
  static constexpr size_t MAX_OPEN_FILES = 3;

  virtual ~SyncStorage() = default;

  // Storage-owned handle, released by close(); nullptr on failure or when MAX_OPEN_FILES are open.
  virtual SyncFile* open(const char* path, SyncOpenMode mode) = 0;
  virtual void close(SyncFile* file) = 0;

  virtual bool exists(const char* path) = 0;
  // False when missing or a directory.
  virtual bool fileSize(const char* path, uint64_t& out) = 0;
  // False when missing, not a directory, or holding anything.
  virtual bool isEmptyDirectory(const char* path) = 0;

  // Creates the directory and its missing parents; true when it already is a directory.
  virtual bool mkdirs(const char* path) = 0;
  // A file only, never a directory.
  virtual bool remove(const char* path) = 0;
  // An empty directory only.
  virtual bool rmdir(const char* path) = 0;
  // A file or a directory; never overwrites.
  virtual bool rename(const char* from, const char* to) = 0;
};
