# Good and Bad Tests

Tests are host gtest suites under `test/<module>/`, one `TEST(Suite, Behavior)` per behavior. Real APIs below where they exist; other names are illustrative.

## Good Tests

**Integration-style**: test through the module's public header, not its internals.

```cpp
// GOOD: observable behavior, through the public interface (real API, test/opds_filename/)
TEST(OpdsFilename, EmptyAuthorCollapsesToTitleForEveryFormat) {
  EXPECT_EQ(opdsBookFilename("", "My Book", OpdsFilenameFormat::AuthorTitle), "My Book.epub");
  EXPECT_EQ(opdsBookFilename("", "My Book", OpdsFilenameFormat::TitleOnly), "My Book.epub");
}
```

Characteristics:

- Tests behavior callers care about
- Includes only the public header
- Survives internal refactors
- Name describes WHAT, not HOW
- One logical assertion per test

For a parser or a stream, feed the input the way the device does: in chunks of arbitrary size, byte by byte included, never as one convenient buffer only. For anything that touches storage, run it against the in-memory test double ([mocking.md](mocking.md)) and assert on the resulting files, never on the calls made.

## Bad Tests

**Implementation-detail tests**: coupled to internal structure.

```cpp
// BAD: reaches into private state through a test-only friend or #define private public
TEST(Planner, UsesShaIndex) {
  SyncPlanner planner;
  planner.buildIndex(entries);
  EXPECT_EQ(planner.shaIndex.size(), 3u);
}
```

Red flags:

- Including a `.cpp` file, `#define private public`, or a test-only header exposing internals
- Asserting on call counts or call order
- Test breaks when refactoring without behavior change
- Test name describes HOW, not WHAT
- Verifying through a side channel instead of the interface
- Linking device-only code (HAL, Arduino, ESP-IDF) into a host test instead of passing a narrow interface

## Checked Contracts and Tests: Complementary Layers

Tests verify behavior from the outside. The code checks its contracts from the inside, and on this firmware a violated contract is refused, not aborted (`CLAUDE.md` § Error Handling Philosophy):

```cpp
// Without a check: a bad record length reads past the buffer, fails far from the cause
bool readRecord(Stream& in, Record& out) {
  in.read(out.path, len);
  ...
}

// With a check: refused at the violated contract, and the serial log names it
bool readRecord(Stream& in, Record& out) {
  if (len >= sizeof(out.path)) {
    LOG_ERR("SYNC", "Record path too long: %u", len);
    return false;
  }
  ...
}
```

Rule of thumb: if a caller must respect a constraint to call a function, the function checks it, and a test feeds it the bad input and expects the refusal.

```cpp
// BAD: verifies by poking the manifest's storage
TEST(Manifest, AddStoresEntry) {
  manifest.add(entry);
  EXPECT_EQ(manifest.entries_[0].path, "a.epub");
}

// GOOD: verifies through the interface, after a round trip
TEST(Manifest, SavedEntryIsFoundAfterReload) {
  manifest.add(entry);
  ASSERT_TRUE(manifest.save(storage));
  Manifest reloaded;
  ASSERT_TRUE(reloaded.load(storage));
  EXPECT_TRUE(reloaded.contains("a.epub"));
}
```
