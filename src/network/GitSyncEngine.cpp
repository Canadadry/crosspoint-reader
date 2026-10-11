#include "GitSyncEngine.h"

#include <Arduino.h>
#include <BookCachePath.h>
#include <FsHelpers.h>
#include <GitHubApi.h>
#include <Logging.h>
#include <Memory.h>
#include <SyncBudget.h>

#include <cstring>
#include <string>
#include <string_view>

#include "CrossPointState.h"
#include "HalSyncStorage.h"
#include "HttpDownloader.h"
#include "RecentBooksStore.h"
#include "util/BookCacheUtils.h"

namespace git_sync_engine {

namespace {

constexpr const char* LOG_TAG = "GSYNC";

// The cache root every reader opens books with.
constexpr char BOOK_CACHE_ROOT[] = "/.crosspoint";

// Glue share of SYNC_HEAP_BUDGET_BYTES, line by line.
// Run state and this context, plus the HalFile::Impl of up to 4 open files (3 storage slots + 1 hash).
constexpr size_t GLUE_STATE_LINE_BYTES = 2048;
constexpr size_t HAL_FILE_IMPL_BYTES = 128;
constexpr size_t MAX_OPEN_HAL_FILES = SyncStorage::MAX_OPEN_FILES + 1;
// Heap blocks of the URL string and the token copy.
constexpr size_t GLUE_STRINGS_LINE_BYTES = 768;
constexpr size_t TOKEN_COPY_BYTES = GitSyncConfig::TOKEN_MAX + 1;
// One HTTP request's non-TLS overhead or one store save's JsonDocument; hooks run between fetches, never both.
constexpr size_t GLUE_TRANSIENT_LINE_BYTES = 4096;
// Kept free when reserving the strings, so std::string's allocation (which aborts on failure) cannot fail.
constexpr size_t STRING_RESERVE_HEADROOM_BYTES = 1024;

static_assert(GITHUB_API_URL_CAPACITY + TOKEN_COPY_BYTES <= GLUE_STRINGS_LINE_BYTES,
              "the URL and token strings outgrew their line of SYNC_GLUE_SHARE_BYTES");
static_assert(GLUE_STATE_LINE_BYTES + GLUE_STRINGS_LINE_BYTES + GIT_SYNC_HASH_BUFFER_BYTES +
                      GLUE_TRANSIENT_LINE_BYTES <=
                  SYNC_GLUE_SHARE_BYTES,
              "the glue lines exceed SYNC_GLUE_SHARE_BYTES of SYNC_HEAP_BUDGET_BYTES");

// Volatile stores so the wipe of the token copy is not dropped as dead before its release.
void wipe(void* data, size_t len) {
  volatile unsigned char* p = static_cast<volatile unsigned char*>(data);
  while (len--) *p++ = 0;
}

const char* describeDownloadError(HttpDownloader::DownloadError error) {
  switch (error) {
    case HttpDownloader::OK:
      return "ok";
    case HttpDownloader::HTTP_ERROR:
      return "HTTP error";
    case HttpDownloader::FILE_ERROR:
      return "body refused";
    case HttpDownloader::ABORTED:
      return "aborted";
    case HttpDownloader::REDIRECT_REFUSED:
      return "redirect refused";
  }
  return "unknown";
}

bool bookCacheKindOf(std::string_view path, BookCacheKind& out) {
  if (FsHelpers::hasEpubExtension(path)) {
    out = BookCacheKind::Epub;
  } else if (FsHelpers::hasTxtExtension(path)) {
    out = BookCacheKind::Txt;
  } else if (FsHelpers::hasXtcExtension(path)) {
    out = BookCacheKind::Xtc;
  } else {
    return false;
  }
  return true;
}

struct EngineContext {
  EngineContext(const GitSyncConfig& config, const GitSyncProgressSink& caller) : config(config), caller(caller) {}

  void wipeToken() {
    wipe(password.data(), password.size());
    password.clear();
  }

