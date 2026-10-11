---
title: "GitHub sync config loader"
description: "Read and validate /.crosspoint/github-sync.json into a fixed-size {owner, repo, branch, token} so the Update entry appears only for a usable config, every value is safe to put in a GitHub API URL, and the token never reaches a log."
status: needs-triage
---

## Problem Statement

The owner keeps their books in a private GitHub repository and wants to press **Update** on the X4 so the SD card mirrors it (plan 1, § 1 Goal). The device needs four facts to talk to GitHub: the repository owner, its name, the branch, and a fine-grained token with *Contents: Read* on that one repository (plan § 2, "Authentication").

The owner writes those facts from a PC into `/.crosspoint/github-sync.json`; there is no on-device editing and no keyboard entry of the token (plan § 2, "Configuration file"; § 2 "Out of scope"). Nothing on the device reads that file yet, so:

- the home menu cannot know whether to show **Update**: the plan wants it shown only when the config exists, and hidden when the file is missing or invalid (plan § 2, "Configuration file" and "UI");
- a file with a typo (an empty field, a stray space pasted with the token, a repository URL instead of a name, a missing quote) would only surface as an obscure HTTP failure deep in a sync, or worse, as a URL that points somewhere else (`owner = "a/b"`, `branch = "main?x"`);
- the token is the one secret of the feature: a careless `LOG_ERR("... %s", token)` would put it on the serial console and in any captured log.

## Solution

A **config loader** in two parts:

- A **pure parser and validator** in the `GitSync` library, host-tested with gtest, that turns the bytes of the file into a fixed-size `GitSyncConfig { owner, repo, branch, token }` or a precise verdict (`Malformed`, `MissingField(repo)`, `InvalidField(branch)`, `TooLarge`…). It accepts only values that can be pasted into a GitHub API URL path with no escaping, applies the `main` default for an absent branch, and never logs.
- A small **device loader** that reads the file through `HalStorage` into a bounded buffer, runs the parser, and logs a failure with `LOG_ERR` using constant text only (file path, field name, reason), never a value from the file. It exposes:
  - `load(config)`, for the sync activity (step 10) and glue (step 9);
  - `exists()`, for the home menu (step 11): true only when the file is present **and** valid, so a missing or invalid file hides **Update**, as the plan requires.

The owner's experience: they copy the JSON onto the card, and **Update** appears on the home screen. If they made a mistake, **Update** does not appear and the serial log says which field is wrong and why, without echoing the token.

## User Stories

