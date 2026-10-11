#include "GitSyncConfig.h"

#include <Memory.h>

#include <cstring>

#include "StreamingJsonParser.h"

namespace {

constexpr char UTF8_BOM[] = {'\xEF', '\xBB', '\xBF'};

bool isJsonSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

bool textIs(const char* text, size_t len, const char* name) {
  return len == strlen(name) && memcmp(text, name, len) == 0;
}

GitSyncConfigField knownField(const char* key, size_t len) {
  if (textIs(key, len, "owner")) return GitSyncConfigField::Owner;
  if (textIs(key, len, "repo")) return GitSyncConfigField::Repo;
  if (textIs(key, len, "branch")) return GitSyncConfigField::Branch;
  if (textIs(key, len, "token")) return GitSyncConfigField::Token;
  return GitSyncConfigField::None;
}

struct FieldSlot {
  char* buffer;
  size_t max;
};

FieldSlot slotOf(GitSyncConfig& config, GitSyncConfigField field) {
  switch (field) {
    case GitSyncConfigField::Owner:
      return {config.owner, GitSyncConfig::OWNER_MAX};
    case GitSyncConfigField::Repo:
      return {config.repo, GitSyncConfig::REPO_MAX};
    case GitSyncConfigField::Branch:
      return {config.branch, GitSyncConfig::BRANCH_MAX};
    case GitSyncConfigField::Token:
      return {config.token, GitSyncConfig::TOKEN_MAX};
    case GitSyncConfigField::None:
      break;
  }
  return {nullptr, 0};
}

bool isAlnum(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); }

// GitHub login and organisation alphabet. Hyphen placement is GitHub's concern, not URL safety.
bool isValidOwner(const char* value, size_t len) {
  for (size_t i = 0; i < len; i++) {
    if (!isAlnum(value[i]) && value[i] != '-') return false;
  }
  return true;
}

// GitHub repository alphabet; "." and ".." would be URL dot-segments.
bool isValidRepo(const char* value, size_t len) {
  if (textIs(value, len, ".") || textIs(value, len, "..")) return false;
  for (size_t i = 0; i < len; i++) {
    const char c = value[i];
    if (!isAlnum(c) && c != '.' && c != '_' && c != '-') return false;
  }
  return true;
}

bool endsWith(const char* text, size_t len, const char* suffix) {
  const size_t suffixLen = strlen(suffix);
  return len >= suffixLen && memcmp(text + len - suffixLen, suffix, suffixLen) == 0;
}

// A subset of git's ref-name rules whose alphabet needs no escaping in /git/trees/{branch}.
bool isValidBranch(const char* value, size_t len) {
  if (value[len - 1] == '.') return false;
  size_t segmentStart = 0;
  for (size_t i = 0; i <= len; i++) {
    if (i == len || value[i] == '/') {
      const char* segment = value + segmentStart;
      const size_t segmentLen = i - segmentStart;
      // Empty segment: leading or trailing '/', or "//".
      if (segmentLen == 0 || segment[0] == '.' || endsWith(segment, segmentLen, ".lock")) return false;
      segmentStart = i + 1;
      continue;
    }
    const char c = value[i];
    if (!isAlnum(c) && c != '.' && c != '_' && c != '-') return false;
    if (c == '.' && i + 1 < len && value[i + 1] == '.') return false;
  }
  return true;
}

// Visible ASCII, so a pasted space or newline is caught. No backslash: \uXXXX is not decoded.
bool isValidToken(const char* value, size_t len) {
  for (size_t i = 0; i < len; i++) {
    const char c = value[i];
    if (c < 0x21 || c > 0x7E || c == '\\') return false;
  }
  return true;
}