  const GitSyncConfig& config;
  const GitSyncProgressSink caller;
  HalSyncStorage storage;
  char urlBuffer[GITHUB_API_URL_CAPACITY] = {};
  std::string url;
  std::string password;
  // The run's sink for the transfer in flight; fetchStream's data callback forwards to it.
  const SyncChunkSink* sink = nullptr;
  // The caller answered false once: a refused body is then the run stopping, not a failure.
  bool cancelRequested = false;
};

static_assert(GIT_SYNC_RUN_STATE_MAX_BYTES + sizeof(EngineContext) <=
                  GLUE_STATE_LINE_BYTES - MAX_OPEN_HAL_FILES * HAL_FILE_IMPL_BYTES,
              "the run state and engine context outgrew their line of SYNC_GLUE_SHARE_BYTES");

bool onProgress(void* ctx, const GitSyncProgress& progress) {
  auto* self = static_cast<EngineContext*>(ctx);
  if (!self->caller.onProgress || self->caller.onProgress(self->caller.ctx, progress)) return true;
  self->cancelRequested = true;
  return false;
}

// The only caller of HttpDownloader::fetchStream. url holds the request's URL.
GitFetchStatus fetch(EngineContext& self, const SyncChunkSink& sink, const char* accept, const char* what) {
  HttpDownloader::RequestOptions options;
  options.headerName = "Accept";
  options.headerValue = accept;
  options.refuseCrossOriginRedirect = true;

  self.sink = &sink;
  EngineContext* const ctx = &self;
  const HttpDownloader::DownloadError error = HttpDownloader::fetchStream(
      self.url, [ctx](const uint8_t* data, size_t len) { return ctx->sink->write(ctx->sink->ctx, data, len); },
      GITHUB_BASIC_AUTH_USER, self.password, options);
  self.sink = nullptr;

  if (error != HttpDownloader::OK) {
    LOG_ERR(LOG_TAG, "%s fetch failed: %s", what, describeDownloadError(error));
  }
  switch (error) {
    case HttpDownloader::OK:
      return GitFetchStatus::Ok;
    case HttpDownloader::REDIRECT_REFUSED:
      return GitFetchStatus::RedirectRefused;
    case HttpDownloader::FILE_ERROR:
      return self.cancelRequested ? GitFetchStatus::Aborted : GitFetchStatus::Failed;
    case HttpDownloader::HTTP_ERROR:
      return GitFetchStatus::NetworkError;
    case HttpDownloader::ABORTED:
      return GitFetchStatus::Aborted;
  }
  return GitFetchStatus::NetworkError;
}

GitFetchStatus fetchTree(void* ctx, const SyncChunkSink& sink) {
  auto* self = static_cast<EngineContext*>(ctx);
  if (!formatTreesUrl(self->urlBuffer, sizeof(self->urlBuffer), self->config)) {
    LOG_ERR(LOG_TAG, "Cannot build the trees URL");
    return GitFetchStatus::Failed;
  }
  self->url.assign(self->urlBuffer);
  return fetch(*self, sink, GITHUB_ACCEPT_JSON, "Tree");
}

GitFetchStatus fetchBlob(void* ctx, const SyncFetchRequest& request, const SyncChunkSink& sink) {
  auto* self = static_cast<EngineContext*>(ctx);
  if (!formatBlobUrl(self->urlBuffer, sizeof(self->urlBuffer), self->config, request.sha)) {
    LOG_ERR(LOG_TAG, "Cannot build the blob URL");
    return GitFetchStatus::Failed;
  }
  self->url.assign(self->urlBuffer);
  return fetch(*self, sink, GITHUB_ACCEPT_RAW, "Blob");
}

// The executor already moved the book, its caches and its bookmarks; only the stores follow here, saved before the
// executor journals the move so a power cut makes the next run repeat this idempotent update.
bool bookMoved(void*, const char* oldAbsPath, const char* newAbsPath) {
  const std::string oldPath(oldAbsPath);
  const std::string newPath(newAbsPath);
  BookCacheKind oldKind;
  BookCacheKind newKind;
  if (bookCacheKindOf(oldAbsPath, oldKind) && bookCacheKindOf(newAbsPath, newKind)) {
    RECENT_BOOKS.updatePath(oldPath, newPath, bookCacheDir(BOOK_CACHE_ROOT, oldKind, oldPath),
                            bookCacheDir(BOOK_CACHE_ROOT, newKind, newPath));
  } else {
    RECENT_BOOKS.updatePath(oldPath, newPath, std::string(), std::string());
  }
  if (APP_STATE.openEpubPath == oldPath) {
    APP_STATE.openEpubPath = newPath;
    if (!APP_STATE.saveToFile()) {
      LOG_ERR(LOG_TAG, "Cannot save the open book's new path");
      return false;
    }
  }
  return true;
}

bool clearCache(void*, const char* absPath) {
  clearBookCache(absPath);
  return true;
}

void logReport(const GitSyncReport& report) {
  const SyncRunResult& r = report.run;
  if (report.error != GitSyncError::Ok) {
    LOG_ERR(LOG_TAG, "Sync failed: %s (added %u, updated %u, moved %u, deleted %u, adopted %u, file errors %u)",
            describeGitSyncError(report.error), static_cast<unsigned>(r.added), static_cast<unsigned>(r.updated),
            static_cast<unsigned>(r.moved), static_cast<unsigned>(r.deleted), static_cast<unsigned>(r.adopted),
            static_cast<unsigned>(r.fileErrors));
    return;
  }
  LOG_INF(LOG_TAG,
          "Sync done: tree %u, added %u, updated %u, moved %u, deleted %u, adopted %u, pruned %u, file errors %u, "
          "state conflicts %u, skipped %u case / %u name",
          static_cast<unsigned>(report.tree.written), static_cast<unsigned>(r.added), static_cast<unsigned>(r.updated),
          static_cast<unsigned>(r.moved), static_cast<unsigned>(r.deleted), static_cast<unsigned>(r.adopted),
          static_cast<unsigned>(r.pruned), static_cast<unsigned>(r.fileErrors), static_cast<unsigned>(r.stateConflicts),
          static_cast<unsigned>(r.skippedCaseCollision), static_cast<unsigned>(r.skippedUnsafeName));
}

GitSyncReport failed(GitSyncError error) {
  GitSyncReport report;
  report.error = error;
  logReport(report);
  return report;
}

}  // namespace

GitSyncReport run(const GitSyncConfig& config, const GitSyncProgressSink& progress) {
  if (config.token[0] == '\0') {
    LOG_ERR(LOG_TAG, "No token in the config");
    return failed(GitSyncError::TreeDownloadFailed);
  }

  auto ctx = makeUniqueNoThrow<EngineContext>(config, progress);
  if (!ctx) {
    LOG_ERR(LOG_TAG, "OOM: engine context needs %u bytes", static_cast<unsigned>(sizeof(EngineContext)));
    return failed(GitSyncError::OutOfMemory);
  }
  ScopedCleanup wipeToken{[&ctx] { ctx->wipeToken(); }};

  if (ESP.getMaxAllocHeap() < GITHUB_API_URL_CAPACITY + STRING_RESERVE_HEADROOM_BYTES ||
      ESP.getFreeHeap() < GLUE_STRINGS_LINE_BYTES + STRING_RESERVE_HEADROOM_BYTES) {
    LOG_ERR(LOG_TAG, "OOM: URL and token strings need %u bytes", static_cast<unsigned>(GLUE_STRINGS_LINE_BYTES));
    return failed(GitSyncError::OutOfMemory);
  }
  // Reserved once so no per-file URL and no token copy reallocates.
  ctx->url.reserve(GITHUB_API_URL_CAPACITY - 1);
  ctx->password.reserve(GitSyncConfig::TOKEN_MAX);
  ctx->password.assign(config.token);

  const GitSyncTransport transport{ctx.get(), &fetchTree, &fetchBlob};
  const GitSyncHooks hooks{ctx.get(), &clearCache, &bookMoved};
  const GitSyncProgressSink sink{ctx.get(), &onProgress};
  const GitSyncReport report = runGitSync(ctx->storage, transport, hooks, sink);
  logReport(report);
  return report;
}

}  // namespace git_sync_engine
