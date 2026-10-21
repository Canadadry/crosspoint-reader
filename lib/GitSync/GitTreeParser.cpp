#include "GitTreeParser.h"

#include <Logging.h>

#include <cstring>

namespace {

constexpr const char* LOG_TAG = "GITTREE";

bool textIs(const char* text, size_t len, const char* name) {
  return len == strlen(name) && memcmp(text, name, len) == 0;
}

// Unsigned decimal only: a sign, fraction, exponent or overflow is refused.
bool parseSize(const char* digits, size_t len, uint64_t& out) {
  if (len == 0) return false;
  uint64_t value = 0;
  for (size_t i = 0; i < len; i++) {
    if (digits[i] < '0' || digits[i] > '9') return false;
    const auto digit = static_cast<uint64_t>(digits[i] - '0');
    if (value > (UINT64_MAX - digit) / 10) return false;
    value = value * 10 + digit;
  }
  out = value;
  return true;
}

}  // namespace

GitTreeParser::GitTreeParser(void* ctx, GitTreeRecordFn onRecord)
    : json(JsonCallbacks{this, sOnKey, sOnString, sOnNumber, sOnBool, sOnNull, sOnObjectStart, sOnObjectEnd,
                         sOnArrayStart, sOnArrayEnd}),
      recordCtx(ctx),
      onRecord(onRecord) {
  reset();
}

void GitTreeParser::reset() {
  json.reset();
  depth = 0;
  pendingKey = PendingKey::None;
  inTreeArray = false;
  shaRead = false;
  treeClosed = false;
  truncatedRead = false;
  truncated = false;
  rootClosed = false;
  error = false;
  aborted = false;
  memset(sha, 0, sizeof(sha));
  emitted = 0;
  skippedLongPath = 0;
  skippedEscapedPath = 0;
  beginEntry();
}

void GitTreeParser::beginEntry() {
  entryType = EntryType::Invalid;
  entryPathRead = false;
  entryPathOverflowed = false;
  entryShaRead = false;
  entrySizeRead = false;
  entryPathLen = 0;
  entryPath[0] = '\0';
  entrySize = 0;
}

// A blob that cannot be read exactly fails the whole response: skipping it would look like a deletion.
// Only a path the sync could never have stored on the card (over-long or mis-decoded) is skipped.
void GitTreeParser::endEntry() {
  if (entryType == EntryType::Blob && entryPathOverflowed) {
    skipEntry("path over the JSON token buffer", skippedLongPath);
  } else if (entryType == EntryType::Blob && entryPathRead && memchr(entryPath, '\\', entryPathLen) != nullptr) {
    skipEntry("path with an undecoded escape", skippedEscapedPath);
  } else if (entryType == EntryType::Blob && entryPathRead && entryShaRead && entrySizeRead) {
    GitTreeRecord record{entryPath, entryPathLen, {}, entrySize};
    memcpy(record.sha, entrySha, sizeof(record.sha));
    emitted++;
    if (!onRecord(recordCtx, record)) aborted = true;
  } else if (entryType != EntryType::Ignored) {
    error = true;
  }
}

// The path is not logged: it is truncated or mis-decoded.
void GitTreeParser::skipEntry(const char* reason, size_t& counter) {
  counter++;
  char hex[SHA_HEX_LENGTH + 1] = "(no valid sha)";
  if (entryShaRead) shaToHex(entrySha, hex);
  LOG_ERR(LOG_TAG, "Skipped blob %s: %s", hex, reason);
}

void GitTreeParser::feed(const char* data, size_t len) {
  if (stopped()) return;
  json.feed(data, len);
}

bool GitTreeParser::isComplete() const {
  return rootClosed && shaRead && treeClosed && truncatedRead && !hasError() && !aborted;
}

// The JSON layer stops at its first error, so an error seen once the root has closed came from trailing bytes.
bool GitTreeParser::hasError() const { return error || (json.hasError() && !rootClosed); }

GitTreeParser::PendingKey GitTreeParser::takePendingKey() {
  const PendingKey key = pendingKey;
  pendingKey = PendingKey::None;
  return key;
}

// A `path` key still pending at the next key or the entry end lost its value to the token buffer.
void GitTreeParser::onKey(const char* key, size_t len) {
  if (pendingKey == PendingKey::EntryPath) entryPathOverflowed = true;
  pendingKey = PendingKey::None;
  if (inEntry()) {
    if (textIs(key, len, "path")) {
      pendingKey = PendingKey::EntryPath;
    } else if (textIs(key, len, "type")) {
      pendingKey = PendingKey::EntryType;
    } else if (textIs(key, len, "sha")) {
      pendingKey = PendingKey::EntrySha;
    } else if (textIs(key, len, "size")) {
      pendingKey = PendingKey::EntrySize;
    }
    return;
  }
  if (depth != 1) return;
  if (textIs(key, len, "sha")) {
    pendingKey = PendingKey::Sha;
  } else if (textIs(key, len, "tree")) {
    pendingKey = PendingKey::Tree;
  } else if (textIs(key, len, "truncated")) {
    pendingKey = PendingKey::Truncated;
  }
}

