# When to Use a Test Double

Host tests build without the device, so the device's boundaries are the only things replaced:

- **Storage**: SD access goes through `HalStorage` / `HalFile` on the device; logic that reads or writes files takes a small storage interface instead, implemented in memory for tests. The double follows the real card's behaviour, not a friendlier one: a rename does not overwrite (`src/activities/reader/ProgressFile.h:53-56`), names are case-insensitive (FAT), and a write can fail or stop halfway.
- **Network**: `HttpDownloader` on the device; a fetch callback in tests, returning recorded bytes or a failure.
- **Display and input**: `GfxRenderer`, `MappedInputManager`. Keep logic out of activities so it doesn't need them.
- **Time and memory**: `millis()`, `ESP.getFreeHeap()` — pass values in.

Don't replace:

- Your own modules
- Internal collaborators
- Anything in `lib/` a test can build and call for real (`StreamingJsonParser`, `Utf8`, …)

## Designing for Test Doubles

**1. Pass the dependency in**

The module receives its storage or its fetch as a parameter instead of reaching for a singleton:

```cpp
// Easy to test: the caller chooses the storage (illustrative)
bool runSync(const SyncPlan& plan, SyncStorage& storage, FetchFn fetch, void* fetchCtx);

// Hard to test: hidden dependency on the SD card singleton
bool runSync(const SyncPlan& plan);  // calls Storage.openFileForWrite(...) inside
```

The test passes an in-memory storage; the device passes one backed by `HalStorage`. Callbacks are a function pointer plus a context, not `std::function` (`CLAUDE.md` § Template and `std::function` Bloat).

**2. One narrow function per boundary operation**

A storage interface has `openRead`, `openWrite`, `rename`, `remove`, `mkdir`, `exists` — not one `do(op, ...)` entry point with a switch. Each double then implements one plain behavior:

- No conditional logic in the test setup
- Easy to see which operations a test exercises
- A failing double (fail at the N-th write, leave a torn file) tests the crash path in one line
