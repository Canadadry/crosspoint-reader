# Plan 1 — GitHub library sync ("Update" button)

## 1. Goal

The owner keeps their books in a **private GitHub repository**. They want to add, remove,
rename or move books by pushing to that repository, then press **Update** on the Xteink X4
so the SD card mirrors the repository, without taking the card out.

The goals behind every rule below:

- **The SD card mirrors the repository's branch**: additions, modifications, deletions,
  renames and moves all land on the card.
- **Nothing outside the synced set is ever touched**: books from USB, OPDS or the web
  server, and the whole `/.crosspoint/` directory, are safe.
- **Reading state survives a move or rename**: progress, bookmarks and recent-books entries
  follow the book.
- **It fits the hardware**: ESP32-C3, about 380 KB RAM, no PSRAM (see `CLAUDE.md`, the
  symlink to `AGENTS.md`). An interrupted sync never leaves a half-written book and resumes
  where it stopped.

## 2. Rules

### Locked decisions

- **No real git on the device.** The device does not run a git client and keeps no `.git`
  directory. It emulates `git pull` through the GitHub REST API:
  1. `GET https://api.github.com/repos/{owner}/{repo}/git/trees/{branch}?recursive=1`
     lists every path with its git blob SHA-1 and its size.
  2. The git blob SHA-1 of a local file is `SHA1("blob " + <decimal size> + "\0" + <content>)`,
     so it can be compared directly with the one GitHub returns.
  3. Only files that are new or changed are downloaded.
- **Repository**: a private repository holding plain git files. There is no Git LFS, so a
  blob download returns the real file.
- **Authentication**: a fine-grained GitHub token scoped to that one repository, with
  *Contents: Read* only. `HttpDownloader` already sends preemptive Basic auth
  (`src/network/HttpDownloader.cpp:84-88`, in `runGetWolf`), and GitHub accepts the token as the password
  of Basic auth.
- **TLS is not verified (owner decision, 2026-10-11).** The sync uses the same transport
  as OPDS, OTA and KOReader, which calls `setInsecure()` (`src/network/HttpDownloader.cpp:75`):
  traffic is encrypted, but the server certificate is not checked. The owner accepts the
  risk: on a hostile network, a man-in-the-middle could capture the token, and the token
  can only read the one books repository. Mitigations are on the owner's side (an expiring
  token, syncing on a trusted Wi-Fi). Rejected: pinning GitHub's root CAs plus a hostname
  check, because `freeink::SecureClient` never calls `wolfSSL_check_domain_name`
  (`freeink-sdk/libs/network/SecureNet/src/SecureClient.cpp:87-103`), and adding it needs
  either a fork of `freeink-sdk` or a vendored copy of `SecureNet`. That is not worth it
  for a read-only token on one repository.
- **Configuration file**: `/.crosspoint/github-sync.json`, written by the owner from a PC.
  There is no on-device editing and no keyboard entry of the token.

  ```json
  { "owner": "…", "repo": "…", "branch": "main", "token": "github_pat_…" }
  ```

  It lives under `/.crosspoint/` so that neither the file browser nor the web server shows
  the token. A missing or invalid file hides the Update entry; an invalid file is also
  logged with `LOG_ERR`.
- **Destination**: the repository's content goes to the **SD card root**. The owner chose
  this over a dedicated subfolder.
- **Filter**: a repository path is ignored when **any** of its segments starts with `.`
  (`.github/…`, `.gitignore`, `docs/.hidden/x.epub`). This also covers `.crosspoint/`,
  which must never be written, renamed or deleted, whatever the remote contains. A path
  containing an empty segment, a `..` segment or a leading `/` is rejected and logged.
- **Deletion scope**: the sync keeps a **manifest** on the SD card listing every path it
  owns with its blob SHA. It only deletes paths that are in the manifest and no longer in
  the remote tree. After deletions, a directory left empty is removed only if a synced file
  lived in it. Nothing else is ever deleted.
- **Conflicts: GitHub always wins.** The owner does not edit synced files locally. The only
  local state is CrossPoint's own data in `/.crosspoint/`.
- **Moves and renames**: a remote path that is not in the manifest, whose blob SHA matches
  a manifest path missing from the remote tree, is a move. It is applied as a local
  `Storage.rename`, with no download. Its reading state is then migrated, following the
  existing pattern `moveFinishedBookToReadFolder` (`src/activities/reader/EpubReaderActivity.cpp:127-146`):
  - **Book cache directory**: `/.crosspoint/epub_<hash>`, `txt_<hash>` or `xtc_<hash>`,
    where `<hash>` is `std::hash<std::string>{}(path)`. See `isBookCacheDirectoryName`
    (`src/util/BookCacheUtils.cpp:9-21`) and `lib/Txt/Txt.cpp:9-10`.
  - **Bookmark file**: `BookmarkUtil::getBookmarkPath(bookPath)` (`src/util/BookmarkUtil.cpp:8`).
  - **Recent books**: `RECENT_BOOKS.updatePath` (`src/RecentBooksStore.h:48`).
  - **Open book**: `APP_STATE.openEpubPath`.

  If several missing paths share the same SHA, they are paired deterministically
  (lexicographic order). A plain modification (same path, new SHA) clears that book's
  cache with `clearBookCache` (`src/util/BookCacheUtils.cpp:23`), like the web server does
  on upload.
- **First sync and adoption**: the first sync is prepared from a PC. The owner copies the
  repository's files to the card root and writes the config file. At the first Update, a
  remote path that is not in the manifest but exists locally with the same blob SHA is
  **adopted**: it is recorded in the manifest and not downloaded. A local file that exists
  with a different SHA is overwritten (GitHub wins). Adoption reads the SD card, not the
  network, so the first sync takes seconds to minutes, not hours.
- **Atomic writes and resume**:
  - every download goes to a temporary file under `/.crosspoint/gitsync/` and is then
    renamed onto its target;
  - after each applied operation, the manifest is updated and persisted;
  - an interrupted run (power loss, Wi-Fi drop, Back) leaves the manifest consistent with
    the files on disk;
  - the next run redoes only what is left;
  - leftover temporary files are deleted when a sync starts.
- **Tree snapshot**: every blob is downloaded through
  `GET /repos/{owner}/{repo}/git/blobs/{sha}` with `Accept: application/vnd.github.raw+json`.
  This endpoint is content-addressed, so a push that happens during the sync cannot mix two
  versions. `HttpDownloader` cannot set an `Accept` header today, so one optional header
  parameter must be added. The response must answer 200 without a redirect. If GitHub
  redirects to another host, the sync stops with an error rather than resend the token
  there: `HttpDownloader` re-adds the `Authorization` header on every redirect hop
  (`src/network/HttpDownloader.cpp:72-114`). Every firmware build defines
  `FREEINK_NET_WOLFSSL=1` (`platformio.ini:41`), so the compiled path is `runGetWolf`
  (`HttpDownloader.cpp:66-128`); the `esp_http_client` path (`131-237`) is dead code in
  these builds.