// Length already checked: 1 to the field's maximum.
bool hasValidCharacters(GitSyncConfigField field, const char* value, size_t len) {
  switch (field) {
    case GitSyncConfigField::Owner:
      return isValidOwner(value, len);
    case GitSyncConfigField::Repo:
      return isValidRepo(value, len);
    case GitSyncConfigField::Branch:
      return isValidBranch(value, len);
    case GitSyncConfigField::Token:
      return isValidToken(value, len);
    case GitSyncConfigField::None:
      break;
  }
  return false;
}

// About 560 bytes, mostly the JSON token buffer: heap-allocated per call, never a local.
struct ParseState {
  explicit ParseState(GitSyncConfig& out)
      : json({this, onKey, onString, onNumber, onBool, onNull, onObjectStart, onObjectEnd, onArray, onArray}),
        out(out) {}

  // The first failure wins; parsing stops there.
  void fail(GitSyncConfigStatus status, GitSyncConfigField field = GitSyncConfigField::None) {
    if (verdict.status == GitSyncConfigStatus::Ok) verdict = {status, field};
  }
  bool failed() const { return verdict.status != GitSyncConfigStatus::Ok || json.hasError(); }

  // A known key got no usable string: a non-string value, no value, or a string too long for the
  // JSON token buffer (dropped by the parser).
  void rejectPendingKey() {
    if (pending != GitSyncConfigField::None) fail(GitSyncConfigStatus::InvalidField, pending);
    pending = GitSyncConfigField::None;
  }

  static ParseState* self(void* ctx) { return static_cast<ParseState*>(ctx); }

  static void onKey(void* ctx, const char* key, size_t len) {
    ParseState* s = self(ctx);
    s->rejectPendingKey();
    const GitSyncConfigField field = knownField(key, len);
    if (field == GitSyncConfigField::None) return;
    bool& seen = s->seen[static_cast<size_t>(field)];
    if (seen) {
      s->fail(GitSyncConfigStatus::InvalidField, field);
      return;
    }
    seen = true;
    s->pending = field;
  }

  static void onString(void* ctx, const char* value, size_t len) {
    ParseState* s = self(ctx);
    if (s->pending == GitSyncConfigField::None) return;
    const FieldSlot slot = slotOf(s->out, s->pending);
    if (len == 0 || len > slot.max || !hasValidCharacters(s->pending, value, len)) {
      s->rejectPendingKey();
      return;
    }
    memcpy(slot.buffer, value, len);
    slot.buffer[len] = '\0';
    s->pending = GitSyncConfigField::None;
  }

  static void onNumber(void* ctx, const char*, size_t) { self(ctx)->rejectPendingKey(); }
  static void onBool(void* ctx, bool) { self(ctx)->rejectPendingKey(); }
  static void onNull(void* ctx) { self(ctx)->rejectPendingKey(); }

  // The format is flat: only the root object is allowed.
  static void onObjectStart(void* ctx) {
    ParseState* s = self(ctx);
    if (s->depth++ > 0) s->fail(GitSyncConfigStatus::Malformed);
  }

  static void onObjectEnd(void* ctx) {
    ParseState* s = self(ctx);
    s->rejectPendingKey();
    if (s->depth > 0 && --s->depth == 0) s->rootClosed = true;
  }

  // No array is ever open, so a ']' is as wrong as a '['.
  static void onArray(void* ctx) { self(ctx)->fail(GitSyncConfigStatus::Malformed); }

  StreamingJsonParser json;
  GitSyncConfig& out;
  GitSyncConfigResult verdict = {GitSyncConfigStatus::Ok, GitSyncConfigField::None};
  GitSyncConfigField pending = GitSyncConfigField::None;
  uint8_t depth = 0;
  bool rootClosed = false;
  // Keys met so far, indexed by GitSyncConfigField.
  bool seen[5] = {};
};

bool isFieldSeen(const ParseState& state, GitSyncConfigField field) { return state.seen[static_cast<size_t>(field)]; }

GitSyncConfigResult refuse(GitSyncConfig& out, GitSyncConfigStatus status,
                           GitSyncConfigField field = GitSyncConfigField::None) {
  memset(&out, 0, sizeof(out));
  return {status, field};
}

}  // namespace

