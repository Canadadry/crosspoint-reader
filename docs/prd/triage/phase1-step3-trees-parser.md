---
title: "Trees response parser for GitHub library sync"
description: "The sync must turn GitHub's recursive trees response, arriving in network chunks of any size, into one fixed-shape record per file without holding the JSON or the path list in RAM, and must never mistake a cut-off, truncated or malformed response for the real repository."
status: needs-triage
---

## Problem Statement

The owner presses **Update** so the SD card mirrors their private books repository (plan 1, § 1 Goal). The device learns what the repository contains from one call, `GET /repos/{owner}/{repo}/git/trees/{branch}?recursive=1`, whose JSON body lists every path with its git blob SHA-1 and its size (plan § 2, "No real git on the device"; step 0.0 confirmed the shape: a top-level `sha`, then `tree[]` entries with `path`, `mode`, `type` = `tree` / `blob` / `commit`, `sha`, `size` for blobs only, and `url`, then `truncated`).

That body can be several hundred kilobytes for a large library, far beyond what the ESP32-C3 can buffer: the plan forbids holding the raw JSON or the full path list in RAM and caps peak sync heap at 64 KB (plan § 2, "Streaming, not buffering"). The body arrives in TLS chunks of arbitrary size, so a field can be split anywhere, even inside a UTF-8 character or an escape.

The repository's existing streaming parser, `StreamingJsonParser`, is lenient and lossy: it silently drops any string of 512 bytes or more (`lib/JsonParser/StreamingJsonParser.cpp:226-248`: `appendToken` stops at 511 bytes and `emitToken` then skips the callback), it does not decode `\uXXXX` escapes but passes them through as literal text (`:149-154`), and it does not report a body that stops halfway. Fed naively, a dropped `path` value would leave the previous entry's state in place, an escaped path would be written under a wrong name, and a cut-off body would look like a repository where most books were deleted, which the planner (step 5) would turn into deletions on the card.

Nothing in the codebase understands the trees response today.

## Solution

A tree response parser in the `GitSync` library, built on `StreamingJsonParser`. The sync glue feeds it the HTTP body chunk by chunk, as it arrives. For every `blob` entry it calls a plain function pointer with one record: the path (exact bytes, length and a terminator), the 20-byte blob SHA and the size. `tree` entries (directories) and `commit` entries (submodules) are skipped. When the body ends, the parser tells the caller:

- whether the response was **complete and well-formed** (top-level object closed, tree `sha` read, `tree` array read, `truncated` read, no error);
- whether GitHub flagged it **`truncated`**;
- the tree's own **SHA**;
- how many blobs it emitted, and how many it **skipped** because the path overflowed the JSON token buffer or contained an escape the JSON parser cannot decode. Each skip is logged with `LOG_ERR`.