- **Truncated tree**: if the trees response carries `"truncated": true` (more than 100,000
  entries or more than 7 MB), the sync aborts with an error message and changes nothing.
- **Streaming, not buffering**:
  - the trees response is parsed as it arrives with `StreamingJsonParser`
    (`lib/JsonParser/StreamingJsonParser.h`, 512-byte token buffer) and written as records
    to an SD temp file;
  - neither the raw JSON nor the full list of paths is held in RAM;
  - entries kept in RAM for planning are fixed-size (SHA bytes plus a file offset, no
    `std::string`);
  - peak sync heap, excluding the TLS session, must stay at or below 64 KB;
  - the supported file count is capped by that budget: the implementer documents the cap
    and refuses a larger tree with a clear message.
- **SHA-1**: a small portable SHA-1 lives in `lib/`, so the host unit tests and the device
  share one implementation, checked against `git hash-object` vectors. mbedTLS SHA-1 is
  not used because the host tests cannot link it.
- **UI**:
  - **Home menu**: one **Update** entry, shown only when the config file exists. It is
    built like the OPDS entry (`src/activities/home/HomeActivity.cpp:309-317`).
  - **Wi-Fi**: the connection follows the OPDS browser's flow (`checkAndConnectWifi`,
    `launchWifiSelection`, `onWifiSelectionComplete` in
    `src/activities/browser/OpdsBookBrowserActivity.cpp:597-620`).
  - **During the sync**: a progress screen shows the current step and `n/total : file`.
  - **Cancel**: Back cancels between files.
  - **End of sync**: a summary shows the added, updated, moved, deleted and adopted counts,
    or the error.
  - **Auto-sleep**: blocked for the whole run with `preventAutoSleep()`
    (`src/activities/Activity.h:45`).
  - **Strings**: every user-facing string uses `tr()`, with English and French entries in
    `lib/I18n/translations/`.
- **Codebase conventions**: everything in `CLAUDE.md` applies, in particular HAL-only SD
  access, `makeUniqueNoThrow`, no bare `new`, no `std::function` in new library code,
  `constexpr` tables and no `file.close()` on local handles.

### Options rejected and why

- **A real git client** (libgit2, a hand-written smart-HTTP client): packfiles carry deltas
  whose resolution needs base objects in memory, which multi-MB EPUBs and about 380 KB RAM
  rule out.
- **Keeping a `.git` directory on the card**, from a PC `git clone` updated by the device:
  the device would change files without updating `.git`, so the next PC `git pull` would
  refuse with "local changes would be overwritten". `.git` also doubles the space used. The
  user guide tells the owner to copy files rather than clone onto the card, or to delete
  `.git` afterwards. Since the filter ignores dot paths, a leftover `.git` is never touched
  by the sync.
- **Downloading a zip or tarball of the branch**: it re-downloads the whole library on every
  Update and needs room for the archive plus the extraction.
- **Typing the token on the device**: 90+ characters on the on-screen keyboard. The owner
  prepares the card from a PC anyway.
- **A dedicated subfolder such as `/Books/`**: rejected by the owner. The manifest and the
  dot filter give the safety a subfolder would have given.
- **Filtering by book extension**: rejected in favour of the dot filter, so repository
  files of any type are mirrored.
- **Comparing every local file's SHA on every sync**: the manifest is trusted, because
  GitHub wins and the owner does not edit synced files locally. Local hashing happens only
  for adoption (a remote path not in the manifest that exists locally).

### Out of scope

- Pushing anything from the device to GitHub: the sync is one-way.
- Public repositories without a token, other hosts (GitLab, Gitea), several repositories,
  and Git LFS.
- Automatic or scheduled sync: only the button triggers it.
- Editing the config on the device.

## 3. Phases and steps

**Verification commands for this repository.** These are also the commands the skills
in `.claude/skills/` use (`git-commit`, `tdd`, `check-build`):

- **Host unit tests (gtest)**: `cmake -S test -B build/test -G Ninja && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j`.
  A new suite is registered with `add_subdirectory` in `test/CMakeLists.txt`; see
  `test/streaming_json_parser/` for the layout.
- **Firmware build**: `pio run` (the `default` environment).
- **Formatting**: `./bin/clang-format-fix -g`.
- **On-device checks**: hardware tests are not available to the agent; they are listed
  for the owner in step 12.

### Phase 0 — De-risk the GitHub API

0. **Owner check of the API from a PC, before step 3.** With the real token, the owner runs:
   - `curl -i -u x:<token> "https://api.github.com/repos/{owner}/{repo}/git/trees/{branch}?recursive=1"`;
   - `curl -i -u x:<token> -H "Accept: application/vnd.github.raw+json" https://api.github.com/repos/{owner}/{repo}/git/blobs/{sha}`,
     on one real file.

   Record in section 6, for each call, the status, whether it redirects (and where), and
   whether the blob body is the raw file. If either call redirects or fails, settle the
   fallback of section 5 in the plan before step 3, including where the commit SHA comes
   from.
   *Verified by*: the recorded results in section 6. Unattended runs stop here until they
   exist.

### Phase 1 — Pure logic in `lib/GitSync/`, host-tested

1. **Git blob hash.** An incremental SHA-1 with `begin(size)` / `update(bytes)` /
   `finish() → 20 bytes`, which hashes `"blob <size>\0"` first. Hex helpers convert between
   40-char hex and 20 bytes.
   *Verified by*: gtest vectors produced with `git hash-object`, covering the empty file,
   `"hello\n"`, a 1 MiB pattern fed in uneven chunks, and a size equal to a multiple of 64.
2. **Path filter.** `isSyncablePath(path)` rejects any segment starting with `.`, empty
   segments, `..`, a leading `/` and over-long paths; it accepts nested UTF-8 names.
   *Verified by*: gtest table, including `.crosspoint/x`, `a/.git/b`, `a//b`, `a/../b`,
   `Livres/Été.epub`.
3. **Trees response parser.** It is fed chunks of the GitHub trees JSON and emits a
   `(path, type, sha, size)` record per `"blob"` entry through a plain function-pointer
   callback, and reports `truncated` and the tree's own `sha`. Built on `StreamingJsonParser`.
   A blob entry whose path overflowed the parser's token buffer is skipped and logged,
   never emitted: `StreamingJsonParser` silently drops a string of 512 bytes or more
   (`lib/JsonParser/StreamingJsonParser.cpp:226-240`).
   *Verified by*: gtest with a recorded fixture fed byte by byte and in random chunk sizes,
   checking that `tree` and `commit` entries are skipped, that `truncated: true` is
   detected, and that an over-long path is skipped without corrupting the next entry.
