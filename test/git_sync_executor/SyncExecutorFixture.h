#pragma once

#include <BookCachePath.h>
#include <BookmarkUtil.h>
#include <GitBlobHasher.h>
#include <Memory.h>
#include <RecordReader.h>
#include <RecordWriter.h>
#include <SyncExecutor.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "MemorySyncStorage.h"

// Card, fakes and helpers shared by the executor suites.

using Sha = std::array<uint8_t, GitBlobHasher::SHA_SIZE>;

inline Sha blobSha(const std::string& content) {
  GitBlobHasher hasher;
  hasher.begin(content.size());
  hasher.update(reinterpret_cast<const uint8_t*>(content.data()), content.size());
  Sha sha{};
  hasher.finish(sha.data());
  return sha;
}

inline std::string hex(const uint8_t* sha) {
  char out[SHA_HEX_LENGTH + 1];
  shaToHex(sha, out);
  return out;
}

struct Entry {
  std::string path;
  std::string content;
};

struct LiveEntry {
  std::string shaHex;
  uint64_t size;
  bool operator==(const LiveEntry& other) const { return shaHex == other.shaHex && size == other.size; }
};

inline void seedFile(MemorySyncStorage& storage, const std::string& path, const std::string& content) {
  const size_t slash = path.rfind('/');
  if (slash > 0) {
    ASSERT_TRUE(storage.mkdirs(path.substr(0, slash).c_str()));
  }
  SyncFile* file = storage.open(path.c_str(), SyncOpenMode::Truncate);
  ASSERT_NE(file, nullptr);
  ASSERT_TRUE(file->write(reinterpret_cast<const uint8_t*>(content.data()), content.size()));
  storage.close(file);
}

inline std::string fileContent(MemorySyncStorage& storage, const std::string& path) {
  const auto snap = storage.snapshot();
  const auto it = snap.files.find(path);
  return it == snap.files.end() ? std::string("<missing>") : it->second;
}

// Writes a record file of the given entries; offsets receives each entry's frame offset.
inline void writeRecords(MemorySyncStorage& storage, const char* path, RecordFileKind kind,
                         const std::vector<Entry>& entries, std::vector<uint32_t>* offsets) {
  ASSERT_TRUE(storage.mkdirs(SYNC_WORK_DIR));
  SyncFile* file = storage.open(path, SyncOpenMode::Truncate);
  ASSERT_NE(file, nullptr);
  auto writer = std::make_unique<RecordWriter>(*file);
  ASSERT_TRUE(writer->begin(kind));
  for (const Entry& entry : entries) {
    const Sha sha = blobSha(entry.content);
    uint32_t offset = 0;
    ASSERT_TRUE(
        writer->put(sha.data(), entry.content.size(), entry.path.data(), entry.path.size(), NO_OFFSET, &offset));
    if (offsets) offsets->push_back(offset);
  }
  ASSERT_TRUE(writer->seal());
  storage.close(file);
}

// Live manifest entries by repository path, journal applied.
inline std::map<std::string, LiveEntry> liveEntries(MemorySyncStorage& storage) {
  std::map<std::string, LiveEntry> live;
  SyncFile* file = storage.open(SYNC_MANIFEST_PATH, SyncOpenMode::Read);
  EXPECT_NE(file, nullptr);
  if (!file) return live;
  auto reader = std::make_unique<RecordReader>(*file, RecordFileKind::Manifest);
  std::map<uint32_t, std::string> byOffset;
  SyncRecord record;
  RecordStatus status;
  while ((status = reader->next(record)) == RecordStatus::Record) {
    const uint32_t target = record.kind == RecordFrameKind::Drop ? record.target : record.replaces;
    if (target != NO_OFFSET) {
      live.erase(byOffset[target]);
    }
    if (record.kind == RecordFrameKind::Entry) {
      byOffset[record.offset] = record.path;
      live[record.path] = {hex(record.sha), record.size};
    }
  }
  EXPECT_EQ(status, RecordStatus::End);
  storage.close(file);
  return live;
}