1. As the owner, I want to write `/.crosspoint/github-sync.json` from my PC with `owner`, `repo`, `branch` and `token`, so that the device knows which repository to mirror.
2. As the owner, I want the **Update** entry to appear on the home screen once a valid config is on the card, so that I know the device picked it up.
3. As the owner, I want **Update** to stay hidden when the config file is absent, so that a reader who never set up GitHub sync sees no extra menu entry.
4. As the owner, I want **Update** to stay hidden when the config file is invalid, so that I never start a sync that is bound to fail with a cryptic network error.
5. As the owner, I want an invalid config to be reported on the serial log with the name of the faulty field and the reason, so that I can fix it without guessing.
6. As the owner, I want the token never to appear in any log line, whatever the error, so that a shared serial capture never leaks it.
7. As the owner, I want to omit `branch` and get `main`, so that the common case needs one field less.
8. As the owner, I want a branch name with slashes (`release/2026`) to be accepted, so that I can sync from any normal branch.
9. As the owner, I want an empty `owner`, `repo`, `branch` or `token` to be refused, so that a half-filled template is caught before any network access.
10. As the owner, I want a token pasted with a trailing space, newline or tab to be refused rather than silently sent, so that I learn about the paste error immediately.
11. As the owner, I want a classic token (`ghp_…`, 40 chars) as well as a fine-grained token (`github_pat_…`, about 93 chars) to be accepted, so that the loader does not lock me to one token format.
12. As the owner, I want a token up to 255 characters to be accepted, so that a future longer GitHub token format still works.
13. As the owner, I want `owner` containing `/`, a space, `?`, `#`, `%` or any non-ASCII byte to be refused, so that a pasted URL or `owner/repo` pair is caught.
14. As the owner, I want `repo` set to `.` or `..`, or containing `/`, to be refused, so that the API URL can never climb out of `/repos/{owner}/{repo}/`.
15. As the owner, I want a branch with `..`, `//`, a leading or trailing `/`, a segment starting with `.`, a segment ending in `.lock`, or a character git forbids, to be refused, so that only real branch names reach GitHub.
16. As the owner, I want a branch or repo name with a character that would need percent-encoding to be refused with a clear reason, so that the device never sends a URL that means something other than what I typed.
17. As the owner, I want a file saved by Windows Notepad (UTF-8 BOM, CRLF line endings, indentation) to load, so that my editor choice does not matter.
18. As the owner, I want unknown extra keys (a `"comment"` for instance) to be ignored, so that I can annotate the file.
19. As the owner, I want a file that is not a JSON object (empty, plain text, an array, cut off before its closing brace, or with junk after it) to be refused as malformed, so that a damaged copy is never half-used.
20. As the owner, I want a field given twice to be refused, so that the device never picks one of two conflicting values behind my back.
21. As the owner, I want a field given as a number, `true` or `null` instead of a string to be refused, so that type mistakes are caught.
22. As the owner, I want an oversized file to be refused without being read into memory whole, so that a wrong file copied to that name cannot exhaust RAM.
23. As the owner, I want a config I fixed over USB drive mode to be picked up the next time the home screen opens, without a reboot, so that fixing a typo is quick.
24. As the owner, I want the home screen to stay as fast as today when no config exists, so that a feature I do not use costs nothing noticeable.
25. As a developer of the sync glue (step 9), I want `owner`, `repo` and `branch` guaranteed URL-safe, so that I build API URLs by plain concatenation with no escaping code.
26. As a developer of the sync activity (step 10), I want one `load()` call that returns either a valid config or a verdict, so that I can show an error screen or start the sync.
27. As a developer of the home menu (step 11), I want a single boolean `exists()`, so that the menu entry logic mirrors the OPDS entry's `hasOpdsServers` flag.
28. As a developer, I want the config held in fixed `char` buffers with no heap inside, so that it follows the CLAUDE.md string policy and can live as a member or behind `makeUniqueNoThrow`.
29. As a developer, I want the parser host-tested over valid, missing-field and malformed inputs, so that validation rules cannot regress silently.
30. As a developer, I want a failed parse to leave the output config zeroed, so that no caller can use a half-filled config or keep a token from a rejected file.
31. As a maintainer, I want the log text for every verdict to be a constant string chosen from the verdict alone, so that "the token is never logged" holds by construction and is easy to review.
32. As a maintainer, I want the loader to reuse the already host-tested `StreamingJsonParser` rather than add ArduinoJson to the host test build, so that no new test dependency is introduced.

## Implementation Decisions

### Modules

1. **Config parser** (new, in the `GitSync` library, host-testable): the `GitSyncConfig` type, the verdict types, `parseGitSyncConfig` and `describeGitSyncConfigResult`. Follows the conventions of steps 1 to 5: `#pragma once`, no namespace, `static constexpr` limits, no Arduino include, no `std::string`, no `std::function`, and **no logging** (the parser returns verdicts; the device loader logs).
2. **Device loader** (new, in `src/`, next to the future sync glue of step 9 in the network area): the file path constant, `load(config)` and `exists()`. Depends on `HalStorage`, `Logging.h`, `Memory.h` and the parser.

Nothing existing is modified, except registering the new test suite.

### Config type

```cpp
struct GitSyncConfig {
  static constexpr size_t OWNER_MAX = 39;    // GitHub login / organisation name limit
  static constexpr size_t REPO_MAX = 100;    // GitHub repository name limit
  static constexpr size_t BRANCH_MAX = 255;  // bytes; one FAT long-name worth, far above real branch names
  static constexpr size_t TOKEN_MAX = 255;   // GitHub's documented upper bound for token length
  char owner[OWNER_MAX + 1];
  char repo[REPO_MAX + 1];
  char branch[BRANCH_MAX + 1];
  char token[TOKEN_MAX + 1];
};
```