4. **Manifest and tree record format.**
   - **Records**: a versioned record file of `(sha[20], size, path)`, with read and write
     over a small stream interface so that tests use memory and the device uses `HalFile`.
   - **Both files**: the same format serves the manifest and the streamed tree
     (`tree.tmp`), including reading a path back from a record offset, which is how the
     planner refers to entries.
   - **Layout**: aligned reads only (`memcpy`), per the RISC-V rule in `CLAUDE.md`.
   - **Corruption**: a corrupt or wrong-version file is reported, never half-applied.

   *Verified by*: gtest covering the round trip, offset read-back, truncation at every
   byte offset, and a version mismatch.
5. **Sync planner.**
   - **Inputs**: the manifest entries, the filtered remote entries, and a callback
     `localExists(path)`. The planner never hashes: hashing is an executor operation.
   - **Output**: an ordered operation list:
     1. moves (rename plus cache migration);
     2. adopt-or-download (a remote path not in the manifest that exists locally: the
        executor hashes it, then adopts it or downloads it);
     3. downloads (new or changed);
     4. deletes (in the manifest, gone remotely);
     5. prunes (empty directories that held synced files).

     Moves run before downloads, so a path freed by a move can be refilled in the same run.
   - **Memory**: it respects the fixed-size memory rule and documents the file-count cap.

   *Verified by*: gtest scenarios covering:
   - no change gives an empty plan;
   - an add;
   - a modify;
   - a delete, plus a directory prune;
   - a rename within a folder, and a move across folders;
   - a swap of two files' names;
   - two missing paths with the same SHA;
   - a remote path not in the manifest that exists locally yields adopt-or-download;
   - a remote `.crosspoint/…` entry, which is ignored;
   - a rename that only changes case, and a remote tree with two paths differing only by
     case: the chosen outcome (success, per-file error or skip) is pinned by the test, and
     an error never blocks the rest of the sync, never repeats on every run and never
     deletes the file;
   - a cap overflow, which is refused.