inline size_t journalFrames(MemorySyncStorage& storage) {
  SyncFile* file = storage.open(SYNC_MANIFEST_PATH, SyncOpenMode::Read);
  EXPECT_NE(file, nullptr);
  if (!file) return 0;
  auto reader = std::make_unique<RecordReader>(*file, RecordFileKind::Manifest);
  SyncRecord record;
  size_t count = 0;
  while (reader->next(record) == RecordStatus::Record) {
    if (record.inJournal) ++count;
  }
  storage.close(file);
  return count;
}

inline SyncOp makeOp(SyncOpKind kind, uint32_t manifestOffset, uint32_t treeOffset) {
  return {kind, SyncSkipReason::None, false, false, manifestOffset, treeOffset};
}

constexpr BookCacheKind CACHE_KINDS[] = {BookCacheKind::Epub, BookCacheKind::Txt, BookCacheKind::Xtc};

// The reader's own derivations of a book's reading state.
inline std::string cacheDir(BookCacheKind kind, const std::string& absPath) {
  return bookCacheDir("/.crosspoint", kind, absPath);
}
inline std::string bookmarkPath(const std::string& absPath) { return BookmarkUtil::getBookmarkPath(absPath); }

// Every cache dir holds one file named after its book, and the bookmark names it too.
inline void seedReadingState(MemorySyncStorage& storage, const std::string& absPath) {
  for (const BookCacheKind kind : CACHE_KINDS) seedFile(storage, cacheDir(kind, absPath) + "/section.bin", absPath);
  seedFile(storage, bookmarkPath(absPath), absPath);
}

inline SyncOp moveOp(uint32_t manifestOffset, uint32_t treeOffset, bool sourceExists, bool targetExists) {
  return {SyncOpKind::Move, SyncSkipReason::None, sourceExists, targetExists, manifestOffset, treeOffset};
}

class SyncExecutorTest : public ::testing::Test {
 protected:
  enum class FetchMode : uint8_t { Serve, WrongBytes, ShortBody, ExtraBytes, FailMidStream };

  SyncExecutorCallbacks callbacks() { return {this, &fetchFake, &hashFake, &clearFake, &movedFake, &progressFake}; }

  std::unique_ptr<SyncExecutor> makeExecutor() { return makeUniqueNoThrow<SyncExecutor>(storage, callbacks()); }

  // Serves content by SHA in 3-byte chunks, as the downloader streams it.
  void serve(const std::string& content) { blobs[hex(blobSha(content).data())] = content; }

  static bool fetchFake(void* ctx, const SyncFetchRequest& request, const SyncChunkSink& sink) {
    auto* self = static_cast<SyncExecutorTest*>(ctx);
    const std::string key = hex(request.sha);
    self->fetches[key]++;
    const auto it = self->blobs.find(key);
    if (it == self->blobs.end()) return false;
    std::string body = it->second;
    switch (self->fetchMode) {
      case FetchMode::Serve:
      case FetchMode::FailMidStream:
        break;
      case FetchMode::WrongBytes:
        body[0] ^= 1;
        break;
      case FetchMode::ShortBody:
        body.pop_back();
        break;
      case FetchMode::ExtraBytes:
        body += "!";
        break;
    }
    const size_t stopAt = self->fetchMode == FetchMode::FailMidStream ? body.size() / 2 : body.size();
    for (size_t pos = 0; pos < stopAt; pos += 3) {
      const size_t len = std::min<size_t>(3, stopAt - pos);
      if (!sink.write(sink.ctx, reinterpret_cast<const uint8_t*>(body.data()) + pos, len)) return false;
    }
    return self->fetchMode != FetchMode::FailMidStream;
  }