- Each field is `'\0'`-terminated. 653 bytes in all, so it is **never a stack local** (CLAUDE.md, Resource Protocol 1: locals < 256 bytes): callers hold it as a member (step 10's activity) or through `makeUniqueNoThrow`. A `static_assert(sizeof(GitSyncConfig) <= 656)` pins the size.
- Fixed `char` buffers rather than `std::string`: CLAUDE.md String Policy; the size is bounded by the limits below, so a heap string buys nothing. Step 9 converts the token to whatever `HttpDownloader` takes at the call site (H-6-2).
- Token length: fine-grained PATs are `github_pat_` plus 82 characters (93), classic ones `ghp_` plus 36 (40); GitHub documents that token length may grow up to 255, so 255 leaves margin without a second limit to revisit.

### Verdicts

```cpp
enum class GitSyncConfigStatus : uint8_t {
  Ok, NotFound, ReadError, TooLarge, Malformed, MissingField, InvalidField, OutOfMemory
};
enum class GitSyncConfigField : uint8_t { None, Owner, Repo, Branch, Token };
struct GitSyncConfigResult { GitSyncConfigStatus status; GitSyncConfigField field; };
```

- `NotFound` and `ReadError` are produced only by the device loader; the parser produces the others.
- `field` is set for `MissingField` and `InvalidField`, `None` otherwise.
- `describeGitSyncConfigResult(result)` returns a constant, static string per (status, field) pair, e.g. `"token: empty, too long or forbidden character"`. It never formats a value. Both the parser and the strings are in flash (`constexpr` table or `switch`).

### Parser contract

`GitSyncConfigResult parseGitSyncConfig(const char* json, size_t len, GitSyncConfig& out)`:

- Reads exactly `len` bytes; `json` need not be `'\0'`-terminated.
- `len > MAX_CONFIG_FILE_BYTES` (`static constexpr size_t MAX_CONFIG_FILE_BYTES = 1024`, public) → `TooLarge`, before any parsing. The largest valid file (39 + 100 + 255 + 255 value bytes, four keys, quotes, separators, indentation) is about 750 bytes, so 1 KiB fits every valid config, pretty-printed with CRLF.
- An optional UTF-8 BOM (`EF BB BF`) at the start is skipped. JSON whitespace (space, tab, CR, LF) is allowed before the root `{` and after the root `}`; any other byte outside the root object → `Malformed`.
- The root must be one object. A nested object or array anywhere → `Malformed` (the format is flat; the plan fixes its shape). An unclosed root (truncated file) → `Malformed`. Empty input or whitespace only → `Malformed`. `StreamingJsonParser::hasError()` → `Malformed`.
- Keys are matched exactly and case-sensitively: `owner`, `repo`, `branch`, `token`. Any other key is ignored whatever its scalar value.
- A known key whose value is not a string (number, `true`, `false`, `null`), or whose string value never arrived (a string of 512 bytes or more is dropped by `StreamingJsonParser`, `lib/JsonParser/StreamingJsonParser.cpp:226-240`), → `InvalidField(that key)`.
- A known key given twice → `InvalidField(that key)`.
- After the root closes: `owner`, `repo` or `token` absent → `MissingField(that key)`, checked in the order owner, repo, branch, token so the verdict is deterministic. `branch` absent → `"main"`. `branch` present but `""` → `InvalidField(Branch)`: the plan wants non-empty fields, and an explicit empty value is more likely a mistake than a request for the default.
- Escapes: `StreamingJsonParser` decodes `\"`, `\\`, `\/`, `\b`, `\f`, `\n`, `\r`, `\t` and passes `\uXXXX` through as a literal backslash sequence. Every field's character set below excludes `\` and control bytes, so an escaped control character or any `\u` escape is `InvalidField`. No Unicode decoding is needed.
- On any non-`Ok` verdict, `out` is fully zeroed (`memset`) before returning, so no caller can use a partial config or keep a token from a rejected file. On `Ok`, all four fields are filled and terminated.
- Working state: the `StreamingJsonParser` (about 560 bytes, 512 of them its token buffer) plus a few flags. It exceeds the 256-byte local rule, so the function allocates it once with `makeUniqueNoThrow` and returns `OutOfMemory` when that fails. One allocation per call, released before return; the call happens once per home-screen entry or sync start, never in a loop (CLAUDE.md, Resource Protocol 2). A static instance was rejected: it would cost 560 bytes of DRAM forever for a parse that runs a few times per session.
- Why `StreamingJsonParser` and not ArduinoJson: (1) no host test links ArduinoJson today (it comes from PlatformIO's `lib_deps`, `platformio.ini:98`), while `StreamingJsonParser` is already host-tested and already a `GitSync` dependency (step 1.3); (2) `PersistableStoreBase::readDocFromFile` goes through an Arduino `String` and a heap `JsonDocument`, and the stores built on it (`OpdsServerStore`, `KOReaderCredentialStore`) keep their values in `std::string` for the whole session; (3) this file is read-only on the device, so `PersistableStore`'s save, mutex and resave machinery is unused. `StreamingJsonParser` is lenient about `:` and `,` placement; the strict checks above (root shape, nesting, value types, duplicates, required fields) are what the tests pin.

### Field validation

All checks are byte-wise on ASCII; any byte ≥ 0x80 is forbidden in every field (it would need percent-encoding in a URL, or is not a valid name).

- **owner**: 1 to 39 bytes, each `[A-Za-z0-9-]` (GitHub login and organisation alphabet). Hyphen placement is not checked: it does not affect URL safety, and GitHub rejects a bad name with a 404 anyway.
- **repo**: 1 to 100 bytes, each `[A-Za-z0-9._-]` (GitHub's repository alphabet); `.` and `..` are refused (URL dot-segments). A leading dot is allowed (`.github`-style names exist); `...` is a plain segment.
- **branch**: 1 to 255 bytes, each `[A-Za-z0-9._/-]`, plus git's ref-name rules that this alphabet still allows: no leading or trailing `/`, no `//`, no `..`, no segment starting with `.`, no segment ending with `.lock`, no trailing `.`. This is a subset of what `git check-ref-format --branch` accepts, chosen so the value needs no escaping in `/git/trees/{branch}` (H-6-3). Branch names with other characters (`+`, `@`, non-ASCII) are refused; the owner renames the branch or uses another one. The `?`, `#`, `%`, space and `\` that the task singles out are all outside the alphabet.
- **token**: 1 to 255 bytes, each a visible ASCII byte 0x21 to 0x7E. No prefix check (classic, fine-grained and future formats all pass); whitespace and control bytes are refused, which catches paste errors. `:` is allowed: the token is the Basic-auth password, where `:` is legal, and the username is chosen by step 9 (H-6-2).

The validators are internal to the parser; tests reach them through `parseGitSyncConfig`.

### Device loader

- Path: `/.crosspoint/github-sync.json` (plan § 2), a `constexpr` constant of the loader.
- `GitSyncConfigResult load(GitSyncConfig& out)`:
  1. `Storage.exists(path)` false → `NotFound`, `out` zeroed, **no log** (the normal state for a reader who never set up the sync; mirrors `readDocFromFile`'s silent first boot, `lib/Serialization/PersistableStore.cpp:24-26`).
  2. Allocate a read buffer of `MAX_CONFIG_FILE_BYTES + 2` bytes with `makeUniqueNoThrow<char[]>`; failure → `OutOfMemory`, logged.
  3. `Storage.readFileToBuffer(path, buf, MAX_CONFIG_FILE_BYTES + 2)`: it reads at most `bufferSize - 1` bytes and always terminates (`freeink-sdk/libs/hardware/SDCardManager/src/SDCardManager.cpp:258-291`), so a result of `MAX_CONFIG_FILE_BYTES + 1` bytes means the file is too large → `TooLarge`, without reading the rest. A result of 0 → `ReadError` (the call cannot tell an empty file from an open failure; the log says "empty or unreadable").
  4. `parseGitSyncConfig(buf, n, out)`.
  5. The read buffer is wiped (`memset`) before release, since it holds the token.
  6. Any verdict other than `Ok` and `NotFound` → `LOG_ERR("GSYNC", "%s: %s", path, describeGitSyncConfigResult(result))`. On `Ok`, `LOG_DBG` may print owner, repo and branch; it never prints the token or its length.
- The only format arguments the loader ever passes to a log macro are the path constant, the describe string, and on success owner, repo and branch. That is the whole of "the token is never logged", reviewable in one function.
- `bool exists()`: allocates a transient `GitSyncConfig` with `makeUniqueNoThrow` (`OutOfMemory` → false, logged), calls `load`, wipes the transient config, returns `status == Ok`. It means **present and valid**, because the plan hides **Update** for a missing *or* invalid file (§ 2, "Configuration file").
- **No cached validity.** `exists()` re-reads the file on every call. Reasons: the owner can rewrite the file over USB drive mode (`src/activities/network/UsbDriveActivity.cpp:20`) and return to the home screen without rebooting, and a cache would show a stale entry; the cost is small: when the file is missing (the common case), one `Storage.exists` lookup in `/.crosspoint/`, the same kind of call the home screen already makes per recent-book cover (`src/activities/home/HomeActivity.cpp:65`); when present, one read of at most 1 KiB and a parse of the same size, with about 2.3 KB of transient heap (1,026-byte buffer, about 560-byte parser state, 653-byte config), all released before `exists()` returns. Step 11 calls it once per home-screen `onEnter`, where `hasOpdsServers` is computed today (`HomeActivity.cpp:116`), never per render (H-6-4). An invalid file therefore logs one `LOG_ERR` per home-screen entry, which is acceptable for an error state the owner is expected to fix.
- No `PersistableStore` singleton: it would keep the token and the other fields in DRAM for the whole session for a feature used occasionally, and the file is never written by the device.
- Memory hygiene of the token is best effort, not a security boundary: the loader wipes its own buffers, but `StreamingJsonParser`'s private token buffer is released unwiped. The token is read-only on one repository (plan § 2, "Authentication").

## Implementation Chunks

1. **Parser and validators** (host-tested): `GitSyncConfig`, the verdict types, `describeGitSyncConfigResult`, `parseGitSyncConfig`, the new gtest suite.
2. **Device loader**: path constant, `load`, `exists`, logging; verified by `pio run` and a review of the log calls. The loader has no caller until steps 10 and 11, so the linker may drop it; `pio run` must still compile it (it is in `src/`, which is always compiled).

## Testing Decisions

- **What makes a good test**: input bytes in, verdict and config out, through `parseGitSyncConfig` only. No test reaches the internal validators or the parser's state.
- **Suite**: a new `test/git_sync_config/`, registered with `add_subdirectory` in `test/CMakeLists.txt`, laid out like `test/git_sync_tree_parser/`: it compiles the parser source and `lib/JsonParser/StreamingJsonParser.cpp`, includes `lib/GitSync`, `lib/JsonParser` and `lib/Memory`, links `crosspoint_test_common` and `GTest::gtest_main`. The parser does not log, so no logging stub is needed.
- **Valid inputs** (`Ok`, every field compared):
  - the plan's example shape with a fine-grained 93-character token;
  - `branch` absent → `"main"`;
  - `branch` `"release/2026"`, and `"a/b.c/d-e_f"`;
  - a classic 40-character `ghp_` token;
  - each field at its maximum length (39, 100, 255, 255);
  - a UTF-8 BOM, CRLF line endings, tabs and indentation;
  - keys in a different order; an unknown key with a string, a number and `null`;
  - `\/` in a branch decodes to `/`;
  - the same input fed with `len` shorter than the buffer's real length (trailing bytes after `len` are ignored, the input is not `'\0'`-terminated).
- **Missing fields**: each of `owner`, `repo`, `token` absent → `MissingField` with that field; all absent → `MissingField(Owner)` (deterministic order); an empty object `{}` → `MissingField(Owner)`.
- **Invalid fields** (`InvalidField` with the right field):
  - each field `""`, including `branch`;
  - each field one byte over its maximum;
  - owner: `"a/b"`, `"a b"`, `"a.b"`, `"a?"`, `"é"`;
  - repo: `"."`, `".."`, `"a/b"`, `"a#b"`, `"a%20b"`, `"a b"`;
  - branch: `"/main"`, `"main/"`, `"a//b"`, `"a..b"`, `"a/.b"`, `"a.lock"`, `"a/b.lock/c"`, `"main."`, `"a b"`, `"a?b"`, `"a#b"`, `"Été"`, `"a\\b"`, `"aA"`;
  - token: with a trailing space, an escaped `\n`, an escaped `\t`, a byte ≥ 0x80;
  - a known key with a number, `true`, `false` and `null` value;
  - a known key given twice (same and different values);
  - a known key whose value is a 600-byte string (dropped by `StreamingJsonParser`).
- **Malformed**: empty input, whitespace only, `"hello"`, `[]`, `[{"owner":"a"}]`, a valid object cut at every byte offset before its closing `}` (each prefix is `Malformed`, `MissingField` never wins over the missing brace), a valid object followed by `x`, two root objects, a nested object value, an array value.
- **Too large**: 1,025 bytes of an otherwise valid file (padded with whitespace) → `TooLarge`; exactly 1,024 → `Ok`.
- **Zeroed output**: after every non-`Ok` case above, `out` is all zero bytes, starting from a config pre-filled with a sentinel.
- **Describe strings**: every (status, field) pair the parser can return yields a non-null, non-empty constant; for a failing input whose token is a known sentinel, the describe string does not contain it.
- **Prior art**: `test/git_sync_tree_parser/` (a `StreamingJsonParser` client fed raw JSON bytes, including an over-long string), `test/release_json_parser/` (a small `StreamingJsonParser` client), `test/git_sync_path_filter/` (table of accepted and refused names).
- **Run with**: `cmake -S test -B build/test -G Ninja && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j`; then `pio run`; then `./bin/clang-format-fix -g`.
- **Not tested on the host**: the device loader (it needs `HalStorage`). It is verified by `pio run` and by review: `NotFound` is silent, every other failure logs through `describeGitSyncConfigResult`, no log call takes the token, buffers are wiped.
- **For the owner (step 12 checklist material)**: with no file, no **Update** and no log line; with a broken file, no **Update** and one `GSYNC` error line naming the field; fixing the file over USB drive mode makes **Update** appear on the next home screen.

## Out of Scope

- The **Update** home menu entry, its icon and index mapping (step 11); the error screen shown when `load` fails at sync time (step 10).
- Building API URLs, the Basic-auth username, the `Accept` header (steps 8 and 9).
- Writing or editing the config on the device, a web-server or keyboard editor (plan § 2, "Out of scope").
- Obfuscating the token at rest (the file is written by the owner from a PC in plain JSON; `/.crosspoint/` is hidden from the file browser and the web server, plan § 2).
- Checking the token against GitHub, or checking that the repository or branch exists (the first API call does that).
- Branch names with characters outside `[A-Za-z0-9._/-]`, and percent-encoding of any field.
- `\uXXXX` decoding.
- Any change to `StreamingJsonParser`, `PersistableStore` or `HalStorage`.
- User-facing strings: the loader shows nothing on screen; its messages are logs, which CLAUDE.md allows in plain text.

## Further Notes

### Evidence

- Plan step 7 text and "Verified by" (`docs/plan-1.md`, § 3, Phase 2, step 7); § 2 "Authentication", "Configuration file", "UI" (Update shown only when the config exists), "Out of scope"; steps 9 to 11 for the consumers.
- `lib/Serialization/PersistableStore.h` and `.cpp` (ArduinoJson via `String` and `JsonDocument`, silent missing file at `.cpp:24-26`), `src/OpdsServerStore.cpp` and `lib/KOReaderSync/KOReaderCredentialStore.h` (values kept in `std::string`, loaded at boot in `src/main.cpp:423-427`).
- `lib/JsonParser/StreamingJsonParser.h` (`TOKEN_BUF_SIZE = 512`, callbacks struct), `.cpp` (escape handling, `\u` passed through, over-long strings dropped at `:226-240`, lenient `:` / `,`).
- `lib/hal/HalStorage.h:35-50` (`readFile` returns `String`, `readFileToBuffer`, `exists`); `freeink-sdk/libs/hardware/SDCardManager/src/SDCardManager.cpp:258-291` (`readFileToBuffer` reads at most `bufferSize - 1` bytes and terminates).
- `test/CMakeLists.txt` and every `test/*/CMakeLists.txt` at HEAD: no suite links ArduinoJson; `test/git_sync_tree_parser/` and `test/release_json_parser/` link `StreamingJsonParser.cpp`.
- `lib/Memory/Memory.h`: standard headers only, usable on the host.
- `src/activities/home/HomeActivity.cpp:113-125` (`hasOpdsServers` computed in `onEnter`), `:309-317` (the OPDS entry the Update entry copies); `src/activities/network/UsbDriveActivity.cpp:20` (USB drive mode, the reason for no cache).
- CLAUDE.md: Resource Protocol 1, 2, 4, 9; String Policy; Error Handling Philosophy; Heap Buffer Allocation.

### Hypotheses about other steps

- **H-6-1** (step 9): the sync glue builds `https://api.github.com/repos/{owner}/{repo}/git/trees/{branch}?recursive=1` and the blob URLs by plain concatenation of this step's fields, relying on the alphabets above for safety; it adds no escaping.
- **H-6-2** (steps 8 and 9): the token is sent as the Basic-auth password through `HttpDownloader`, whose credential parameters are `std::string` (`src/network/HttpDownloader.h:30-46`); the glue builds that string at the call site from `config.token`, with a username of its choice (GitHub ignores it for token auth, step 0.0 used `x`). Nothing in this step depends on which username is chosen.
- **H-6-3** (step 9): GitHub's `git/trees/{branch}` resolves a branch name containing `/`. Step 0.0 only verified `main`. If it does not, step 9 resolves the branch to a commit SHA first; this step's validation is unchanged either way.
- **H-6-4** (step 11): the home menu calls `exists()` once per `HomeActivity::onEnter`, stores the boolean like `hasOpdsServers`, and uses it in `menuItemToIndex` / `indexToMenuItem` and `render`.
- **H-6-5** (step 10): the sync activity calls `load()` at sync start into a member or `makeUniqueNoThrow` config, so a file changed or removed since the home screen is re-validated before any network access.
- The step 6 executor (being implemented now) does not touch the config; this step depends on nothing from it.

### Decisions taken without a human (guideline rules)

- `exists()` means present **and** valid, re-validated on each call with no cache: the plan hides the entry for a missing or invalid file, and USB drive mode can change the file without a reboot; the cost is one directory lookup when the file is absent.
- `StreamingJsonParser` instead of ArduinoJson: host-testable with no new test dependency, no heap `String` or `JsonDocument`, already a `GitSync` dependency.
- Fixed `char` buffers sized by GitHub's limits (39, 100) and 255 for branch and token; struct never on the stack.
- Alphabets restricted to characters that need no URL escaping; `branch` alone may contain `/`, with git's ref-name rules on top; token limited to visible ASCII, no prefix check.
- Absent `branch` → `main`; explicit empty `branch` → invalid.
- Unknown keys ignored; duplicate known keys and nested containers refused.
- The parser never logs; the device loader logs constant strings only, which makes "the token is never logged" hold by construction.
- File capped at 1,024 bytes, detected by reading one byte past the cap.