6. **Sync executor**, in `lib/GitSync/` behind a small storage interface (memory in tests,
   `HalStorage` on the device). It runs a plan from step 5:
   - each download is hashed with the step 1 incremental blob hash as it is written to a
     temp file; a size or SHA mismatch with the tree entry deletes the temp file, reports a
     per-file error and leaves the manifest unchanged;
   - each verified download is renamed onto its target; when it replaces a
     changed path, that book's cache is cleared (through an injected hook) before the
     manifest is persisted;
   - parent directories are created as needed;
   - adopt-or-download hashes the local file through an injected hash callback, then
     records it in the manifest if the SHA matches, or downloads it otherwise;
   - the manifest is persisted after each operation;
   - deletes and prunes are applied;
   - a move renames the file, then migrates the reading state before the manifest is
     persisted:
     - the cache directory and bookmark file are renamed through the storage interface,
       for epub, txt and xtc;
     - recent books and app state are updated through one injected hook;
   - a cancel flag is honoured between operations.

   Downloads go through an injected fetch callback, so no network is needed in tests. A
   size-0 remote entry is written as an empty file without fetching:
   `HttpDownloader::downloadToFile` treats an empty body as an error
   (`src/network/HttpDownloader.cpp:309-313`).
   The in-memory storage's rename fails when the destination exists, like SdFat's
   (`src/activities/reader/ProgressFile.h:53-56`), so every overwrite, the manifest's own
   save included, is a remove then a rename, and the fault injection covers that window.
   It also follows FAT naming: path lookup ignores case, and names with FAT-forbidden
   characters (`: * ? " < > | \`) are rejected.
   *Verified by*: gtest showing that:
   - with in-memory storage that fails at its N-th mutating call (write, rename, remove,
     mkdir, manifest persist), where a failing write leaves a partial prefix of its bytes
     (a torn file), stopping at every N and running again ends with the same
     files and manifest as an uninterrupted run, including a plan of adopt-or-download
     operations where a rerun does not re-hash files already adopted;
   - an adopt-or-download plan at the documented file-count cap: the in-memory storage
     counts the bytes written, and the total is recorded in the step notes next to the RAM
     cap, so that a manifest rewrite cost growing with the square of the file count is
     caught here;
   - a file outside the manifest and anything under `.crosspoint/` are never touched;
   - a fetch returning wrong or truncated bytes leaves no file at the target and no
     manifest entry, and a rerun fetches it again;
   - a failed download leaves no partial file at the target;
   - a run interrupted between a move's file rename and its migration still migrates the
     reading state on the next run, and the same holds for a modify and its cache clear;
   - the expected migration paths come from the reader's own code, applied to the
     absolute SD path (`BookmarkUtil::getBookmarkPath`, the epub, txt and xtc cache-path
     derivation), either linked into the test or pinned as vectors those functions
     produce, never re-derived by the test author;
   - an end-to-end chain, from a recorded trees fixture through the step 3 parser, the
     tree record stream, the planner and the executor, on in-memory storage, ends with the
     expected files and manifest; the fixture includes a size-0 file.

### Phase 2 — Device integration

7. **Config loader.** `/.crosspoint/github-sync.json` is parsed into
   `{owner, repo, branch, token}`, with validation (non-empty fields, branch defaulting to
   `main`). It exposes `exists()` for the home menu. The token is never logged.
   *Verified by*: gtest on the parsing function with valid, missing-field and malformed
   inputs, plus `pio run`.
8. **HTTP additions.** `HttpDownloader` gains:
   - an optional extra request header for the `Accept` value;
   - a "no cross-host redirect when credentials are set" guard.

   Both land on the compiled `runGetWolf` path, and on the `esp_http_client` path too if
   it is kept. Existing callers are unchanged.
   *Verified by*: `pio run`; a review naming the `#if` branch each change lands in; and a
   review that OPDS, OTA and KOReader call sites compile unchanged.
9. **Sync engine glue** (`src/network/GitSync*.cpp` or `src/util/`). It wires the step 6
   executor to the device:
   - the `HalStorage` implementation of its storage interface;
   - deleting leftover temporary files;
   - streaming the tree to `/.crosspoint/gitsync/tree.tmp`, and aborting on `truncated`;
   - the hash callback used by adopt-or-download, reading the local file through
     `HalStorage` in 4 KB chunks;
   - the fetch callback over `HttpDownloader`;
   - a review that the `HalStorage` implementation keeps the storage interface's contract
     (rename never overwrites, case-insensitive lookup, FAT-forbidden names rejected);
   - the device side of the move hook (`RECENT_BOOKS.updatePath`,
     `APP_STATE.openEpubPath`), following `EpubReaderActivity.cpp:127-146`, with paths in
     the same absolute form the reader stores;
   - the device side of the modify hook (`clearBookCache`);
   - progress reporting through a function-pointer-plus-context callback.

   *Verified by*: `pio run` and a code review against the rules in section 2, plus:
   - the review names each status-returning call the glue makes and the `LOG_ERR` that
     handles its failure: `GitTreeParser::hasError()`, `isTruncated()` and
     `wasAborted()`; every non-`Ok` `SyncPlanStatus`; every `RecordStatus` other than
     `Record` and `End`; every non-`Ok` `SyncRunResult`;
   - a grep of the step's diff finds no `std::hash`, `"epub_"`, `"txt_"`, `"xtc_"`,
     `"/.crosspoint/bookmarks"` or `".json"` literal in the new `src/` glue: cache paths
     come from `bookCacheDir` / `formatBookCacheDir` (`lib/BookCachePath/`), bookmark
     paths from `BookmarkUtil::getBookmarkPath`;
   - one named budget constant in `lib/GitSync/` replaces the 64 KB literals in the
     `SyncPlanner` and `SyncExecutor` `static_assert`s, and the step notes write out the
     arithmetic showing that the objects the glue holds at the same moment fit it.
10. **`GitSyncActivity`.** It handles the Wi-Fi connect flow, the progress screen, Back as
   cancel, the summary or error screen, and `preventAutoSleep() == true`. Its tr() strings
   go in `english.yaml` and `french.yaml`. The sync runs in the context that already runs
   wolfSSL downloads on this device: the activity loop, as OPDS does
   (`OpdsBookBrowserActivity.cpp:483-520`) and OTA does (`src/network/OtaUpdater.cpp`),
   with input pumped from the progress callback so Back can cancel. If a dedicated task is
   used instead, its stack is sized from that context, not from the generic 4096-byte rule,
   and it is deleted in `onExit()`.
   *Verified by*: `pio run`, `./bin/clang-format-fix -g`, and a review of orientation
   handling in all four modes.
11. **Home menu entry.** **Update** is shown only when the config exists, with its own icon
    and index mapping updated in `menuItemToIndex` / `indexToMenuItem`.
    *Verified by*: `pio run`, and a review that the menu index math still holds with and
    without OPDS servers and recent books.

### Phase 3 — Documentation and hand-off

12. **User guide.** Add a section to `USER_GUIDE.md` covering:
    - creating the fine-grained token (one repository, Contents: Read);
    - writing `/.crosspoint/github-sync.json`;
    - the first sync from a PC (copy the files, do not clone onto the card or delete
      `.git`, then press Update to adopt);
    - what Update changes and what it never touches.

    End the plan's notes with the owner's on-device checklist:
    - the first sync adopts everything with zero downloads;
    - an add and a modify;
    - a delete with its folder pruned;
    - moving the book currently being read keeps its progress and bookmarks;
    - Back mid-sync, then Update again, resumes;
    - Back during the first sync's adoption, then Update again, resumes without re-hashing
      the files already adopted;
    - the duration of the first sync, recorded with the library's file count;
    - the stack high-water mark of the sync's context (`uxTaskGetStackHighWaterMark`),
      logged after a run that includes a TLS fetch, with at least 1 KB left;
    - Wi-Fi drops mid-download and the next run recovers;
    - free heap stays at or above 50 KB during a sync (`LOG_DBG` `ESP.getFreeHeap()`);
      the log shows free heap before the sync and the lowest value during it, so the
      measured peak can be compared with the 64 KB budget;
    - a push that adds at least 50 small files, then Update: Added = N, no reboot and no
      "Not enough memory"; record the lowest free heap; the serial capture has no
      `abort()` and no `Guru Meditation`; then remove the files from the repository (a
      bulk delete);
    - with File Transfer running, `/download?path=/.crosspoint/github-sync.json`, WebDAV
      `GET /.crosspoint/github-sync.json` and WebDAV `PROPFIND /.crosspoint` all answer
      403, with no token text and no listing.

    *Verified by*: the doc renders and every step matches the implemented behaviour; the
    guide's File Transfer statement is checked against every `WebDAVHandler::handle*` and
    every `server->on` path handler.

## 4. Done when

- `ctest --test-dir build/test` passes, including the new GitSync suites.
- `pio run` (default environment) builds without new warnings.
- `./bin/clang-format-fix -g` leaves no diff.
- CI on the pull request is green (`.github/workflows/ci.yml`, `pr-formatting-check.yml`).
- `USER_GUIDE.md` documents the setup, and the on-device checklist is handed to the owner.

## 5. Open questions

- **Library size is unknown.** The owner does not know how many books or how many GB. The
  file-count cap and the 64 KB budget in section 2 are the implementer's call. If the
  measured cap is below about 2,000 files, the implementer must say so in the step notes
  so the owner can decide.
- **The blob endpoint is unconfirmed.** The plan assumes that the `git/blobs/{sha}`
  endpoint with the raw `Accept` header answers 200 directly for private repositories,
  with Basic auth. If it redirects, fall back to
  `GET /repos/{owner}/{repo}/contents/{path}?ref={commit}` with the same header, and keep
  the no-cross-host-redirect guard.
## 6. Step notes

### Step 0.0

Done 2026-10-11 by the owner, with curl from macOS and a fine-grained token sent as Basic auth `x:<token>`.

- Repository: `Canadadry/books`, branch `main`.
- `GET /repos/Canadadry/books/git/trees/main?recursive=1` returns **200** with no redirect.
  The response carries the tree `sha` at the top level, then `tree[]` entries with `path`,
  `mode`, `type` (`tree` or `blob`), `sha`, `size` (blobs only) and `url`.
- `GET /repos/Canadadry/books/git/blobs/cc858913c6f1452c9cbd67097e1d97d4a4450a08` with
  `Accept: application/vnd.github.raw+json` returns **200** with no redirect, and 133,386
  bytes, equal to the tree's `size`. The body is the raw file, not JSON or base64, and
  `git hash-object` on it gives back `cc858913…`, so the step 1 blob hash and the
  per-download verification hold.
- Section 5's fallback is not needed: the blob endpoint is confirmed for this private
  repository.
- The repository has a `.atelier/` working directory of Markdown files, which the dot
  filter excludes as intended.

### Step 1.1

- `lib/GitSync/GitBlobHasher.{h,cpp}`: `GitBlobHasher` with `begin(uint64_t size)`,
  `update(const uint8_t*, size_t)` and `bool finish(uint8_t out[20])`, plus the free
  functions `shaFromHex(const char*, size_t, out)` and `shaToHex(sha, char[41])`. No
  namespace, no heap, no logging, no Arduino include; about 120 bytes per hasher.
- `finish()` returns false and zero-fills the output when the bytes fed differ from the
  declared size: step 6 gets its size check from it.
- `shaFromHex` takes an explicit length (JSON parser strings are not null-terminated) and
  accepts upper- or lower-case hex.
- Tests: `test/git_sync_blob_hash/` (20 cases), with vectors from `git hash-object`.
- Build net: `pio run -e default` green, no warning in `src/` or `lib/`; formatting clean
  with clang-format 21. The sandbox needed the `pioarduino` PyPI package and the proxy CA
  in certifi to build; nothing in the repository changed for it.
- No `guideline` decisions.

### Step 1.2

- `lib/GitSync/SyncPath.{h,cpp}`: `classifySyncPath(const char*, size_t)` returns
  `Syncable`, `Hidden`, `Malformed` or `TooLong`; `isSyncablePath` is the boolean shortcut.
  The caller logs `Malformed` and `TooLong`, and skips `Hidden` quietly.
- `MAX_SYNC_PATH_BYTES = 255` UTF-8 bytes: below the trees parser's 512-byte token buffer,
  and every segment then fits SdFat's 255-unit long name limit.
- Beyond the plan's list, a segment starting with a space and any byte below 0x20 are
  `Malformed`: SdFat skips leading spaces, so ` .crosspoint/x` would have reached
  `/.crosspoint/`. FAT-forbidden printable characters and case clashes stay with steps 5/6.
- Tests: `test/git_sync_path_filter/` (13 cases). Build net green.
- No `guideline` decisions.

### Step 1.3

- `lib/GitSync/GitTreeParser.{h,cpp}`: `GitTreeParser(ctx, onRecord)` fed in chunks;
  `GitTreeRecord {path, pathLen, sha[20], size}` per blob through
  `bool (*)(void*, const GitTreeRecord&)`, returning false aborts. `isComplete()`,
  `isTruncated()`, `hasError()`, `wasAborted()`, `treeSha()` and three counters.
  About 1.1 KB, no heap: held as a member or through `makeUniqueNoThrow`.
- The parser does not filter paths: the caller applies `classifySyncPath`.
- Records are usable only when `isComplete() && !isTruncated()`; a cut-off body is never
  complete, so it cannot turn into deletions.
- A blob with a missing or invalid SHA, size, path or type fails the whole response
  rather than being skipped, since a skipped entry would look deleted to the planner.
- Over-long paths (512+ bytes, dropped by `StreamingJsonParser`) and paths still holding
  a backslash after decoding (`\uXXXX` is not decoded) are skipped, counted and logged
  by SHA. Neither can be in the manifest, so the skip never deletes anything.
- Tests: `test/git_sync_tree_parser/` (35 cases) with `TreeFixture.h`, a body exported
  from a real git repository, with the content of each blob, reusable by step 6. The
  over-long cases use separate small bodies so the fixture stays a real tree.
- First GitSync module to log: host tests use a no-op `stubs/Logging.h`.
- Build net green. No `guideline` decisions.

### Step 1.4

- One format for `tree.tmp` and the manifest (`lib/GitSync/RecordFormat.h`, documented in
  `docs/file-formats.md`, `RECORD_FILE_VERSION = 1`): an 8-byte header, then CRC-32 frames
  (entry `sha, size, replaces, path`; drop `target`; seal `count`). A record's offset is
  its frame's position; `RecordReader::readEntryAt(offset)` reads it back, CRC-checked.
- A tree is entries plus one seal; an unsealed file reads `Truncated`.
- The manifest is a sealed base plus an append-only journal: a modify or move is one
  entry frame with `replaces`, and a delete is one drop frame. Persisting one operation
  appends 39 + path bytes (entry) or 11 bytes (drop), so a run's writes stay linear.
- A frame cut off at the end of the journal is a torn tail: it reads `End` with
  `tornTail()`, and `endOffset()` gives where to resume. Flipped kind or length bytes
  read `Corrupt`, which is the stricter choice; a crash only ever leaves a prefix.
- `ManifestCompactor` folds the journal into a new sealed base in two passes over a
  caller-supplied `uint32_t` array (4 B per frame target). Step 6 runs it at sync start,
  into a temp file, then remove and rename.
- Streams: `RecordSource` (`read`, `seek`) and `RecordSink` (`write`), with
  `MemoryRecordStream` for tests. Sizes: reader 320 B, writer 320 B, compactor 640 B,
  held as members or through `makeUniqueNoThrow`. Nothing logs or allocates.
- Tests: `test/git_sync_record_format/` (61 cases), covering truncation and a source
  error at every byte, bit flips, and the grammar rules. Build net green.
- No `guideline` decisions.

### Step 1.5

- `lib/GitSync/SyncPathFold.{h,cpp}`: a port of SdFat 2.3.1's `toUpcase` as `constexpr`
  tables (checked equal over 0..0xFFFF), `syncPathsEqualFolded`, `syncPathKey`
  (FNV-1a over folded units) and `isFatSafeName`.
- `lib/GitSync/SyncPlanner.{h,cpp}`: `plan(manifest, tree, localExists, existsCtx, sink)`
  with `SyncOpSink {ctx, onOp, onTotal}`. `onTotal` is called once before the first op,
  0 included, so the progress screen can show n/total. Ops are emitted in this order:
  Move, AdoptOrDownload, Download, Delete, Prune, Skip.
- **Cap: `MAX_SYNC_FILES = 2000`, holding even when every file moves.** Each file costs
  one 12-byte slot (4-byte SHA prefix, folded path key, 24-bit offset plus state), and
  there are 2 × cap slots. `sizeof(SyncPlanner)` is 48,960 B, with a `static_assert` at
  51,200 B. Every prefix or key match is confirmed by reading the record back, and SD
  reads stay at or below 5 × entries + 16.
  The first PRD draft gave 1,300; I had it reworked to reach the plan's ~2,000.
- Outcomes pinned by the tests:
  - a case-only rename with the same SHA is a no-op;
  - a swap is two replacing Downloads;
  - for remote paths that differ only by case, the manifest's path or the byte-wise
    smallest one wins, and the others give a `Skip(CaseCollision)` on every run, never
    a delete;
  - a FAT-forbidden name gives a `Skip(UnsafeName)`;
  - a move within a folder gets no Prune;
  - the existence table: source only → Move; destination only → Move to resume; both
    or neither → Delete plus AdoptOrDownload or Download.
- The planner requires a compacted manifest (`ManifestNotCompacted` otherwise). The
  caller allocates it with `makeUniqueNoThrow` and frees it after the plan.
- Tests: `test/git_sync_path_fold/` (11 cases) and `test/git_sync_planner/` (51 cases).
  Build net green. No `guideline` decisions.

### Step 1.6

- `lib/GitSync/SyncExecutor.{h,cpp}`: `prepare()` creates the work dir, removes the temp
  file, recovers `manifest.new` and compacts a journal or a torn tail; then
  `apply(planner)`, or `begin`/`execute`/`end` for hand-made ops, and `requestCancel()`.
  Callbacks (`fetch`, `hashFile`, `clearBookCache`, `bookMoved`, `onProgress`) are
  function pointers sharing one ctx. Progress reports n/total from the planner's `onTotal`.
- Commit point: each operation's journal frame is its last write, and every earlier
  step is idempotent. A rerun after a fault at any mutating call ends with the same files,
  manifest and hook effects (fault sweep over a mixed plan and over an adopt-only plan).
- Downloads are hashed while streaming into `download.tmp`, refused past the declared
  size, and verified before the rename. Size 0 needs no fetch. Overwrite is remove then
  rename. A failed fetch or verification is a per-file error; failing storage, journal
  writes or hooks are fatal.
- Moves rename the book, then its epub/txt/xtc cache dirs and its bookmark, using
  `lib/BookCachePath` (now shared with the reader, byte-identical) and
  `formatBookmarkPath` (checked against the linked `BookmarkUtil`). Then comes the
  `bookMoved` hook. A resume is decided from what is on the card.
- **Bytes written at the 2,000-file cap**:
  - adopt-only first sync: 142,019 B (19 + 2,000 frames);
  - next run: the same 142,019 B, from the one compaction;
  - the run after that: 0 B.
  Linear, not quadratic. RAM: `SyncExecutor` 2,392 B (x86-64 host), compaction scratch
  8 KB transient.
- For step 2.9's HalStorage adapter: `HalStorage::open` passes SdFat flags through
  (Append = `O_RDWR|O_APPEND`), but `HalFile::flush()` returns `void`, so flush errors
  are invisible on the device. Every open allocates a `HalFile::Impl`. The device's
  `clearBookCache` clears by file extension; the executor clears a destination cache
  before any migration rename so a migrated epub cache is not wiped.
- Tests: `test/git_sync_executor/` (78 cases, about 2 s). Build net green. No `guideline`
  decisions.

### Step 2.7

- `lib/GitSync/GitSyncConfig.{h,cpp}`: `parseGitSyncConfig(json, len, GitSyncConfig&)`
  over `StreamingJsonParser`, with fixed buffers: owner 39, repo 100, branch 255,
  token 255 (653 B, never a stack local). An absent branch becomes `main`; unknown keys
  are ignored; any failure zeroes the output. Statuses: Ok, NotFound, ReadError,
  TooLarge (over 1 KB), Malformed, MissingField, InvalidField and OutOfMemory, with the
  field named.
- URL-safe alphabets, so step 9 concatenates URLs without escaping: owner
  `[A-Za-z0-9-]`, repo `[A-Za-z0-9._-]` (not `.` or `..`), branch `[A-Za-z0-9._/-]`
  plus git's ref rules, token visible ASCII without `\`.
- `src/network/GitSyncConfigLoader.{h,cpp}`: `git_sync_config::load(config)` and
  `exists()`. `exists()` means present and valid, re-read on each call because USB mode
  can rewrite the file. Logs are the constant verdict text and, on success, owner, repo
  and branch at debug level; the token is never logged, and buffers are wiped with
  volatile stores.
- JSON engine: `StreamingJsonParser`, not ArduinoJson (host-testable, no `String`).
- Tests: `test/git_sync_config/` (36 cases). Build net green. No `guideline` decisions.
- End of phase 1 (`warn-review`, then `plan-review`), three accepted method changes:
  step 9's *Verified by* gains a logging review of every status, a grep against inline
  cache and bookmark path derivation, and one named 64 KB budget constant with the
  arithmetic written in its note; step 12's checklist logs the lowest free heap during
  a sync. The other warnings (duplicated path literals, header hygiene, naming) were
  judged not to change the odds of the remaining steps.

### Step 2.8

- `src/util/UrlOrigin.{h,cpp}`: `parseUrlOrigin` and `sameOrigin` compare scheme, host
  (ignoring case) and effective port; they fail closed on userinfo, IPv6 literals and bad
  ports. Pure and host-tested (`test/url_origin/`, 7 cases).
- `HttpDownloader` gains `RequestOptions {headerName, headerValue,
  refuseCrossOriginRedirect}`, `REDIRECT_REFUSED` (at the end of `DownloadError`) and
  `fetchStream(url, onData, username, password, options)`.
  - The guard is opt-in, and active only with credentials. OPDS sends credentials and
    its downloads may redirect, so a default-on guard would break it.
  - A same-origin redirect is followed with credentials, as before.
- `#if` branches:
  - `FREEINK_NET_WOLFSSL` (`runGetWolf`, compiled by every env): the header is added on
    every hop; the guard runs after `resolveUrl`, before the next hop's client is built,
    and logs only the status and the target host.
  - `!FREEINK_NET_WOLFSSL` (`runGet`, not compiled): `esp_http_client_set_header`, and
    with the guard on plus credentials, every redirect is refused (fail closed). Checked
    by reading only.
- Call sites unchanged and compiled:
  - OTA: `OtaUpdater.cpp:43`, `:162`;
  - OPDS: `OpdsBookBrowserActivity.cpp:364`, `:495`;
  - fonts: `FontDownloadActivity.cpp:129`, `:451`.
  KOReader does not use `HttpDownloader`. Nothing switches on `DownloadError`.
- Build net green. No `guideline` decisions.

### Step 2.9

- Pure parts in `lib/GitSync/`, host-tested:
  - `SyncBudget.h`: `SYNC_HEAP_BUDGET_BYTES = 65536`, split into planner 51,200,
    executor 3,072 and glue 11,264 (tree writer 2,048, borrowed from the planner's share
    since the two never coexist). The planner and executor `static_assert`s use it.
  - `GitHubApi`: trees and blob URLs in a 448 B buffer; Accept constants; Basic user `x`.
  - `GitBlobSourceHash`: hashes a local file in caller-supplied 4 KB chunks.
  - `TreeSnapshotWriter`: parser, `classifySyncPath` and `RecordWriter` into
    `tree.tmp`. It stops the download at the first file past the cap and seals only a
    complete, untruncated tree. Rejected paths are logged by SHA, never by text.
  - `GitSyncRun`: `runGitSync(storage, transport, hooks, progress)` runs prepare, tree,
    plan and apply, then removes `tree.tmp`. It latches cancel, stops after three
    consecutive network errors or a refused redirect, and logs one end line for every
    outcome that is not Ok. The report is `GitSyncReport {GitSyncError, SyncRunResult,
    tree counts}`, with 11 `GitSyncError` values.
- Device parts in `src/network/`, checked by `pio run` and review:
  - `HalSyncStorage` gives three fixed `HalFile` slots. Rename checks `exists(to)`, and
    SdFat's `O_EXCL` backs it up. FAT-unsafe names are refused. `flush()` reports a
    latched short write. Known limits: case folding is SdFat's own, and the "open file"
    rule is kept by the executor's call order.
  - `GitSyncEngine` (`git_sync_engine::run(config, progress)`) goes through
    `HttpDownloader::fetchStream` with the guard on. The move hook calls
    `RECENT_BOOKS.updatePath` with `bookCacheDir` paths, then sets and saves
    `APP_STATE.openEpubPath`; the modify hook is `clearBookCache`. It refuses an empty
    token and checks the heap before reserving its two strings.
- Logging review: every status-returning call in the glue has a `LOG_ERR`, listed in the
  chunk 5 report. `RecentBooksStore::updatePath` and `clearBookCache` return void.
- Grep of the new `src/` files: no `std::hash`, `epub_`/`txt_`/`xtc_`, or bookmarks
  literal. The only `.json` hit is the step 2.7 config path.
- **Peak heap**, objects alive at the same moment, TLS excluded, device sizes measured
  on RISC-V (planner 48,920, executor 2,312, tree writer 1,552, engine context 592):

  | Phase | By shares | Measured |
  | --- | --- | --- |
  | Prepare (executor, compactor, 8 KB scratch, glue state) | 18,624 B | 17,864 B |
  | Tree download (glue, executor, tree writer) | 16,128 B | 14,376 B |
  | Apply (glue, executor, planner) | 65,280 B | 61,744 B |

  Apply is the peak, within the 65,536 B budget. The glue's 4,096 B transient line
  (one HTTP request or one JSON save) can only be measured on the device (step 12).
- Tests: `git_sync_github_api` (13), `git_sync_tree_snapshot` (13), `git_sync_run` (32),
  plus 7 hash cases. Build net green. No `guideline` decisions.

### Step 2.10

- `src/activities/network/GitSyncActivity.{h,cpp}`, opened by
  `ActivityManager::goToGitSync()` with `replaceActivity`, so the home activity is freed
  first. States: ConfigError, WifiSelection, WifiFailed, Starting, Running, Done.
- `onEnter` loads the config; an invalid one shows a fix-it screen and Wi-Fi never
  starts. The Wi-Fi flow is the OPDS one, and the run starts from `loop()` on the
  activity task, as OPDS and OTA do; no new task.
- The progress callback:
  - feeds the watchdog;
  - pumps `mappedInput` (a Back release cancels between files; the home gesture
    cancels, then goes home);
  - tracks the lowest free heap;
  - publishes a fixed snapshot of at most 288 B under `RenderLock`, on a phase change,
    on the first cancel, or otherwise every 2,000 ms;
  - allocates nothing.
- Running screen: the step, files listed while reading the tree, a progress bar, and
  `n/total : name` (last path segment). Done screen: "Already up to date" or the five
  counts, plus failed and skipped lines; on error, a `tr()` title and a 2-line detail.
  Each `GitSyncError` has its own string, and the switches have no `default`, so a new
  value triggers `-Wswitch`.
- `preventAutoSleep()` is always true. `onExit` wipes the config and, when Wi-Fi is on,
  disconnects and restarts silently, as OPDS does.
- Logs for step 12: heap and largest block before the run, then the lowest free heap and
  the loop task's stack high-water mark after it.
- 35 `STR_GIT_SYNC_*` keys in `english.yaml` and `french.yaml`.
- Orientation review: all geometry comes from `getScreenSafeArea` plus theme metrics.
  The worst Done screen (10 lines) ends at y = 417 on Lyra landscape, under 480. A
  cosmetic point is left as is: in landscape the theme centres the progress percentage
  on the screen, not on the bar.
- Build net green (RAM 17.1%, flash 82.7%). No device test. No `guideline` decisions.

### Step 2.11

- `src/activities/home/HomeMenu.h`: the `HomeMenuItem` enum (with `GIT_SYNC`) and one
  `constexpr` row table with visibility gates. `itemCount`, `menuItemToIndex` and
  `indexToMenuItem` all derive from it. `HomeActivity` builds its count, cursor, labels,
  icons and actions from it, replacing the hand-written OPDS index arithmetic. Host-tested
  over every {OPDS, Update} combination (`test/home_menu/`, 7 cases).
- Order: Browse Files, Recent Books, [OPDS], [Update Library], File Transfer, Settings.
  `git_sync_config::exists()` is called once per Home `onEnter`. Label
  `STR_GIT_SYNC_MENU` ("Update Library" / "Mettre à jour les livres"). Icon: a new
  `UIIcon::Sync`, which Lyra draws with the Lucide download bitmap. Leaving the sync
  screen puts the cursor back on the entry.
- Index review: render, action, count and the cursor on entry agree for {OPDS yes/no} ×
  {Update yes/no} × {no recent books / some}, RoundedRaff's Continue Reading row
  included.
- `guideline` decision: with OPDS servers and a valid sync config on a button device,
  the menu has six rows and Settings runs into the button hints (Classic 10 px, label
  readable; Lyra 3 Covers 36 px, label partly hidden). Accepted as a known limitation for
  the owner's checklist; fixing it means changing the themes' shared row layout.
- Bug found while writing the step 3.12 PRD and fixed with `fix-bug` (its own `fix:`
  commit): the File Transfer web server checked only the last path segment, so
  `/download?path=/.crosspoint/github-sync.json` served the token to anyone on the
  network. Delete, rename, move, mkdir, both uploads and `/api/files` had the same gap.
  `isProtectedStoragePath` (`src/util/ProtectedPath`) now checks every segment for all
  eight entry points and for WebDAV. The new host test fails on the old rule. WebDAV
  PROPFIND may still list `/.crosspoint`: an unproven lead, left unchanged.
- Build net green.

### Step 3.12

- `USER_GUIDE.md` § 3.9 GitHub Library Sync (placed after 3.8, nine `####` parts:
  What You Need, Creating the Access Token, Writing the Configuration File, Preparing the
  First Sync, Running an Update, What Update Changes, What Update Never Touches, Limits
  and Skipped Files, Messages and What to Do), plus a sentence in 3.1 Home Screen and one
  in 7 Troubleshooting (deleting `.crosspoint` deletes the config and the sync record).
  The table of contents is updated and every anchor checked.
- The example JSON uses placeholders only. The token is a fine-grained PAT for one
  repository, Contents: Read-only, with an expiry; the guide says plainly that GitHub's
  certificate is not verified, and gives the mitigations.
- Every quoted on-screen string is verbatim from `english.yaml`; every behaviour is
  traced to a step note or the source. Points where the guide follows the code rather
  than the PRD's wording: a refused redirect on a book stops the run partway; leaving
  the sync screen restarts to Home whenever Wi-Fi was on; a name with `\` is ignored and
  logged rather than counted as skipped.
- End-of-phase-2 review (`warn-review`, then `plan-review`), two accepted method changes:
  the guide's File Transfer statement is checked against every WebDAV and web-server
  path handler, and the checklist gains a 50-file sync. The check found that WebDAV
  PROPFIND listed `/.crosspoint`; fixed with `fix-bug` in its own `fix:` commit
  (403 before the exists check, as GET does), so the guide can say the file cannot be
  listed. The other warnings (duplicated wipe and Wi-Fi helpers, header hygiene, the
  web server's untranslated HTTP messages, which match that file) were judged not to
  change the odds.
- Step 11 hypotheses H-11-1 to H-11-6 held. No `guideline` decisions in this step.

#### Owner's on-device checklist

Run it on a `default`-environment build (log level 2, serial on), with the serial log captured
through `python3 scripts/debugging_monitor.py`. File a failed item with the `fix-bug` skill.

**Setup and home menu**

- [ ] No config file: no Update Library entry, and the serial log has no `GSYNC` line on
  entering the Home screen.
- [ ] A broken config (for example a missing `token`): no entry; the log shows a `GSYNC`
  error naming the field.
- [ ] Copy a valid config over USB (or with the card in a computer), return to Home without
  restarting: the entry appears; Confirm (and a tap on touch devices) opens the sync screen.
- [ ] Lyra theme: the row shows the download icon.
- [ ] Known limitation (not fixed in step 11): with OPDS servers and sync configured, on a
  button device, Classic's sixth row runs about 10 px into the button hints (label
  readable) and Lyra 3 Covers' Settings row about 36 px (label partly hidden).
  *Record:* what is visible on each theme.
- [ ] Break the config while on the Home screen, then open Update Library: the "Sync
  settings missing or invalid" screen, no Wi-Fi start; Back returns to Home with the cursor
  on Update Library.

**First sync**

- [ ] Card prepared by copying the repository's files: the first sync adopts everything with
  zero downloads (Added 0, Updated 0, "Already on card" = number of files).
  *Record:* the file count, the library size in GB and the duration of the first sync.
- [ ] A dot folder in the repository (e.g. `.atelier/`) is not created on the card.
- [ ] Back during the first sync's adoption ("Checking"), then Update again: it resumes
  without re-hashing the files already adopted (the new total is the old total minus the
  files already done, and it finishes faster).

**Changes**

- [ ] Push an added book and a modified book: Added 1, Updated 1; the modified book
  reopens at the beginning, its bookmarks are kept.
- [ ] Delete the only book of a folder in the repository: Deleted 1, the folder is removed;
  a folder of your own that is empty is not.
- [ ] Move the book currently being read to another folder in the repository: Moved 1, no
  download; reopening it keeps its progress and bookmarks; the Recent Books list and the
  Home cover tile show it with its cover; after a restart, the reader reopens it at the same
  page.
- [ ] A book copied over USB outside the repository's paths is untouched after an Update.
- [ ] Two paths differing only by case, and a name containing `:`: "Skipped (name
  conflict): 2" on this run and the next, nothing deleted.
- [ ] An Update with no change: "Already up to date".

**Interruptions**

- [ ] Back mid-sync during downloads ("Stopping after the current file...", then "Sync
  stopped"), then Update again: it resumes and finishes; no partial book at any target path.
- [ ] Touch device: the home gesture mid-sync stops it and goes Home.
- [ ] Wi-Fi dropped mid-download (router off): "Network lost. Press Update again to
  resume."; with Wi-Fi back, the next run recovers and the summary counts only what was
  left.
- [ ] Reset button pressed mid-download: after restart, no half-written book; Update
  finishes the job.
- [ ] With Time to Sleep at 1 minute, a sync longer than a minute does not sleep.
- [ ] Leaving the Done screen after a Wi-Fi sync restarts to Home; reading state intact.

**Security**

- [ ] A full serial capture of a sync (debug build) contains no part of the token: search it
  for `github_pat_` and for the token's last 8 characters; zero hits.
- [ ] An expired or revoked token: "Cannot read the repository. Check token, name and
  Wi-Fi.", nothing changed on the card.
- [ ] Optional, needs a man-in-the-middle proxy you control (e.g. mitmproxy on a laptop
  hotspot, answering the trees request with a 302 to another host): "GitHub redirected to
  another site. Sync stopped.", nothing changed, and the proxy log shows no
  `Authorization` header sent to the other host.
- [ ] With File Transfer running (Join a Network), from a computer on the same network
  (for example with `curl -i`): `GET /download?path=/.crosspoint/github-sync.json`,
  `GET /api/files?path=/.crosspoint`, WebDAV `GET /.crosspoint/github-sync.json` and
  `curl -i -X PROPFIND -H "Depth: 1" http://<ip>/.crosspoint` all answer **403**, with no
  token text and no listing. `PROPFIND /` still answers 207 and does not list
  `.crosspoint`.

**Screens**

- [ ] The progress screen and the Done screen (a summary with Failed and Skipped lines, and
  one error) in all four orientations: nothing clipped or overlapping. *Record:* any
  orientation where the progress percentage looks off-centre (known cosmetic point of step
  2.10, landscape).

**Measurements** (from the `GSYNC` log lines of one sync that downloads at least one file)

- [ ] Free heap stays at or above 50 KB during the sync. *Record:* "Heap before sync: free
  …, largest block …" and "Lowest free heap during sync: …", and their difference. The
  difference holds the sync's peak (budget 64 KB, 61,744 B computed, of which the
  4 KB transient request/save line is an estimate) plus the TLS session, which the budget excludes; if it is
  above 64 KB, also watch the free-RAM graph of the debugging monitor during an OPDS download
  (same TLS transport) to size the TLS part.
- [ ] The transient 4 KB line: the lowest value is taken over the whole run, downloads
  included, so it is covered by the item above; *Record:* whether the lowest value occurred
  during the tree download, the planning or the downloads (watch the graph).
- [ ] A push that adds at least 50 small files, then Update: Added = N, no reboot and no
  "Not enough memory". *Record:* the "Lowest free heap during sync" line. The serial
  capture has no `abort()` and no `Guru Meditation`. Then remove the files from the
  repository and run Update again (a bulk delete): Deleted = N.
- [ ] Stack: after a run that includes a TLS fetch, "Loop task stack high-water mark: …
  bytes" is at least 1,024. *Record:* the value.