The caller (step 9 glue, and step 6's end-to-end test) uses the records only if the response is complete and not truncated; otherwise the sync aborts and changes nothing (plan § 2, "Truncated tree"). The parser does not apply the path filter: it reports what the repository contains, and the caller filters each record with the step 2 filter before writing it to `tree.tmp`, so the planner receives "filtered remote entries" (plan step 5, Inputs).

## User Stories

1. As the library owner, I want the device to read my whole repository listing without running out of memory, so that Update works however many books I have, up to the documented cap.
2. As the library owner, I want the listing to be read as it downloads, so that the sync does not need a large buffer or a temporary copy of the raw JSON.
3. As the library owner, I want directories in the listing to be ignored as entries, so that only files are downloaded, adopted, moved or deleted.
4. As the library owner, I want git submodules (`commit` entries) in my repository to be ignored, so that a submodule pointer never becomes a file or a failed download on my card.
5. As the library owner, I want a repository GitHub reports as `truncated` to stop the sync before anything changes, so that a partial listing never deletes books that are still in the repository.
6. As the library owner, I want a listing that stops halfway (Wi-Fi drop, server error, cut-off body) to be refused, so that a partial listing never deletes books.
7. As the library owner, I want a listing that is malformed in a way the device cannot interpret safely (a file entry without a valid SHA or size, an unknown entry type) to stop the sync, so that a GitHub format change cannot make books disappear from the card.
8. As the library owner, I want a file whose path is too long for the device to be skipped with a log line instead of breaking the sync, so that the rest of my library still syncs.
9. As the library owner, I want the file listed after an over-long path to sync normally, so that one bad name never corrupts its neighbour's name, SHA or size.
10. As the library owner, I want a file whose name the device cannot decode exactly to be skipped and logged, so that it is never written under a wrong name.
11. As the library owner, I want empty files (size 0) in my repository to be listed, so that they are mirrored like any other file.
12. As the library owner, I want accented and non-Latin file names (`Livres/Été.epub`, Japanese titles) to come through byte for byte, so that adoption finds the copies I put on the card from my PC.
13. As the library owner, I want hidden repository folders such as `.atelier/` and `.github/` to be reported to the sync and then dropped by the path filter, so that one shared rule decides what is synced.
14. As the library owner, I want the sync to know the tree's own SHA, so that logs can name exactly which repository state was mirrored.
15. As a firmware developer, I want the parser to accept chunks of any size, down to one byte, so that I can pass each TLS read straight in.
16. As a firmware developer, I want the record delivered through a function pointer plus a context pointer, so that no `std::function` or heap closure is involved (CLAUDE.md, "Template and `std::function` Bloat").
17. As a firmware developer, I want the record's path to carry an explicit length and a terminator, so that I can pass it to the step 2 filter (pointer and length) and to C APIs without copying.
18. As a firmware developer, I want the record's SHA as 20 bytes, so that the step 4 record format and the step 1 hasher's output compare directly.
19. As a firmware developer, I want the record callback to be able to stop parsing (for instance when writing `tree.tmp` fails), so that I do not keep downloading a listing I cannot store.
20. As a firmware developer, I want the parser to allocate nothing and to have a fixed, documented size, so that I can budget it inside the 64 KB sync heap.
21. As a firmware developer, I want field order inside an entry not to matter, so that a GitHub change in key order does not break the sync.
22. As a firmware developer, I want unknown keys (`url`, `mode`, future additions) and unknown nested values to be ignored, so that new fields in the response do not break the sync.
23. As a firmware developer, I want an over-long key or value elsewhere in the body never to be attributed to the wrong field, so that the parser stays correct whatever the lossy JSON layer drops.
24. As a firmware developer, I want a `reset()` that makes the parser reusable, so that a retry does not need a new object.
25. As a firmware developer, I want skip counters I can read after parsing, so that the sync summary or a test can report how many files were not listed.
26. As a firmware developer, I want the shared `StreamingJsonParser` left unchanged, so that the OTA release parser that also uses it keeps its behaviour.
27. As a test author, I want a recorded fixture with the exact shape of the real response, fed byte by byte and in random chunk sizes, so that chunk boundaries inside keys, values, escapes and multi-byte characters are covered.
28. As a test author, I want every chunking of the same fixture to yield the same records and the same final state, so that the test does not depend on how TLS splits the body.
29. As a test author (step 6), I want to reuse this step's fixture and its known records for the end-to-end chain test, so that the chain starts from a parser output already pinned here.

## Implementation Decisions

- **Module: tree response parser**, a new module of the `GitSync` library (its own header/source pair next to the step 1 hasher, e.g. `GitTreeParser`). Same conventions as step 1 (plan § 6, step 1.1 note): no namespace, no heap, no Arduino include, `#pragma once`, `static constexpr` constants. It includes `StreamingJsonParser.h` from the `JsonParser` library and `GitBlobHasher.h` for `shaFromHex` / `SHA_SIZE` / `shaToHex`.

- **Public interface** (exact names are the implementer's; the shapes are the decision):
  - A record struct, valid only for the duration of the callback:
    - `const char* path` (terminated) and `size_t pathLen` (bytes, terminator excluded);
    - `uint8_t sha[20]`;
    - `uint64_t size` (the step 1 hasher's `begin` takes `uint64_t`).
  - The record has no `type` field: only `blob` entries are emitted, so the plan's `(path, type, sha, size)` is met by construction and a constant field would cost bytes in every step 4 record for nothing.
  - Callback: `bool (*)(void* ctx, const Record& record)`. Returning `false` stops the parser: it sets an `aborted` state and ignores every later byte.
  - Construction with `{ctx, callback}`; `reset()`; `feed(const char* data, size_t len)`.
  - Read after the body has been fed:
    - `isComplete()`: the top-level object closed, the tree `sha` was a valid 40-hex SHA, `tree` was an array and closed, `truncated` was a boolean, no error, not aborted.
    - `isTruncated()`: `truncated` was `true`.
    - `hasError()`: the response is malformed (see below) or `StreamingJsonParser` reported an error.
    - `wasAborted()`.
    - `treeSha()`: 20 bytes, meaningful only when complete.
    - Counters: blobs emitted, blobs skipped for an over-long path, blobs skipped for an undecodable path.
  - The caller's rule, documented on the interface: use the records only when `isComplete() && !isTruncated()`; anything else aborts the sync with no change (plan § 2, "Truncated tree"). The parser does not decide this itself because `truncated` arrives after the whole `tree` array, when every record has already been emitted.

- **Structure tracking.** Depth 1 is the top-level object, depth 2 the `tree` array, depth 3 an entry object. Top-level keys `sha`, `tree` and `truncated` are read only at depth 1; entry keys only at depth 3 inside `tree`. Any other key, and any object or array nested under an unknown key at any depth, is ignored by depth counting.

- **Key/value pairing (guard against the lossy JSON layer).** A value is attributed only to the key event immediately before it, and the pending key is cleared as soon as a value event (string, number, bool, null, object or array start) consumes it. A value that arrives with no pending key (its key was over 511 bytes and dropped) is ignored. This keeps a dropped key or value from ever shifting a value onto the wrong field.

- **Entry assembly.** Each entry's fields are collected into members during the entry object and acted on at its object end, so key order does not matter. The path is copied into a member buffer of `StreamingJsonParser::TOKEN_BUF_SIZE` bytes, so every path the token buffer can deliver (up to 511 bytes) is emitted unshortened; the length cap that the card needs is the step 2 filter's job (H-2-1). The SHA is converted with `shaFromHex` as it arrives. The size is parsed from the number token as an unsigned decimal into `uint64_t`; a sign, fraction, exponent or overflow makes it invalid.

- **Entry outcomes at object end**, in this order:
  1. `type` is `tree` or `commit` → skipped silently, nothing counted. Their other fields are not validated.
  2. `type` missing, not a string, or any other value → **response error**.
  3. `type` is `blob` and the `path` key was seen with no value before the next key or the object end → the path overflowed the token buffer: **skipped, counted, logged** with `LOG_ERR`, naming the entry's SHA in hex when it is valid. The entry is not emitted and the next entry starts from clean state. This is how the parser detects the overflow `StreamingJsonParser` does not report, with no change to that shared parser.
  4. `type` is `blob` and the decoded path contains a backslash byte → **skipped, counted, logged**. `StreamingJsonParser` decodes `\\` to `\` and passes `\uXXXX` through as the six literal characters (`StreamingJsonParser.cpp:149-157`); valid JSON cannot carry a raw backslash, so a backslash in the decoded path always means the emitted name would not be the repository's name. GitHub sends non-ASCII names as raw UTF-8, which is unaffected.
  5. `type` is `blob` and the path is missing or not a string, the SHA is missing or not 40 hex digits, or the size is missing or invalid → **response error**.
  6. Otherwise the record is emitted.
  - **Why errors, not skips, for case 2 and 5**: a skipped entry looks to the planner like a path removed from the repository, and the planner deletes manifest paths missing remotely (plan § 2, "Deletion scope"). An over-long or backslash path can never be in the manifest, because the filter or the executor rejects it first (H-2-1, H-2-3), so skipping it is safe; a blob with a bad SHA or size could be a book already on the card, so the whole response is refused instead.

- **Response errors and completeness.** Besides the entry errors above: a top-level `sha` that is not 40 hex digits, a `tree` value that is not an array, a non-object element inside `tree`, and a `truncated` value that is not a boolean are errors. A missing `sha`, `tree` or `truncated`, or a body that ends before the top-level object closes, leaves `isComplete()` false. After an error or an abort, `feed` ignores further bytes. Bytes after the top-level object closes are ignored.

- **Symlinks** (`mode` `120000`, `type` `blob`) are emitted like any blob: the parser does not read `mode`. FAT has no symlinks; the executor writes the blob's content (the link target text) as a small file. Interpreting `mode` is not needed by this step (guideline: take the smaller change).

- **Control bytes and dot paths are emitted as decoded.** `\n`, `\t`, … are decoded by `StreamingJsonParser` into control bytes, and `.atelier/…`, `.crosspoint/…` paths are emitted unchanged; the step 2 filter rejects or hides them (H-2-2).

- **Logging (decided here; nobody to ask).** The parser includes `Logging.h` and logs each skipped blob with `LOG_ERR`, because the plan says "skipped and logged" for this step and `LOG_ERR` is the codebase's way to log from `lib/` (e.g. `lib/Epub/Epub/css/CssParser.cpp:4`). It also exposes the skip counters, so tests assert behaviour through the public interface, not through log text. On the host, `Logging.h` pulls in `Arduino.h`, so the test suite supplies its own `stubs/Logging.h` whose macros compile to a no-op sink, as `test/css_parser/stubs/Logging.h` does. Log tag: a short constant for the module (e.g. `"GITTREE"`). The log line must not include the path itself (it was dropped or is mis-decoded); the SHA identifies the file. Rejected: counters only, with logging left to step 9, because the plan assigns the logging to this step and the glue would have no SHA to name.

- **Memory.** No heap allocation. The object holds `StreamingJsonParser` (about 0.55 KB, its 512-byte token buffer), the 512-byte path buffer, the entry's SHA, size and flags, and the tree SHA: about 1.1 KB in total, well inside the 64 KB sync budget. It is larger than CLAUDE.md's 256-byte stack guideline, so the device glue (step 9) holds it as a member or through `makeUniqueNoThrow`, never as a local; tests may keep it on the host stack. The only local buffer is the 41-byte hex SHA used in a log line.

- **No path filtering here.** Plan step 5's planner takes "the filtered remote entries", and the plan's streaming rule writes parser output to `tree.tmp`; the filter is applied by the caller between the callback and the tree record write (step 9, and step 6's end-to-end test). This keeps the parser independent of step 2, which is being implemented in parallel, and keeps "what the repository contains" separate from "what the sync may touch".

- **`StreamingJsonParser` is not modified.** An overflow callback would be cleaner but changes a shared library used by OTA (`ReleaseJsonParser`); the pending-key rule above detects the overflow from outside.

## Implementation Chunks

1. **Top-level shape.** The parser skeleton over `StreamingJsonParser`: depth tracking, the pending-key rule, tree `sha`, `tree` array, `truncated`, `isComplete` / `isTruncated` / `hasError`, `reset`. Tests: minimal complete response with an empty `tree`, `truncated: true`, missing fields, bad top-level SHA, body cut at every byte offset never complete.
2. **Blob records.** Entry assembly in any key order, the callback with stop, `tree` / `commit` skipping, size parsing, entry errors (missing or invalid SHA, size, path, type). Tests: the fixture fed whole, byte by byte and in random chunks.
3. **Skips.** Over-long path detection and backslash path skipping, with counters and `LOG_ERR`, plus the test stub `Logging.h`. Tests: the boundary cases and "the next entry is intact".

## Testing Decisions

- **What makes a good test**: only the public interface is used. A test feeds bytes and checks the sequence of records received (path bytes, SHA, size), the final flags (`isComplete`, `isTruncated`, `hasError`, `wasAborted`), the tree SHA and the counters. No test reads members or depends on how entries are buffered.
- **Module tested**: the tree response parser, in a new gtest suite (e.g. `test/git_sync_tree_parser/`) registered with `add_subdirectory` in the test CMake root. It compiles the parser's source, `lib/JsonParser/StreamingJsonParser.cpp` and `lib/GitSync/GitBlobHasher.cpp`, adds the `GitSync` and `JsonParser` include directories plus its own `stubs/` directory (first, for `Logging.h`), and links `crosspoint_test_common` and `GTest::gtest_main`, discovered with `gtest_discover_tests`.
- **Prior art**: `test/release_json_parser/` (a domain parser over `StreamingJsonParser`, with realistic minified and pretty-printed bodies, byte-by-byte and varied chunk feeding, truncated bodies, field order, nested unknown objects), `test/streaming_json_parser/` (`ChunkedFeeding`, `EveryByteBoundary`, `LargeTokenTruncation`), `test/css_parser/stubs/Logging.h` (host logging stub), and `test/git_sync_blob_hash/` (the step 1 suite layout).
- **The fixture**: a recorded-shape trees response kept in the test directory (a raw string literal or a data file read by the test), minified like the API returns, with the exact fields of step 0.0 (`sha`, `url`, `tree[]` with `path`, `mode`, `type`, `sha`, `size` on blobs, `url`; then `truncated`). Blob SHAs and sizes are real values produced with `git hash-object` on stated contents, so step 6's end-to-end chain can reuse the fixture with those contents. It contains at least:
  - a top-level blob and blobs nested two folders deep;
  - the `tree` entries for those folders;
  - a `commit` entry (mode `160000`, no `size`);
  - a size-0 blob (SHA `e69de29bb2d1d6434b8b29ae775ad8c2e48c5391`);
  - a UTF-8 path (`Livres/Été.epub`) and a path with a space;
  - a `.atelier/notes.md` blob (emitted: filtering is not this module's job);
  - a blob with a 600-byte path, immediately followed by a normal blob;
  - `"truncated": false`.
  A copy with `"truncated": true` is derived from it in the test.
- **Required tests**:
  - The fixture fed whole, byte by byte, and in random chunk sizes from 1 to 64 bytes over several fixed seeds (`std::mt19937`, seeds printed on failure): each run gives the same expected record list, `isComplete()`, not truncated, the expected tree SHA and the expected counters.
  - `tree` and `commit` entries never produce a record.
  - `truncated: true` is detected after a complete body, with the same records emitted.
  - Over-long path: a 600-byte path is skipped and counted; the next blob's path, SHA and size are exact. Boundaries: a 511-byte path is emitted whole; a 512-byte path is skipped. The over-long path as the last key of its entry (object end right after it) is also skipped.
  - A path containing the JSON escape `\u00e9` (passed through as six literal characters, so it decodes with a backslash) and a path containing `\\` are skipped and counted; a raw UTF-8 `Été` path is emitted; a path with `\"` or `\/` (decoded with no backslash left) is emitted as decoded.
  - Key order: an entry with `size` and `sha` before `path` and `type` gives the same record.
  - Unknown keys and an unknown nested object or array inside an entry and at the top level are ignored.
  - A key over 511 bytes followed by a string value does not change any field.
  - Errors: a blob with a missing SHA, a 39-character SHA, a non-hex SHA, a missing size, a negative or fractional size, a missing path, a numeric path, a missing type, an unknown type; a non-array `tree`; a non-object element in `tree`; a non-boolean `truncated`; a bad top-level SHA. Each gives `hasError()` and not complete.
  - Completeness: the fixture cut at every byte offset before the top-level object closes is never complete; a body missing `truncated`, `sha` or `tree` is not complete.
  - Size: `0` and a value above 4 GiB (e.g. `5000000000`) are parsed exactly; a value above `UINT64_MAX` is an error.
  - Callback stop: returning `false` on the second record gives exactly two callback calls, `wasAborted()`, not complete.
  - `reset()` after a complete parse and after an error gives the same result on a second feed of the fixture.
  - The pretty-printed form of the fixture gives the same result as the minified form.
- **Run with**: `cmake -S test -B build/test -G Ninja && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j`.
- **Not tested on host**: the device glue and the real HTTP stream (steps 9 and 12).

## Out of Scope

- Applying the path filter, writing `tree.tmp`, the tree record format (step 4), planning (step 5), executing and the end-to-end chain (step 6), and the HTTP request and its abort on `truncated` (step 9).
- Any change to `StreamingJsonParser`, `ReleaseJsonParser` or the `JsonParser` library.
- Decoding `\uXXXX` escapes.
- Interpreting `mode` (executable bit, symlinks).
- The meaning of an empty but complete `tree` (a repository with no files): the parser reports it faithfully; what the sync does with it follows the plan's mirroring rule in later steps.
- `src/`, `platformio.ini`, UI and i18n strings.

## Further Notes

### Evidence

- Plan: step 3 text and "Verified by" (`docs/plan-1.md`, § 3, Phase 1, step 3); § 2 "Streaming, not buffering", "Truncated tree", "Deletion scope", "Filter"; step 5 "Inputs: … the filtered remote entries"; step 6 end-to-end chain "from a recorded trees fixture through the step 3 parser … the fixture includes a size-0 file"; § 6 step 0.0 note (response shape, `.atelier/`); § 6 step 1.1 note (`GitBlobHasher`, `shaFromHex(const char*, size_t, out)`, `shaToHex`, no namespace, no logging).
- `lib/JsonParser/StreamingJsonParser.h:6-17` (callback struct of plain function pointers plus `ctx`), `:21` (`TOKEN_BUF_SIZE = 512`).
- `lib/JsonParser/StreamingJsonParser.cpp:226-232` (`appendToken` keeps at most 511 bytes, then sets `tokenOverflow`), `:234-248` (`emitToken` drops an overflowed key or value with no callback, still clearing `expectingValue`), `:125-174` (escape decoding; `\u` passed through as literal text), `:176-190` (number tokens, also dropped on overflow).
- `lib/JsonParser/ReleaseJsonParser.{h,cpp}`: the existing domain parser over `StreamingJsonParser` (static trampolines, depth tracking, `lastKey`), the pattern this module follows.
- `lib/GitSync/GitBlobHasher.h`: `SHA_SIZE`, `SHA_HEX_LENGTH`, `shaFromHex`, `shaToHex`.
- `lib/Logging/Logging.h:3-8` (includes `Arduino.h`, so host tests need a stub); `test/css_parser/stubs/Logging.h` and `test/css_parser/CMakeLists.txt` (stub directory placed first on the include path).

### Verification

- Host: the new suite passes under the `ctest` command above.
- Firmware: `pio run` must still succeed. Nothing in `src/` includes the parser yet; the PlatformIO library finder resolves `StreamingJsonParser.h` and `Logging.h` from `lib/` only if something includes the parser, so the firmware image is expected to be unchanged. Format with `./bin/clang-format-fix -g`.
- Device: nothing to check in this step. The owner's step 12 checklist exercises the real response.

### Hypotheses

Step 2 (path filter) is being implemented while this PRD is written. These assumptions about its outcome rest on its PRD (`docs/prd/triage/phase1-step2-path-filter.md`) and the committed tree only. This step does not include or call the step 2 module, so none of them blocks implementation; the implementer checks each against the committed step 2 code and adapts this PRD's notes if one fails.

- **H-2-1**: Step 2's filter rejects (as too long) every path over 255 bytes, a cap below 512 (its PRD: `MAX_SYNC_PATH_BYTES = 255`, asserted below `StreamingJsonParser::TOKEN_BUF_SIZE`). So no path this parser skips as over-long could ever have been synced into the manifest, and skipping it cannot cause a deletion. If step 2 chose a different cap, it is still at most 511 bytes, since nothing longer can reach it, and the same holds.
- **H-2-2**: Step 2's filter takes a pointer and an explicit length (`classifySyncPath(const char*, size_t)` / `isSyncablePath(const char*, size_t)`), and rejects control bytes and hides dot segments. This parser's record therefore carries `pathLen` and emits control-byte and dot paths unchanged for the caller to filter.
- **H-2-3**: Step 2's filter does not reject the backslash (its PRD leaves FAT-forbidden printable characters to the step 6 storage interface). This parser skips backslash paths itself because of the JSON escape issue, not as a FAT rule; if step 2 does reject `\`, the parser's rule is redundant but harmless and stays.
- **H-2-4**: Step 2's filter does not log; its PRD says later steps log malformed and too-long paths with `LOG_ERR`. That logging belongs to the caller that applies the filter (step 9 glue), not to this parser.
- **H-2-5**: Step 2 adds its own module and its own sibling test suite (e.g. `test/git_sync_path_filter/`) without a `library.json` or shared GitSync test target, and keeps `lib/GitSync/` free of `Logging`. This step then adds a third module and a third suite, and is the first `GitSync` module to include `Logging.h`; if step 2 instead created a shared GitSync suite, this step's test file and stubs go into that suite, with the stub directory first on its include path.
