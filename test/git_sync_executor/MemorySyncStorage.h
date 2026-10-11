#pragma once

#include <SyncPathFold.h>
#include <SyncStorage.h>

#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

// Host-only FAT-like card for executor tests. Names keep the case they were created with.
class MemorySyncStorage : public SyncStorage {
 public:
  // Bytes the failing write keeps: none, or len / 2.
  enum class Tear : uint8_t { None, Half };

  enum class CallKind : uint8_t { Truncate, Write, Rename, Remove, Mkdirs, Rmdir };

  struct Call {
    CallKind kind;
    std::string path;
    std::string to;  // rename destination
    bool ok;
    bool operator==(const Call& other) const {
      return kind == other.kind && path == other.path && to == other.to && ok == other.ok;
    }
  };

  // Files with their contents and directories, by stored name.
  struct Snapshot {
    std::map<std::string, std::string> files;
    std::set<std::string> directories;
    bool operator==(const Snapshot& other) const { return files == other.files && directories == other.directories; }
  };

  Snapshot snapshot() const {
    Snapshot snap;
    for (const auto& [path, node] : nodes) {
      if (node.isDir) {
        snap.directories.insert(path);
      } else {
        snap.files[path] = node.content;
      }
    }
    return snap;
  }

  // The n-th mutating call (1-based, counting every call so far) fails, and so does every later one; reads keep
  // working.
  void failAt(size_t n, Tear tearPolicy) {
    failAtCall = n;
    tear = tearPolicy;
  }
  void clearFault() { failAtCall = 0; }

  // Forgets the calls made so far, typically those that seeded the card.
  void resetCounters() {
    log.clear();
    written = 0;
  }

  const std::vector<Call>& calls() const { return log; }
  size_t mutatingCalls() const { return log.size(); }
  uint64_t bytesWritten() const { return written; }
  size_t openFiles() const { return handles.size(); }

  SyncFile* open(const char* path, SyncOpenMode mode) override {
    if (mode == SyncOpenMode::Truncate) {
      return record(CallKind::Truncate, path, "", faulted() ? nullptr : openTruncate(path));
    }
    Node* node = find(path);
    if (!node || node->isDir) return nullptr;
    return addHandle(*node, path, mode);
  }

  void close(SyncFile* file) override {
    for (auto it = handles.begin(); it != handles.end(); ++it) {
      if (it->get() == file) {
        handles.erase(it);
        return;
      }
    }
  }

  bool exists(const char* path) override { return find(path) != nullptr; }

  bool fileSize(const char* path, uint64_t& out) override {
    const Node* node = find(path);
    if (!node || node->isDir) return false;
    out = node->content.size();
    return true;
  }

  bool isEmptyDirectory(const char* path) override {
    auto it = findEntry(path);
    return it != nodes.end() && it->second.isDir && !hasChildren(it->first);
  }

  bool mkdirs(const char* path) override { return record(CallKind::Mkdirs, path, "", !faulted() && doMkdirs(path)); }
  bool remove(const char* path) override { return record(CallKind::Remove, path, "", !faulted() && doRemove(path)); }
  bool rmdir(const char* path) override { return record(CallKind::Rmdir, path, "", !faulted() && doRmdir(path)); }
  bool rename(const char* from, const char* to) override {
    return record(CallKind::Rename, from, to, !faulted() && doRename(from, to));
  }

 private:
  struct Node {
    bool isDir = false;
    int openCount = 0;
    std::string content;
  };

  class File : public SyncFile {
   public:
    File(MemorySyncStorage& owner, Node& node, const char* path, SyncOpenMode mode)
        : owner(owner), node(node), path(path), mode(mode) {
      node.openCount++;
    }
    ~File() override { node.openCount--; }

    int read(uint8_t* buf, size_t len) override {
      if (mode == SyncOpenMode::Truncate) return -1;
      size_t count = node.content.size() - cursor;
      if (count > len) count = len;
      memcpy(buf, node.content.data() + cursor, count);
      cursor += count;
      return static_cast<int>(count);
    }

    bool seek(uint32_t offset) override {
      if (offset > node.content.size()) return false;
      cursor = offset;
      return true;
    }

    bool write(const uint8_t* buf, size_t len) override {
      return owner.record(CallKind::Write, path.c_str(), "",
                          mode != SyncOpenMode::Read && owner.doWrite(node, buf, len));
    }

    bool flush() override { return true; }
    uint64_t size() override { return node.content.size(); }

   private:
    MemorySyncStorage& owner;
    Node& node;
    std::string path;
    SyncOpenMode mode;
    size_t cursor = 0;
  };

  template <typename T>
  T record(CallKind kind, const char* path, const char* to, T result) {
    log.push_back({kind, path, to, static_cast<bool>(result)});
    return result;
  }