  // Reads the card the way the device glue does, into a blob hasher.
  static bool hashFake(void* ctx, const char* absPath, uint64_t expectedSize, uint8_t outSha[GitBlobHasher::SHA_SIZE]) {
    auto* self = static_cast<SyncExecutorTest*>(ctx);
    self->hashed.push_back(absPath);
    if (absPath == self->unhashable) return false;
    SyncFile* file = self->storage.open(absPath, SyncOpenMode::Read);
    if (!file) return false;
    GitBlobHasher hasher;
    hasher.begin(expectedSize);
    uint8_t buf[5];
    int n;
    while ((n = file->read(buf, sizeof(buf))) > 0) hasher.update(buf, static_cast<size_t>(n));
    self->storage.close(file);
    return hasher.finish(outSha);
  }

  struct Progress {
    uint32_t done;
    uint32_t total;
    SyncOpKind kind;
    std::string repoPath;
    bool operator==(const Progress& o) const {
      return done == o.done && total == o.total && kind == o.kind && repoPath == o.repoPath;
    }
  };

  static void progressFake(void* ctx, const SyncProgress& progress) {
    auto* self = static_cast<SyncExecutorTest*>(ctx);
    self->progress.push_back({progress.done, progress.total, progress.kind, progress.repoPath});
  }

  // Like the device's clearBookCache: removes, through the card, the cache dir of the kind its extension names.
  static bool clearFake(void* ctx, const char* absPath) {
    auto* self = static_cast<SyncExecutorTest*>(ctx);
    self->cleared.push_back(absPath);
    if (!self->clearResult) return false;
    const std::string path = absPath;
    const auto endsWith = [&](const std::string& ext) {
      return path.size() >= ext.size() && path.compare(path.size() - ext.size(), ext.size(), ext) == 0;
    };
    BookCacheKind kind;
    if (endsWith(".epub")) {
      kind = BookCacheKind::Epub;
    } else if (endsWith(".txt")) {
      kind = BookCacheKind::Txt;
    } else if (endsWith(".xtc")) {
      kind = BookCacheKind::Xtc;
    } else {
      return true;
    }
    const std::string dir = cacheDir(kind, path);
    if (!self->storage.exists(dir.c_str())) return true;
    for (const auto& [file, content] : self->storage.snapshot().files) {
      if (file.compare(0, dir.size() + 1, dir + "/") == 0 && !self->storage.remove(file.c_str())) return false;
    }
    return self->storage.rmdir(dir.c_str());
  }

  // Like RECENT_BOOKS.updatePath and APP_STATE.openEpubPath: idempotent, a no-op for an unknown path.
  static bool movedFake(void* ctx, const char* oldAbsPath, const char* newAbsPath) {
    auto* self = static_cast<SyncExecutorTest*>(ctx);
    self->moves.emplace_back(oldAbsPath, newAbsPath);
    std::replace(self->recentBooks.begin(), self->recentBooks.end(), std::string(oldAbsPath), std::string(newAbsPath));
    if (self->openBook == oldAbsPath) self->openBook = newAbsPath;
    return self->movedResult;
  }

  // Prepares an empty card's manifest, or the one already seeded, then writes the tree and begins.
  std::unique_ptr<SyncExecutor> begin(const std::vector<Entry>& tree) {
    auto executor = makeExecutor();
    EXPECT_EQ(executor->prepare().status, SyncRunStatus::Ok);
    treeOffsets.clear();
    writeRecords(storage, SYNC_TREE_PATH, RecordFileKind::Tree, tree, &treeOffsets);
    for (const Entry& entry : tree) serve(entry.content);
    EXPECT_TRUE(executor->begin());
    return executor;
  }

  MemorySyncStorage storage;
  std::map<std::string, std::string> blobs;
  std::map<std::string, int> fetches;
  FetchMode fetchMode = FetchMode::Serve;
  std::vector<std::string> cleared;
  bool clearResult = true;
  std::vector<std::string> hashed;
  // The hash callback answers "unknown" for this path.
  std::string unhashable;
  std::vector<uint32_t> treeOffsets;
  std::vector<Progress> progress;
  std::vector<std::pair<std::string, std::string>> moves;
  bool movedResult = true;
  std::vector<std::string> recentBooks;
  std::string openBook;
};
