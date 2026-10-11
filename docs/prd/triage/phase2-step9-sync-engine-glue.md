---
title: "GitHub sync engine glue: one call that runs a whole sync on the device"
description: "The phase 1 sync library (tree parser, record files, planner, executor) and the config loader exist, but nothing wires them to the SD card, the network, the reader's state and a progress screen, so the Update button has nothing to call."
status: needs-triage
---

## Problem Statement

The owner keeps their books in a private GitHub repository and wants **Update** on the
Xteink X4 to make the SD card mirror it. Phase 1 built every piece of logic as host-tested
library code: the trees response parser, the path filter, the record file format for
`tree.tmp` and the manifest, the manifest compactor, the planner (about 49 KB) and the
executor with its callbacks. Step 7 loads `/.crosspoint/github-sync.json`. Step 8 gave
`HttpDownloader` a streaming `fetchStream` with an extra request header and a cross-origin
redirect guard.

None of these pieces touches the device yet:

- the executor talks to an abstract storage interface that only has an in-memory double;
- nothing downloads the trees response, filters it and writes `tree.tmp`;
- nothing fetches blobs, hashes local files for adoption, or tells the recent-books list
  and the open-book state that a book moved;
- nothing decides in which order the large objects are allocated, so nobody knows whether
  the 64 KB sync heap budget really holds on the device;
- the activity of step 10 has no single call to make and no progress or cancel shape to
  build its screen on.

## Solution

One device entry point runs a whole sync from a loaded config and reports one result:
an error kind for the UI plus the executor's counts. Behind it:

- most of the orchestration is a **pure library module** (the sync run) that drives the
  phases over the abstract storage interface, an injected transport and injected reading
  state hooks, so its ordering, cleanup, cancel and error mapping are host-tested on the
  in-memory storage with the recorded trees fixture;
- two smaller pure helpers join it in the library: the GitHub API URL builder, and the
  streaming tree snapshot writer (parser, filter and record writer in one object);
- a third pure helper hashes a local file as a git blob through a stream in 4 KB chunks;
- the device side is thin: the `HalStorage` adapter of the storage interface, the HTTP
  transport over `HttpDownloader`, and the two reading-state hooks (recent books and open
  book on a move, `clearBookCache` on a modify);
- one named 64 KB budget constant in the library replaces the literals in the planner and
  executor assertions, and the glue asserts its own share against it.

