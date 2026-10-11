#include "GitTreeParser.h"

#include <cstring>

namespace {

bool keyIs(const char* key, size_t len, const char* name) { return len == strlen(name) && memcmp(key, name, len) == 0; }

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
  memset(sha, 0, sizeof(sha));
}

void GitTreeParser::feed(const char* data, size_t len) {
  if (stopped()) return;
  json.feed(data, len);
}

bool GitTreeParser::isComplete() const { return rootClosed && shaRead && treeClosed && truncatedRead && !hasError(); }

// The JSON layer stops at its first error, so an error seen once the root has closed came from trailing bytes.
bool GitTreeParser::hasError() const { return error || (json.hasError() && !rootClosed); }

GitTreeParser::PendingKey GitTreeParser::takePendingKey() {
  const PendingKey key = pendingKey;
  pendingKey = PendingKey::None;
  return key;
}

void GitTreeParser::onKey(const char* key, size_t len) {
  pendingKey = PendingKey::None;
  if (depth != 1) return;
  if (keyIs(key, len, "sha")) {
    pendingKey = PendingKey::Sha;
  } else if (keyIs(key, len, "tree")) {
    pendingKey = PendingKey::Tree;
  } else if (keyIs(key, len, "truncated")) {
    pendingKey = PendingKey::Truncated;
  }
}

void GitTreeParser::onString(const char* value, size_t len) {
  const PendingKey key = takePendingKey();
  if (key == PendingKey::None) return;
  if (key == PendingKey::Sha && shaFromHex(value, len, sha)) {
    shaRead = true;
  } else {
    error = true;
  }
}

void GitTreeParser::onScalar() {
  if (takePendingKey() != PendingKey::None) error = true;
}

void GitTreeParser::onBool(bool value) {
  const PendingKey key = takePendingKey();
  if (key == PendingKey::None) return;
  if (key == PendingKey::Truncated) {
    truncated = value;
    truncatedRead = true;
  } else {
    error = true;
  }
}

void GitTreeParser::onContainerStart(bool isArray) {
  const PendingKey key = takePendingKey();
  depth++;
  if (key == PendingKey::None) return;
  if (key == PendingKey::Tree && isArray) {
    inTreeArray = true;
  } else {
    error = true;
  }
}

void GitTreeParser::onContainerEnd() {
  pendingKey = PendingKey::None;
  if (depth == 0) return;
  depth--;
  if (depth == 1 && inTreeArray) {
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

void GitTreeParser::sOnNumber(void* ctx, const char* /*value*/, size_t /*len*/) {
  auto* self = static_cast<GitTreeParser*>(ctx);
  if (!self->stopped()) self->onScalar();
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