GitSyncConfigResult parseGitSyncConfig(const char* json, size_t len, GitSyncConfig& out) {
  if (len > GitSyncConfig::MAX_CONFIG_FILE_BYTES) return refuse(out, GitSyncConfigStatus::TooLarge);
  if (json == nullptr) return refuse(out, GitSyncConfigStatus::Malformed);
  size_t start = 0;
  if (len >= sizeof(UTF8_BOM) && memcmp(json, UTF8_BOM, sizeof(UTF8_BOM)) == 0) start = sizeof(UTF8_BOM);
  while (start < len && isJsonSpace(json[start])) start++;
  if (start == len || json[start] != '{') return refuse(out, GitSyncConfigStatus::Malformed);

  // Filled field by field as values arrive; refuse() zeroes it again on failure.
  memset(&out, 0, sizeof(out));
  auto state = makeUniqueNoThrow<ParseState>(out);
  if (!state) return refuse(out, GitSyncConfigStatus::OutOfMemory);
  // One byte at a time, so the bytes after the root object are left for the check below.
  size_t pos = start;
  while (pos < len && !state->rootClosed && !state->failed()) state->json.feed(json + pos++, 1);
  if (state->json.hasError()) return refuse(out, GitSyncConfigStatus::Malformed);
  if (state->verdict.status != GitSyncConfigStatus::Ok) return refuse(out, state->verdict.status, state->verdict.field);
  if (!state->rootClosed) return refuse(out, GitSyncConfigStatus::Malformed);
  while (pos < len && isJsonSpace(json[pos])) pos++;
  if (pos != len) return refuse(out, GitSyncConfigStatus::Malformed);
  for (const auto field : {GitSyncConfigField::Owner, GitSyncConfigField::Repo, GitSyncConfigField::Token}) {
    if (!isFieldSeen(*state, field)) return refuse(out, GitSyncConfigStatus::MissingField, field);
  }
  if (!isFieldSeen(*state, GitSyncConfigField::Branch)) strcpy(out.branch, "main");
  return {GitSyncConfigStatus::Ok, GitSyncConfigField::None};
}

const char* describeGitSyncConfigResult(GitSyncConfigResult result) {
  switch (result.status) {
    case GitSyncConfigStatus::Ok:
      return "ok";
    case GitSyncConfigStatus::NotFound:
      return "not found";
    case GitSyncConfigStatus::ReadError:
      return "empty or unreadable";
    case GitSyncConfigStatus::TooLarge:
      return "larger than 1024 bytes";
    case GitSyncConfigStatus::Malformed:
      return "not a single flat JSON object";
    case GitSyncConfigStatus::OutOfMemory:
      return "out of memory";
    case GitSyncConfigStatus::MissingField:
      switch (result.field) {
        case GitSyncConfigField::Owner:
          return "owner: missing";
        case GitSyncConfigField::Repo:
          return "repo: missing";
        case GitSyncConfigField::Branch:
          return "branch: missing";
        case GitSyncConfigField::Token:
          return "token: missing";
        case GitSyncConfigField::None:
          break;
      }
      return "missing field";
    case GitSyncConfigStatus::InvalidField:
      switch (result.field) {
        case GitSyncConfigField::Owner:
          return "owner: not a string, given twice, empty, over 39 bytes or not [A-Za-z0-9-]";
        case GitSyncConfigField::Repo:
          return "repo: not a string, given twice, empty, over 100 bytes, '.', '..' or not [A-Za-z0-9._-]";
        case GitSyncConfigField::Branch:
          return "branch: not a string, given twice, empty, over 255 bytes, not [A-Za-z0-9._/-] or not a git branch "
                 "name";
        case GitSyncConfigField::Token:
          return "token: not a string, given twice, empty, over 255 bytes or not visible ASCII";
        case GitSyncConfigField::None:
          break;
      }
      return "invalid field";
  }
  return "unknown error";
}
