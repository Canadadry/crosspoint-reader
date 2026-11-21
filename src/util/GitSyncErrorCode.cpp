#include "GitSyncErrorCode.h"

#include <cinttypes>
#include <cstdio>

namespace {

// Advances used past what snprintf wrote, never past the terminator of a full buffer.
void append(size_t capacity, size_t& used, int written) {
  if (written < 0) return;
  used += static_cast<size_t>(written);
  if (used >= capacity) used = capacity - 1;
}

}  // namespace

void formatGitSyncCode(char* out, size_t capacity, const GitSyncCodeInputs& in) {
  if (!out || capacity == 0) return;
  size_t used = 0;
  out[0] = '\0';
  append(capacity, used, snprintf(out, capacity, "E%u.%u", in.runError, in.fileError));
  if (in.apiError != 0 || in.apiStatus != 0) {
    append(capacity, used, snprintf(out + used, capacity - used, " A%u/%d", in.apiError, in.apiStatus));
  }
  if (in.rangedError != 0 || in.rangedStatus != 0) {
    append(capacity, used, snprintf(out + used, capacity - used, " R%u/%d", in.rangedError, in.rangedStatus));
  }
  if (in.fileError != 0 && in.fileSize != 0) {
    append(capacity, used,
           snprintf(out + used, capacity - used, " %" PRIu64 "/%" PRIu64 "K", in.fileHeld / 1024, in.fileSize / 1024));
  }
  append(capacity, used, snprintf(out + used, capacity - used, " H%" PRIu32, in.lowestFreeHeap / 1024));
}
