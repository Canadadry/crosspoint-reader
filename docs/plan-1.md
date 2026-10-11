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
  (`src/network/HttpDownloader.cpp:161-166`), and GitHub accepts the token as the password
  of Basic auth.
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
  there: `HttpDownloader` follows redirects with the same headers
  (`src/network/HttpDownloader.cpp:178-189`).
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

**Verification commands for this repository.** These replace the `make test` and
`make e2e` that the skills copied from slipways-love refer to:

- **Host unit tests (gtest)**: `cmake -S test -B build/test -G Ninja && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j`.
  A new suite is registered with `add_subdirectory` in `test/CMakeLists.txt`; see
  `test/streaming_json_parser/` for the layout.
- **Firmware build**: `pio run` (the `default` environment).
- **Formatting**: `./bin/clang-format-fix -g`.
- **On-device checks**: hardware tests are not available to the agent; they are listed
  for the owner in step 11.

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
   *Verified by*: gtest with a recorded fixture fed byte by byte and in random chunk sizes,
   checking that `tree` and `commit` entries are skipped and that `truncated: true` is
   detected.
4. **Manifest format.**
   - **Records**: a versioned record file of `(sha[20], size, path)`, with read and write
     over a small stream interface so that tests use memory and the device uses `HalFile`.
   - **Layout**: aligned reads only (`memcpy`), per the RISC-V rule in `CLAUDE.md`.
   - **Corruption**: a corrupt or wrong-version file is reported, never half-applied.

   *Verified by*: gtest covering the round trip, truncation at every byte offset, and a
   version mismatch.
5. **Sync planner.**
   - **Inputs**: the manifest entries, the filtered remote entries, and a callback
     `localBlobSha(path) → optional sha`, used only for adoption.
   - **Output**: an ordered operation list:
     1. moves (rename plus cache migration);
     2. adopts (manifest only);
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
   - adoption with a matching local file, and a local file with a different SHA, which is
     downloaded;
   - a remote `.crosspoint/…` entry, which is ignored;
   - a cap overflow, which is refused.

### Phase 2 — Device integration

6. **Config loader.** `/.crosspoint/github-sync.json` is parsed into
   `{owner, repo, branch, token}`, with validation (non-empty fields, branch defaulting to
   `main`). It exposes `exists()` for the home menu. The token is never logged.
   *Verified by*: gtest on the parsing function with valid, missing-field and malformed
   inputs, plus `pio run`.
7. **HTTP additions.** `HttpDownloader` gains:
   - an optional extra request header for the `Accept` value;
   - a "no cross-host redirect when credentials are set" guard.

   Existing callers are unchanged.
   *Verified by*: `pio run`, and review that OPDS, OTA and KOReader call sites compile
   unchanged.
8. **Sync engine** (`src/network/GitSync*.cpp` or `src/util/`). It:
   - deletes leftover temporary files;
   - streams the tree to `/.crosspoint/gitsync/tree.tmp`;
   - aborts on `truncated`;
   - plans with the hash callback reading the local file through `HalStorage` in 4 KB
     chunks;
   - executes operations one by one, writing each download to a temp file that is renamed
     onto the target, creating parent directories with `Storage.mkdir`;
   - migrates the reading state on moves (cache dir, bookmark file, recent books,
     `APP_STATE.openEpubPath`), following `EpubReaderActivity.cpp:127-146`;
   - persists the manifest after each operation;
   - honours a cancel flag between operations;
   - reports progress through a function-pointer-plus-context callback.

   *Verified by*: `pio run` and a code review against the rules in section 2.
9. **`GitSyncActivity`.** It handles the Wi-Fi connect flow, the progress screen, Back as
   cancel, the summary or error screen, and `preventAutoSleep() == true`. Its tr() strings
   go in `english.yaml` and `french.yaml`, and the work runs in a task with a 4096-byte
   stack (network work, per `CLAUDE.md`) deleted in `onExit()`.
   *Verified by*: `pio run`, `./bin/clang-format-fix -g`, and a review of orientation
   handling in all four modes.
10. **Home menu entry.** **Update** is shown only when the config exists, with its own icon
    and index mapping updated in `menuItemToIndex` / `indexToMenuItem`.
    *Verified by*: `pio run`, and a review that the menu index math still holds with and
    without OPDS servers and recent books.

### Phase 3 — Documentation and hand-off

11. **User guide.** Add a section to `USER_GUIDE.md` covering:
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
    - Wi-Fi drops mid-download and the next run recovers;
    - free heap stays at or above 50 KB during a sync (`LOG_DBG` `ESP.getFreeHeap()`).

    *Verified by*: the doc renders and every step matches the implemented behaviour.

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
- **The copied skills assume another build.** The skills copied from slipways-love
  (`git-commit`, `fix-bug`, `tdd`, `check-e2e`, `prd-autopilot`) run `make test` and
  `make e2e`, which do not exist here. Until they are adapted, use the verification
  commands listed at the top of section 3. There is no e2e replay suite in this
  repository, so `check-e2e` steps do not apply.

## 6. Step notes

(empty — plan-pilot appends a note after each step)