  SyncFile* addHandle(Node& node, const char* path, SyncOpenMode mode) {
    if (handles.size() >= MAX_OPEN_FILES) return nullptr;
    handles.push_back(std::make_unique<File>(*this, node, path, mode));
    return handles.back().get();
  }

  SyncFile* openTruncate(const char* path) {
    if (handles.size() >= MAX_OPEN_FILES) return nullptr;
    Node* node = find(path);
    if (node && node->isDir) return nullptr;
    if (!node) node = create(path, false);
    if (!node) return nullptr;
    node->content.clear();
    return addHandle(*node, path, SyncOpenMode::Truncate);
  }

  // Whether the call about to be recorded falls in the fault.
  bool faulted() const { return failAtCall != 0 && log.size() + 1 >= failAtCall; }

  bool doWrite(Node& node, const uint8_t* buf, size_t len) {
    if (faulted()) {
      if (log.size() + 1 == failAtCall && tear == Tear::Half) append(node, buf, len / 2);
      return false;
    }
    append(node, buf, len);
    return true;
  }

  void append(Node& node, const uint8_t* buf, size_t len) {
    node.content.append(reinterpret_cast<const char*>(buf), len);
    written += len;
  }

  bool doMkdirs(const std::string& path) {
    if (!isWellFormed(path)) return false;
    for (size_t slash = path.find('/', 1);; slash = path.find('/', slash + 1)) {
      const std::string prefix = path.substr(0, slash);
      Node* node = find(prefix);
      if (node && !node->isDir) return false;
      if (!node && !create(prefix, true)) return false;
      if (slash == std::string::npos) return true;
    }
  }

  bool doRemove(const std::string& path) {
    auto it = findEntry(path);
    if (it == nodes.end() || it->second.isDir || it->second.openCount > 0) return false;
    nodes.erase(it);
    return true;
  }

  bool doRmdir(const std::string& path) {
    auto it = findEntry(path);
    if (it == nodes.end() || !it->second.isDir || hasChildren(it->first)) return false;
    nodes.erase(it);
    return true;
  }

  bool doRename(const std::string& from, const std::string& to) {
    auto source = findEntry(from);
    std::string key;
    if (source == nodes.end() || find(to) || !newKey(to, key)) return false;
    const std::string oldKey = source->first;
    const std::string oldPrefix = oldKey + "/";
    if (key.compare(0, oldPrefix.size(), oldPrefix) == 0) return false;
    std::vector<std::string> moved;
    for (auto it = source; it != nodes.end(); ++it) {
      if (it->first != oldKey && it->first.compare(0, oldPrefix.size(), oldPrefix) != 0) continue;
      if (it->second.openCount > 0) return false;
      moved.push_back(it->first);
    }
    for (const std::string& oldPath : moved) {
      auto entry = nodes.extract(oldPath);
      entry.key() = key + oldPath.substr(oldKey.size());
      nodes.insert(std::move(entry));
    }
    return true;
  }

  static bool isWellFormed(const std::string& path) {
    if (path.size() < 2 || path[0] != '/' || path.back() == '/') return false;
    return path.find("//") == std::string::npos;
  }

  std::map<std::string, Node>::iterator findEntry(const std::string& path) {
    auto it = nodes.find(path);
    if (it != nodes.end()) return it;
    for (it = nodes.begin(); it != nodes.end(); ++it) {
      if (syncPathsEqualFolded(it->first.data(), it->first.size(), path.data(), path.size())) return it;
    }
    return nodes.end();
  }

  Node* find(const std::string& path) {
    auto it = findEntry(path);
    return it == nodes.end() ? nullptr : &it->second;
  }

  bool hasChildren(const std::string& key) const {
    const std::string prefix = key + "/";
    auto it = nodes.lower_bound(prefix);
    return it != nodes.end() && it->first.compare(0, prefix.size(), prefix) == 0;
  }

  // A new name under its parent's stored case; false when the name is refused or the parent is not a directory.
  bool newKey(const std::string& path, std::string& key) {
    if (!isWellFormed(path) || !isFatSafeName(path.data(), path.size())) return false;
    const size_t slash = path.rfind('/');
    key.clear();
    if (slash > 0) {
      auto parent = findEntry(path.substr(0, slash));
      if (parent == nodes.end() || !parent->second.isDir) return false;
      key = parent->first;
    }
    key += path.substr(slash);
    return true;
  }

  Node* create(const std::string& path, bool isDir) {
    std::string key;
    if (!newKey(path, key)) return nullptr;
    Node& node = nodes[key];
    node.isDir = isDir;
    return &node;
  }

  std::map<std::string, Node> nodes;
  std::vector<std::unique_ptr<File>> handles;
  std::vector<Call> log;
  uint64_t written = 0;
  size_t failAtCall = 0;
  Tear tear = Tear::None;
};