The owner sees, through step 10: a progress screen fed by phase, `n/total` and file
name; Back taking effect between files; and a summary or a precise error ("the
repository has too many files", "GitHub redirected to another host", "network lost").

## User Stories

1. As the library owner, I want one Update to fetch the branch's tree, plan and apply every change, so that the card mirrors the repository without me taking it out.
2. As the library owner, I want a truncated tree response to stop the sync before anything on the card changes, so that a partial listing never turns into deletions.
3. As the library owner, I want a malformed or cut-off tree response to stop the sync before anything changes, so that a network hiccup never deletes my books.
4. As the library owner, I want a repository with more files than the device supports to be refused with a clear message, so that I know to split the library instead of watching a half-done sync.
5. As the library owner, I want the over-the-cap refusal to come while the tree is still downloading, so that I do not wait for a multi-megabyte listing the device will reject anyway.
6. As the library owner, I want dot paths (`.github/`, `.atelier/`, `.crosspoint/`) left out of the tree the device plans from, so that they are never written, moved or deleted.
7. As the library owner, I want malformed repository paths skipped and logged, so that one odd name never stops the rest of the sync.
8. As the library owner, I want each blob downloaded from GitHub's content-addressed blob endpoint with the raw media type, so that the file on the card is the exact file in the repository.
9. As the library owner, I want my token sent only to `api.github.com`, so that a redirect never hands it to another host.
10. As the library owner, I want a refused redirect to stop the whole sync with its own message, so that I am not shown one failure per file.
11. As the library owner, I want the sync to stop after a few consecutive network failures, so that a dropped Wi-Fi does not make the device time out on every remaining file.
12. As the library owner, I want an interrupted sync to resume where it stopped on the next Update, so that I never re-download what is already on the card.
13. As the library owner, I want a leftover tree snapshot from an interrupted run deleted when a sync starts, so that a stale listing is never planned from and never wastes card space.
14. As the library owner, I want the tree snapshot removed after every run, so that `/.crosspoint/gitsync/` holds only the manifest between syncs.
15. As the library owner, I want my first sync to adopt the files I copied from a PC by hashing them on the card, so that it downloads nothing that is already there.
16. As the library owner, I want adoption hashing to read in 4 KB chunks, so that a large book is hashed without a large buffer.
17. As the library owner, I want a book I moved in the repository to keep its reading progress, bookmarks and recent-books entry, so that moving a book costs me nothing.
18. As the library owner, I want the book I was last reading to reopen at its new path after a move, so that the reader does not lose it on the next boot.
19. As the library owner, I want the recent-books cover thumbnail to follow a moved book, so that the home screen still shows its cover.
20. As the library owner, I want the reading-state update for a move saved before the sync records the move as done, so that a power cut cannot leave my recent books pointing at a path that no longer exists.
21. As the library owner, I want a modified book's cache cleared, so that the reader re-parses the new version instead of showing stale pages.
22. As the library owner, I want Back to cancel the sync between files, so that I can stop it without leaving a half-written book.
23. As the library owner, I want Back to work even while a large file is downloading or being hashed, so that a press is never lost while the screen looks busy.
24. As the library owner, I want Back during the tree download to stop at once, so that cancelling early changes nothing.
25. As the library owner, I want the progress screen to show the current phase (preparing, fetching the list, planning, applying), so that I know the device is working.
26. As the library owner, I want `n/total : file` during the apply phase, so that I can see how far the sync is.
27. As the library owner, I want a summary with the added, updated, moved, deleted and adopted counts, plus the files that failed and will be retried, so that I know what changed.
28. As the library owner, I want an out-of-memory condition reported as an error, never a crash, so that the device stays usable.
29. As the library owner, I want an SD card failure reported as a storage error, so that I can check the card.
30. As the library owner, I want a corrupt manifest reported distinctly, so that the user guide can tell me what to do about it.
31. As the library owner, I want my token never written to the log, so that a serial capture does not leak it.
32. As the step 10 activity, I want one call taking the config and a progress sink and returning a report, so that the activity only handles Wi-Fi, screens and input.
33. As the step 10 activity, I want the progress callback called often during long transfers, with a flag telling a heartbeat from a state change, so that I can pump input every time and redraw only when something changed.
34. As the step 10 activity, I want to return false from the progress callback to request a cancel, so that I need no shared flag or task.
35. As the step 10 activity, I want an error kind for every way a sync can end, so that each maps to one `tr()` string.
36. As the step 10 activity, I want the progress callback to be a plain function pointer with a context, so that no `std::function` or heap closure is needed.
37. As a firmware developer, I want the phase order, cleanup, cancel and error mapping in a host-tested library module, so that they are proven without hardware.
38. As a firmware developer, I want the GitHub URLs built by a pure function with a fixed capacity derived from the config's field limits, so that no URL can overflow and the format is pinned by tests.
39. As a firmware developer, I want the tree snapshot writer host-tested on the recorded fixture fed in random chunks, so that its output is byte-identical whatever the network's chunking.
40. As a firmware developer, I want the file-hash helper tested against `git hash-object` vectors and size mismatches, so that adoption cannot accept a wrong file.
41. As a firmware developer, I want the `HalStorage` adapter to keep the storage interface's contract (three open files, rename never overwrites, case-insensitive lookup, FAT-forbidden names refused), so that the executor's crash-safety proof holds on the device.
42. As a firmware developer, I want each large object allocated with `makeUniqueNoThrow` at a fixed point and freed as soon as its phase ends, so that the heap peak is the documented one.
43. As a firmware developer, I want one named budget constant and per-part shares asserted at compile time, so that a later change that grows the planner or executor fails the build instead of the device.
44. As a firmware developer, I want every status-returning call of the glue to have a named `LOG_ERR`, so that a failed sync can be diagnosed from the serial log.
45. As a firmware developer, I want no cache or bookmark path derived inline in the glue, so that the sync and the reader can never disagree on where a book's state lives.
46. As a firmware developer, I want the step 8 API used in exactly one place, so that a different final name or shape costs a one-function change.
47. As a firmware developer, I want existing callers of `HttpDownloader`, `RECENT_BOOKS` and `APP_STATE` unchanged, so that this step is additive.

## Implementation Decisions

### Modules

Pure library modules (in the GitSync library, no Arduino, host-tested):

- **Sync budget (new, header only).** `SYNC_HEAP_BUDGET_BYTES = 65,536` and the shares
  derived from it: planner 51,200, executor 3,072, glue 11,264 (the remainder, asserted
  equal to budget minus the other two). The planner's and executor's existing
  `static_assert`s use the shares instead of their literals; their messages name the
  budget constant. Nothing else changes in those modules.
- **GitHub API (new).** Builds the two URLs into a caller buffer and holds the request
  constants:
  - `formatTreesUrl(out, capacity, config)` →
    `https://api.github.com/repos/{owner}/{repo}/git/trees/{branch}?recursive=1`;
  - `formatBlobUrl(out, capacity, config, sha)` →
    `https://api.github.com/repos/{owner}/{repo}/git/blobs/{40 lower-case hex}`;
  - both return false and leave an empty string when a field is empty or the result does
    not fit;
  - `GITHUB_API_URL_CAPACITY`, a `constexpr` computed from the config's field limits
    (owner 39, repo 100, branch 255) and the fixed parts: 448 bytes, the trees URL being
    the longer;
  - `GITHUB_ACCEPT_JSON = "application/vnd.github+json"` for the trees call,
    `GITHUB_ACCEPT_RAW = "application/vnd.github.raw+json"` for blobs, and
    `GITHUB_BASIC_AUTH_USER = "x"`, the username step 0.0 verified (GitHub ignores it and
    takes the token as the password).

  No escaping: step 7 restricted owner, repo and branch to URL-safe alphabets.
- **Blob file hash (new).** `hashGitBlobSource(source, expectedSize, buffer, bufferLen,
  hasher, outSha, tick)` reads a `RecordSource` to its end in `bufferLen` chunks through
  the step 1 incremental hasher, calls the optional tick after each chunk, and returns
  false on a read error or when the bytes read differ from `expectedSize` (the hasher's
  own size check). The buffer and the hasher are the caller's, so nothing is allocated
  and nothing sits on the stack.
- **Tree snapshot writer (new).** One object that owns a `GitTreeParser` and a
  `RecordWriter` over a caller's `RecordSink`. Interface:
  - `begin()` writes the tree file header;
  - `feed(data, len)` passes a chunk to the parser; `stopped()` tells the transport to end
    the transfer;
  - for each blob record: `classifySyncPath` decides. `Syncable` is written as an entry
    frame (`replaces = NO_OFFSET`); `Hidden` is counted and skipped quietly; `Malformed`
    and `TooLong` are counted, skipped and logged with `LOG_ERR` by SHA, never by path
    (the path may hold control bytes);
  - past `MAX_SYNC_FILES` written entries, the record callback returns false: the parser
    stops and the status is `TooManyFiles`, so an oversize tree is refused during the
    download, not after it;
  - `finish()` seals the file only when the parser `isComplete()` and not
    `isTruncated()`, and returns one status: `Ok`, `Truncated`, `Invalid` (parser error
    or incomplete body), `TooManyFiles`, `WriteFailed` (a sink write failed, checked
    before the parser state), `Cancelled` (stopped by the caller);
  - counters: entries written, hidden, malformed, too long, plus the parser's long-path
    and escaped-path skips, and the tree SHA for the log.

  About 1.5 KB, no heap: allocated with `makeUniqueNoThrow` for the tree phase only.
- **Sync run (new).** The orchestration, over the storage interface, a transport and
  hooks. Single entry point:

  ```
  GitSyncReport runGitSync(SyncStorage& storage, const GitSyncTransport& transport,
                           const GitSyncHooks& hooks, const GitSyncProgressSink& progress);
  ```

  Shapes (all plain function pointers with one `void* ctx`, no `std::function`):
  - `GitSyncTransport { ctx; fetchTree(ctx, const SyncChunkSink&) → GitFetchStatus;
    fetchBlob(ctx, const SyncFetchRequest&, const SyncChunkSink&) → GitFetchStatus }`;
  - `GitFetchStatus { Ok, Failed, NetworkError, RedirectRefused, Aborted }`: `Failed` is
    a body the sink refused, `NetworkError` is a connection, TLS or HTTP status failure,
    `Aborted` is a transfer stopped because the run asked;
  - `GitSyncHooks { ctx; clearBookCache(ctx, absPath) → bool; bookMoved(ctx, oldAbs,
    newAbs) → bool }`, passed through to the executor's hooks unchanged;
  - `GitSyncProgress { phase; tick; done; total; kind; repoPath; treeEntries }` with
    `GitSyncPhase { Preparing, FetchingTree, Planning, Applying }`. `tick` is true for a
    heartbeat during a transfer or a hash (nothing changed), false on a phase change or a
    new operation;
  - `GitSyncProgressSink { ctx; onProgress(ctx, const GitSyncProgress&) → bool }`:
    returning false requests a cancel, latched for the rest of the run. Null sink or null
    function means "never cancel, report nothing";
  - `GitSyncReport { GitSyncError error; SyncRunResult run; snapshot counters }`;
  - `GitSyncError { Ok, Cancelled, OutOfMemory, StorageError, ManifestCorrupt,
    TreeDownloadFailed, TreeInvalid, TreeTruncated, TooManyFiles, RedirectRefused,
    TooManyFetchFailures }`, plus `describeGitSyncError(error)`, a constant string per
    value for logs only (the UI uses `tr()` in step 10);
  - `Ok` with a non-zero `run.fileErrors` is a completed sync with per-file failures the
    next run retries; step 10 shows them in the summary.

  The run implements the executor's `fetch` and `hashFile` callbacks itself; the
  `clearBookCache` and `bookMoved` callbacks forward to the hooks.

Device modules (firmware network area, no host tests):

- **`HalStorage` sync adapter (new).** Implements `SyncStorage` and `SyncFile` over
  `HalStorage`/`HalFile`; see "Storage adapter" below.
- **Sync engine (new).** The device entry point for step 10, in a namespace beside
  `git_sync_config`:

  ```
  namespace git_sync_engine {
  GitSyncReport run(const GitSyncConfig& config, const GitSyncProgressSink& progress);
  }
  ```

  It allocates one context (the adapter, the URL buffer, the reserved URL and token
  strings, the transport's failure flags), builds the transport over `HttpDownloader` and
  the hooks over `RECENT_BOOKS`, `APP_STATE` and `clearBookCache`, calls `runGitSync`,
  wipes the token copy, logs the report and returns it. The caller owns the config
  (loaded by `git_sync_config::load`) and must already be on Wi-Fi.

### Order of phases (inside `runGitSync`)

1. **Allocate** the executor and the 4 KB hash buffer with `makeUniqueNoThrow`; either
   missing → `OutOfMemory`, nothing touched. The buffer lives for the whole run, so
   adoption never allocates per file.
2. **Prepare** (phase `Preparing`): `SyncExecutor::prepare()`. It creates the work
   directory, removes a leftover `download.tmp`, recovers or removes `manifest.new` and
   compacts the journal (8 KB scratch, freed before it returns). Non-`Ok` → mapped error.
3. **Leftover cleanup**: a `tree.tmp` left by an earlier run is removed (failure →
   `StorageError`). `prepare()` handles the other temporary files, so after this step the
   work directory holds only the manifest.
4. **Cancel check.**
5. **Tree** (phase `FetchingTree`): allocate the tree snapshot writer; open `tree.tmp`
   with `Truncate` through the storage; `begin()`; `transport.fetchTree` with a sink that
   feeds the writer, emits a tick, and returns false once the writer `stopped()` or a
   cancel is latched; `finish()`; flush and close the file; free the writer. Any status
   other than `Ok`, or a transport status other than `Ok`, removes `tree.tmp` and ends
   the run: nothing on the card has changed beyond the compaction, which is equivalent.
   The writer's status takes precedence over the transport's (a `TooManyFiles` stop makes
   the transport report a callback abort).
6. **Cancel check.**
7. **Plan and apply** (phase `Planning`, then `Applying` from the first operation):
   allocate the planner (about 49 KB) with `makeUniqueNoThrow` (OOM → `OutOfMemory`, with
   `tree.tmp` removed), then `SyncExecutor::apply(planner)`. The executor's `onProgress`
   becomes an `Applying` progress; a false return from the sink calls
   `requestCancel()`.
8. **Free** the planner, remove `tree.tmp`, free the executor and the hash buffer; map
   the result.

The planner is allocated only once the tree download is over, as the plan orders, so the
49 KB block never coexists with the tree phase's TLS session plus parser. Because the
planner holds the largest block, it is allocated after the executor and the hash buffer
are already in place and the tree writer is freed; an allocation failure is logged with
the requested size.

### Fetch, network failures and cancel

- **Blob fetch** (the executor's `fetch`): the run calls `transport.fetchBlob`, and wraps
  the executor's chunk sink so each chunk also emits a tick. A cancel latched during a
  blob is passed to `requestCancel()` and does not cut the transfer: Back cancels between
  files, as the plan says, and the file in flight completes or fails as a whole.
- **Mapping**: `Ok` → true. `Failed` and `Aborted` → false (per-file error, manifest
  unchanged). `NetworkError` → false and counts one consecutive network failure; any `Ok`
  resets the count. At `MAX_CONSECUTIVE_FETCH_FAILURES = 3` the run latches
  `TooManyFetchFailures` and calls `requestCancel()`. `RedirectRefused` → false, latches
  `RedirectRefused` and calls `requestCancel()` at once: retrying would refuse again for
  every file (this answers step 8's H-9-2: fatal for the run).
- **Final error**: when the executor ends `Cancelled`, the latched reason wins in this
  order: `RedirectRefused`, `TooManyFetchFailures`, then the user's `Cancelled`.
- **Tree fetch**: any transport status other than `Ok` is `TreeDownloadFailed` (bad
  token, unknown repository or branch, network), except `RedirectRefused` and a latched
  cancel. There is no per-status distinction (401 vs 404) because step 8 exposes no
  status code; the log line from `HttpDownloader` carries it.

### Hash callback (adoption)

- The run opens the local file through the storage interface (`Read`) and calls
  `hashGitBlobSource` with the run's 4 KB buffer and its own `GitBlobHasher` (the
  executor's hasher is private). Each chunk emits a tick, so Back is pumped during a long
  hash.
- Slot use: the executor calls `hashFile` only from adopt-or-download before it opens the
  download temp, so the manifest and the tree hold two slots and the hash takes the third;
  the in-memory storage's slot limit proves it in the host tests.
- A failure to open or read returns false, so the executor downloads the file instead.

### Storage adapter (`HalStorage` implementation)

- **Three slots.** A fixed array of `SyncStorage::MAX_OPEN_FILES` file objects inside the
  adapter, each holding one `HalFile` and its flags. `open` takes a free slot or returns
  nullptr; `close` closes the member `HalFile` explicitly (a member, so the explicit close
  is the release point) and frees the slot. No heap of its own; each open still makes
  `HalStorage::open` allocate one `HalFile::Impl` (counted in the budget below).
- **Open modes**: `Read` → `O_RDONLY`, refusing a directory; `Truncate` →
  `O_WRONLY | O_CREAT | O_TRUNC`; `Append` → `O_RDWR | O_APPEND`, which `HalStorage::open`
  passes through to SdFat (step 1.6 note). A handle that is not open → nullptr.
- **File calls**: `read` forwards `HalFile::read`; `seek` forwards `HalFile::seek`;
  `size` is `fileSize64()`; `write` returns true only when all bytes were written and
  latches a write-failed flag otherwise.
- **`flush`**: `HalFile::flush()` returns `void`, so card-level flush errors are
  invisible. The adapter returns false when the latched write-failed flag is set, true
  otherwise; this limit is recorded under Further Notes.
- **Rename never overwrites**: `rename` first checks `Storage.exists(to)` and refuses;
  SdFat's rename already refuses an existing destination (the progress file's
  remove-then-rename relies on it), and the explicit check makes the contract visible in
  the adapter.
- **Case-insensitive lookup**: FAT and exFAT name lookup in SdFat ignores case, so
  `exists`, `fileSize`, `open` and the rename check above see every case variant. The
  adapter adds no folding of its own; the review states this.
- **FAT-forbidden names**: `open` with `Truncate`, `mkdirs` and `rename`'s destination
  call `isFatSafeName` on the path and refuse a forbidden character before any SD call.
- **Directories**: `mkdirs` returns true when the path already is a directory, otherwise
  calls `Storage.mkdir(path, true)`. `remove` refuses a directory. `rmdir` refuses a path
  that is not an empty directory (checked with `isEmptyDirectory`, which opens the
  directory and asks `openNextFile()` for one entry). `fileSize` refuses a directory.
- **Open-file rule**: the contract's "an open file cannot be renamed or removed" is kept
  by the executor's call order, which the in-memory double enforces in the host tests;
  the adapter does not copy paths to enforce it again (three path copies would cost about
  800 bytes). The review names this choice.

### Transport (device side)

- One function per call, both funnelled through a single private helper that is the
  only caller of `HttpDownloader::fetchStream` (H-8-1 to H-8-4, all checked against the
  committed code).
- **URLs** are built with the library's builders into the context's 448-byte buffer, then
  assigned to a `std::string` reserved once to `GITHUB_API_URL_CAPACITY`, so the per-file
  URL causes no reallocation (`fetchStream` takes `const std::string&`, H-8-1).
- **Credentials**: username `GITHUB_BASIC_AUTH_USER`, password a `std::string` copy of the
  token made once per run, wiped with volatile stores and cleared before the context is
  freed, like the config loader's buffers. The token is never logged; only the URL
  (which holds no secret) appears in `HttpDownloader`'s own debug line.
- **Options**: an `HttpDownloader::RequestOptions` with `refuseCrossOriginRedirect = true`,
  `headerName = "Accept"` and `headerValue` set to `GITHUB_ACCEPT_JSON` for the trees call
  and `GITHUB_ACCEPT_RAW` for blobs (string literals, so they outlive the call, as the
  struct's comment requires).
- **Data callback**: a lambda capturing only the context pointer, which reads the current
  `SyncChunkSink` from the context; one pointer fits `std::function`'s small buffer, so
  neither the lambda nor `fetchStream`'s copy of it into its internal sink allocates
  (H-8-4).
- **Status mapping**: `OK` → `Ok`; `REDIRECT_REFUSED` → `RedirectRefused`; `FILE_ERROR`
  (the data callback returned false on a 200 body) → `Aborted` when the run asked to
  stop, else `Failed`; `HTTP_ERROR` (connection, TLS, any non-200 status including 401
  and 404, an incomplete body, too many redirects) → `NetworkError`; `ABORTED` →
  `Aborted`, unreachable from `fetchStream` (H-8-7) and mapped only so the switch is
  total.

### Reading-state hooks (device side)

- **Paths**: the executor hands the hooks absolute paths (`/` plus the repository path),
  the same form the file browser and `APP_STATE.openEpubPath` use; the executor's tests
  already pin this against `BookmarkUtil`.
- **`bookMoved(old, new)`**, following the finished-book move in the EPUB reader:
  - the executor has already renamed the book, its epub, txt and xtc cache directories
    and its bookmark file; the hook only updates the two stores;
  - the cache paths for the recent-books cover come from `bookCacheDir("/.crosspoint",
    kind, path)` with the kind picked from each path's extension (`FsHelpers`); a path
    with no book extension passes empty cache paths, so the cover is left alone;
  - `RECENT_BOOKS.updatePath(old, new, oldCache, newCache)`, which persists itself when an
    entry matches and is a no-op otherwise;
  - when `APP_STATE.openEpubPath == old`: set it to `new` and `APP_STATE.saveToFile()`; a
    failed save returns false, which the executor treats as fatal, so the next run
    resumes the move and repeats the idempotent hook.
- **Persistence is write-through, not debounced.** The executor writes the operation's
  journal frame after the hook returns; a save deferred to the end of the run would be
  lost by a power cut after that frame, and the next run would not repeat the move. Cost
  is bounded: `updatePath` saves only on a match (at most 10 recent books), `APP_STATE`
  at most once per run.
- **`clearBookCache(path)`**: calls the existing `clearBookCache` and returns true (it
  returns `void`; it clears by extension, so a non-book does nothing).

### Progress and input pumping (shape for step 10)

- The run calls `onProgress` on each phase change, on each operation start (`done`,
  `total`, `kind`, repository path), and as a tick after every transfer chunk (about
  1 KB, `HttpDownloader`'s read size) and every 4 KB hashed. There is no time throttle in
  the library (it has no clock); the activity throttles redraws, as OPDS does.
- Step 10's callback pumps `mappedInput.update()`, turns a Back release into
  `return false`, and may reset the task watchdog; the sync runs in the activity loop,
  as OPDS and OTA do, so no task is created here.
- Planning has no ticks: the planner reads the manifest and tree before its first
  operation. Its SD reads are bounded (5 × entries + 16, step 1.5 note); the duration is
  on the owner's step 12 list.

### Logging (review checklist for *Verified by*)

Every status-returning call the glue makes, and where its failure is logged with `LOG_ERR`:

- `GitTreeParser::hasError()` and an incomplete body → `Invalid` in the snapshot writer's
  `finish()`, logged there with the parser counters;
- `GitTreeParser::isTruncated()` → `Truncated`, logged in `finish()`;
- `GitTreeParser::wasAborted()` → logged in `finish()` with the reason the writer
  recorded (cap, sink write, caller stop);
- `Malformed` and `TooLong` paths → logged by SHA in the writer;
- every non-`Ok` `SyncPlanStatus` → logged by `SyncExecutor::apply` and mapped to a
  `SyncRunStatus`, which the run logs again with its error kind (below);
- every `RecordStatus` other than `Record` and `End` reaching the glue (the
  `recordStatus` of a `ManifestError` or `TreeError` result) → printed in the run's
  `LOG_ERR` line for that result. The glue reads no record file itself; it only writes
  `tree.tmp` through `RecordWriter`, whose false returns become `WriteFailed`;
- every non-`Ok` `SyncRunResult` status from `prepare()` and `apply()` → one `LOG_ERR` in
  the run naming the status, the record status and the mapped error, `Cancelled`
  included, so every run that does not end `Ok` leaves exactly one error line;
- every non-`Ok` `GitFetchStatus` of the tree fetch and every `RedirectRefused` or
  failure-limit stop → `LOG_ERR` in the run;
- allocation failures → `LOG_ERR` with the requested size;
- adapter: a refused FAT name and a rename onto an existing path → `LOG_ERR`;
- the engine logs the final report: `LOG_INF` with the counts on `Ok`, `LOG_ERR` with
  `describeGitSyncError` otherwise.

### Mapping of executor results to `GitSyncError`

- `StorageError`, `NotPrepared`, `TreeError` (the snapshot is ours: an unreadable
  `tree.tmp` is a card problem) → `StorageError`;
- `ManifestError` → `ManifestCorrupt`;
- `CapExceeded` → `TooManyFiles` (the manifest side can still exceed the cap);
- `OutOfMemory` → `OutOfMemory`;
- `Cancelled` → latched reason, as above;
- `Ok` → `Ok`.

### Heap budget and arithmetic

The sync heap excludes the TLS session (plan section 2). Sizes are x86-64 host sizes, an
upper bound for the 32-bit device. The glue share of 11,264 B is split into four lines,
three of them asserted at compile time in the engine:

| Glue line | Bytes | How it is held to it |
| --- | --- | --- |
| run state + engine context (adapter slots, hasher, 448 B URL buffer, flags) plus the `HalFile::Impl` of up to 4 open files (3 slots + 1 hash, about 128 B each) | 2,048 | `static_assert` on the two structs against 2,048 − 4 × 128 |
| owned strings: URL reserved to 448, token copy up to 256 | 768 | `static_assert` on the constants |
| adoption hash buffer | 4,096 | the constant itself |
| transient: one HTTP request's non-TLS overhead (headers, Basic auth string, `SecureHttpClient` header list) **or** one store save's `JsonDocument`, never both, because hooks run between fetches | 4,096 | measured on device (step 12 checklist logs the lowest free heap) |
| **sum** | **11,008 ≤ 11,264** | `static_assert` on the asserted lines plus the transient constant |

Objects alive at the same moment, per phase:

- **Prepare**: glue 2,048 + 768 + 4,096, executor 3,072 (2,392 measured), compactor 640,
  compaction scratch 8,000 (`MAX_SYNC_FILES` × 4) = 18,624 B.
- **Tree download**: glue 2,048 + 768 + 4,096 + 4,096 transient, executor 3,072, tree
  snapshot writer ≤ 1,536 (borrowing the planner's share, since they never coexist;
  asserted) = 15,616 B.
- **Apply (peak)**: glue 11,008, executor 3,072, planner 51,200 (48,960 measured)
  = 65,280 B ≤ 65,536 B by shares, 62,360 B with the measured executor and planner sizes.

The same arithmetic is copied into the step 9 note of the plan when the step is done.

### Grep rule

The new device files contain no `std::hash`, `"epub_"`, `"txt_"`, `"xtc_"`,
`"/.crosspoint/bookmarks"` or `".json"` literal. Cache paths come from `bookCacheDir`
with a `BookCacheKind`; bookmark files are moved by the executor through the shared
bookmark path code; the store files are named by the stores themselves.

### Decisions taken without a `guideline` agent

All choices above follow from the plan's Rules, its step 9 text, the step notes and the
code; none needed a `guideline` call:

- extracting the orchestration into the library (the caller asked to prefer pure,
  host-tested extraction, and the executor's fault-injection storage makes the phases
  testable);
- write-through persistence (the executor's commit point, step 1.6 note);
- three consecutive network failures as the stop rule (the plan's "Wi-Fi drops
  mid-download and the next run recovers", without a timeout per remaining file);
- `RedirectRefused` fatal for the run (plan's Tree snapshot rule);
- logging a user cancel through the same single `LOG_ERR` line as every non-`Ok` end
  (the *Verified by* asks for a `LOG_ERR` on every non-`Ok` `SyncRunResult`);
- username `x` (the one step 0.0 verified).

## Implementation Chunks

1. **Budget constant and small pure helpers.** The sync budget header and the planner and
   executor assertions moved onto it; the GitHub API URL builder and constants; the blob
   file hash helper. gtest suites for the two helpers.
2. **Tree snapshot writer.** Parser plus filter plus record writer, with the cap stop and
   the status precedence. gtest suite on the recorded trees fixture.
3. **Sync run.** `runGitSync` with its transport, hooks, progress and report shapes, the
   phase order, cleanup, cancel latching, failure counting and error mapping. gtest suite
   on the in-memory storage with a fake transport.
4. **`HalStorage` adapter.** Device only; `pio run` and the contract review.
5. **Sync engine.** Transport over `HttpDownloader::fetchStream`, the reading-state
   hooks, the context and its budget assertions, the device entry point and its final
   log. Step 8 is committed, so nothing blocks it. `pio run`, the logging review and the grep.

## Testing Decisions

- A good test drives a module through its public interface only: bytes in (a trees
  body, file contents, fake transport answers, injected storage faults), and the files,
  manifest, report and callbacks out. It never inspects private state or call order
  beyond what the interface promises.
- **GitHub API builder** (new suite): the exact trees and blob URLs for the step 0.0
  repository and a SHA; a branch with `/`; the longest legal owner, repo and branch fit
  `GITHUB_API_URL_CAPACITY` exactly with their terminator; one byte less refuses with an
  empty output; an empty field refuses; SHA hex is lower-case.
- **Blob file hash** (added to the existing blob hash suite): the `git hash-object`
  vectors read through a memory stream with buffer sizes 1, 7, 64 and 4,096; a source
  shorter or longer than the expected size fails; a source returning a negative read
  fails; the tick fires once per chunk.
- **Tree snapshot writer** (new suite), on the step 3 fixture:
  - fed byte by byte and in random chunk sizes, the output is byte-identical and reads
    back with `RecordReader` as the fixture's syncable blobs, sealed, in order;
  - a fixture variant with dot paths, a malformed path and an over-long path writes
    neither and counts each;
  - `truncated: true` gives `Truncated` and an unsealed file; a body cut at any byte
    gives `Invalid`;
  - a generated tree of `MAX_SYNC_FILES + 1` syncable blobs stops with `TooManyFiles`
    and `stopped()` turns true at that entry;
  - a sink failing at its N-th write gives `WriteFailed`.
- **Sync run** (new suite), on the executor suite's in-memory storage (with its slot
  limit, case folding, FAT-name refusal and fault injection) and a fake transport that
  serves the fixture's body and blob contents in random chunks:
  - a first sync downloads everything, and a second makes no fetch and no change;
  - files pre-placed on the storage are adopted with zero blob fetches, through the
    storage's third slot;
  - truncated, invalid and over-the-cap trees end with the matching error, no change to
    the files or the manifest, and no `tree.tmp` left;
  - a leftover `tree.tmp` from a previous run is gone before the new tree is written,
    and `tree.tmp` is gone after every run, failed or not;
  - a tree fetch failure gives `TreeDownloadFailed` and changes nothing;
  - a blob `RedirectRefused` stops the run with that error after the current file, and a
    rerun with a healthy transport finishes the sync;
  - three consecutive `NetworkError`s stop with `TooManyFetchFailures`; two followed by
    an `Ok` do not; a rerun finishes;
  - the progress sink returning false during the tree phase gives `Cancelled` with no
    change; during the apply phase the run stops after the current operation and a rerun
    finishes;
  - progress arrives as `Preparing`, `FetchingTree`, `Planning`, then `Applying` with
    `done` from 1 to `total`, ticks in between;
  - a move calls `bookMoved` with absolute paths and a modify calls `clearBookCache`; a
    hook returning false stops the run with `StorageError` and a rerun repeats it;
  - storage faults at every mutating call across a whole run, each followed by a clean
    rerun, end with the same files and manifest as an uninterrupted run (reusing the
    executor suite's sweep pattern).
- **Device modules** (adapter, transport, hooks, engine) have no host tests: they depend
  on `HalStorage`, `HttpDownloader`, Wi-Fi and the global stores. They are covered by:
  - `pio run` (default environment) with no new warnings;
  - the adapter contract review (rename never overwrites, case-insensitive lookup, FAT
    names, three slots, flush limit, open-file rule);
  - the logging review listed above;
  - the grep rule above;
  - the budget assertions compiling.
- Prior art: the executor suite (in-memory storage, fault sweep, fixture end-to-end), the
  trees parser suite (fixture, random chunking, logging stub) and the config suite.

## Out of Scope

- `GitSyncActivity`, Wi-Fi connection, screens, `tr()` strings, auto-sleep and the
  input pumping code itself (step 10); the home menu entry (step 11); the user guide and
  the owner's checklist (step 12).
- Changes to `HttpDownloader` beyond calling `fetchStream`; HTTP keep-alive across
  blobs (one TLS session per file stays as it is).
- Telling a bad token from an unknown repository in the UI: step 8 exposes no status
  code.
- Making `HalFile::flush()` report errors, or making `HalStorage::open` nothrow (it
  allocates its `Impl` with `std::make_unique`); both are pre-existing HAL behaviour.
- Debouncing store saves; changing `RecentBooksStore::updatePath` to report its save.
- Any change to the planner, executor, parser or record format beyond the budget
  assertions.

## Further Notes

- **Flush limit.** Because `HalFile::flush()` returns `void`, a card error during a
  flush is only caught when an earlier write came back short. A journal frame lost that
  way reads as a torn tail on the next run and the operation is redone; a download temp
  lost that way would already have been hashed in flight, so a bad sector after the
  rename is not detected. This is accepted, as the step 1.6 note anticipated.
- **Watchdog during planning.** The planner's reads happen without a progress tick. If
  the loop task is subscribed to the task watchdog and planning a 2,000-file library on a
  slow card approaches the timeout, step 10 can add a `Planning` heartbeat through the
  executor's `localExists`; the step 12 checklist's first-sync duration will show it.
- **Open book during a sync.** The sync runs from its own activity, so the reader is not
  holding the moved book's cache files open.
- **Modify clears progress.** `clearBookCache` removes the whole cache directory,
  progress included, exactly as a web upload does (plan rule).

### Hypotheses

These were written while step 8 was in progress. Step 8 is now committed (`279bb33`, with
the origin helper in `ddc0729`); each hypothesis is checked against `HEAD` below. Two were
corrected (names only), five confirmed. Line numbers refer to `HttpDownloader.cpp` at
`HEAD` unless stated.

- **H-8-1 — corrected (name).** The entry point is
  `HttpDownloader::fetchStream(const std::string& url, const DataCallback& onData,
  const std::string& username, const std::string& password, const RequestOptions& options)`,
  static, returning `DownloadError`, no defaulted parameters (`HttpDownloader.h`, the
  `fetchStream` declaration; definition at lines 313-320). It was assumed as `streamUrl`;
  only the name changes. The URL is `const std::string&`, so the engine keeps its
  reserved URL string and the 768 B string line of the budget stands.
- **H-8-2 — corrected (field names).** `HttpDownloader::RequestOptions` is a plain
  aggregate with defaults: `const char* headerName = nullptr`, `const char* headerValue =
  nullptr`, `bool refuseCrossOriginRedirect = false` (`HttpDownloader.h`). The header is
  sent only when both strings are non-empty (`hasExtraHeader`, lines 56-58) and is added
  on every hop (line 104, inside the hop loop); the pointers must outlive the call (the
  struct's comment). The guard is active only with credentials (`guardRedirects`, lines
  60-62). Semantics are as assumed; the PRD now uses the real field names.
- **H-8-3 — confirmed.** `REDIRECT_REFUSED` is the last `DownloadError` value, after
  `ABORTED` (`HttpDownloader.h`). On the compiled `runGetWolf` path it is returned after
  `resolveUrl` and before the next hop's client is built, when the guard is on and
  `sameOrigin(startUrl, url)` is false (lines 129-135); the log line prints the status and
  the target host only. The origin test fails closed (`src/util/UrlOrigin.h`).
- **H-8-4 — confirmed.** `DataCallback` is still `std::function<bool(const uint8_t*,
  size_t)>`. The wolfSSL data lambda returns early for any status other than 200, so a
  redirect or error body never reaches it (line 109). A false return stops the transfer:
  `SecureHttpClient` marks `callbackAborted()` and still returns the 200 status
  (`freeink-sdk/libs/network/SecureNet/include/SecureHttpClient.h:294`, flag set at
  `:470`/`:475`), so the call reaches `if (http.callbackAborted()) return FILE_ERROR;`
  (line 142). `fetchStream` copies `onData` into its internal `Sink::write` (line 318);
  a one-pointer capture stays in the small buffer, so no heap.
- **H-8-5 — confirmed.** `OK` only after status 200, no callback abort and
  `responseComplete()` (lines 138-147). `HTTP_ERROR` for a bad URL (92-93), a transport
  failure (`status < 0`, 119-121), a bad redirect (126-127), any non-200 final status
  (138-140), an incomplete body (143-146) and more than 5 redirects (149-150). So a 401
  (bad token) or 404 (unknown repository) on the trees call is `HTTP_ERROR` →
  `TreeDownloadFailed`, and on a blob it counts toward `MAX_CONSECUTIVE_FETCH_FAILURES`.
- **H-8-6 — confirmed.** `Authorization` is added only when username and password are
  both non-empty (`hasCredentials`, lines 52-54, used at 99); `setUserAgent` is unchanged
  (line 98); no log line prints the credentials (the hop's debug line prints the URL
  only, line 106).
- **H-8-7 — confirmed.** `fetchStream` never sets `Sink::cancelFlag` (default `nullptr`,
  line 42; definition 313-320), so the abort callback always answers false and
  `http.aborted()` (line 118) cannot fire: `ABORTED` is unreachable from `fetchStream`.
  The engine cancels a transfer only by returning false from the data callback
  (`FILE_ERROR`), and maps `ABORTED` to `Aborted` only to keep the switch total.