void GitTreeParser::rejectValue(PendingKey key) {
  if (key == PendingKey::Sha || key == PendingKey::Tree || key == PendingKey::Truncated || atTreeElement()) {
    error = true;
  }
}

void GitTreeParser::onString(const char* value, size_t len) {
  const PendingKey key = takePendingKey();
  switch (key) {
    case PendingKey::Sha:
      shaRead = shaFromHex(value, len, sha);
      if (!shaRead) error = true;
      break;
    case PendingKey::EntryPath:
      memcpy(entryPath, value, len);
      entryPath[len] = '\0';
      entryPathLen = len;
      entryPathRead = true;
      break;
    case PendingKey::EntryType:
      if (textIs(value, len, "blob")) {
        entryType = EntryType::Blob;
      } else if (textIs(value, len, "tree") || textIs(value, len, "commit")) {
        entryType = EntryType::Ignored;
      } else {
        entryType = EntryType::Invalid;
      }
      break;
    case PendingKey::EntrySha:
      entryShaRead = shaFromHex(value, len, entrySha);
      break;
    default:
      rejectValue(key);
      break;
  }
}

void GitTreeParser::onScalar() { rejectValue(takePendingKey()); }

void GitTreeParser::onNumber(const char* value, size_t len) {
  const PendingKey key = takePendingKey();
  if (key == PendingKey::EntrySize) {
    entrySizeRead = parseSize(value, len, entrySize);
  } else {
    rejectValue(key);
  }
}

void GitTreeParser::onBool(bool value) {
  const PendingKey key = takePendingKey();
  if (key == PendingKey::Truncated) {
    truncated = value;
    truncatedRead = true;
  } else {
    rejectValue(key);
  }
}

void GitTreeParser::onContainerStart(bool isArray) {
  const PendingKey key = takePendingKey();
  if (key == PendingKey::Tree && isArray) {
    inTreeArray = true;
  } else if (atTreeElement() && !isArray) {
    beginEntry();
  } else {
    rejectValue(key);
  }
  depth++;
}

void GitTreeParser::onContainerEnd() {
  if (pendingKey == PendingKey::EntryPath) entryPathOverflowed = true;
  pendingKey = PendingKey::None;
  if (depth == 0) return;
  depth--;
  if (depth == 2 && inTreeArray) {
    endEntry();
  } else if (depth == 1 && inTreeArray) {
    inTreeArray = false;
    treeClosed = true;
  } else if (depth == 0) {
    rootClosed = true;
  }
}

// -- StreamingJsonParser trampolines ----------------------------------------

void GitTreeParser::sOnKey(void* ctx, const char* key, size_t len) {
  auto* self = static_cast<GitTreeParser*>(ctx);
  if (!self->stopped()) self->onKey(key, len);
}

void GitTreeParser::sOnString(void* ctx, const char* value, size_t len) {
  auto* self = static_cast<GitTreeParser*>(ctx);
  if (!self->stopped()) self->onString(value, len);
}

void GitTreeParser::sOnNumber(void* ctx, const char* value, size_t len) {
  auto* self = static_cast<GitTreeParser*>(ctx);
  if (!self->stopped()) self->onNumber(value, len);
}

void GitTreeParser::sOnBool(void* ctx, bool value) {
  auto* self = static_cast<GitTreeParser*>(ctx);
  if (!self->stopped()) self->onBool(value);
}

void GitTreeParser::sOnNull(void* ctx) {
  auto* self = static_cast<GitTreeParser*>(ctx);
  if (!self->stopped()) self->onScalar();
}

void GitTreeParser::sOnObjectStart(void* ctx) {
  auto* self = static_cast<GitTreeParser*>(ctx);
  if (!self->stopped()) self->onContainerStart(false);
}

void GitTreeParser::sOnObjectEnd(void* ctx) {
  auto* self = static_cast<GitTreeParser*>(ctx);
  if (!self->stopped()) self->onContainerEnd();
}

void GitTreeParser::sOnArrayStart(void* ctx) {
  auto* self = static_cast<GitTreeParser*>(ctx);
  if (!self->stopped()) self->onContainerStart(true);
}

void GitTreeParser::sOnArrayEnd(void* ctx) {
  auto* self = static_cast<GitTreeParser*>(ctx);
  if (!self->stopped()) self->onContainerEnd();
}
