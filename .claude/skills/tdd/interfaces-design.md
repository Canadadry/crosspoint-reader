# Interface Design for Testability

Good interfaces make testing natural. Names are illustrative unless marked real.

1. **Accept dependencies, don't create them**

   ```cpp
   // Testable
   bool saveManifest(const Manifest& manifest, SyncStorage& storage);

   // Hard to test
   bool saveManifest(const Manifest& manifest);  // writes through the Storage singleton
   ```

2. **Return results, don't produce side effects**

   ```cpp
   // Testable (real API, src/util/OpdsFilename.h)
   std::string opdsBookFilename(const std::string& author, const std::string& title, OpdsFilenameFormat format);

   // Hard to test
   void downloadBook(const OpdsEntry& entry);  // builds the name and writes the file in one go
   ```

3. **Small surface area**
   - Fewer functions in the header = fewer tests needed
   - Fewer parameters = simpler test setup

4. **Make illegal states unrepresentable**

   A field meaningful only in some cases says the type is wrong:

   ```cpp
   // Allows an impossible state: a download with no SHA, a delete with a size
   struct SyncOp { int kind; std::string path; uint8_t sha[20]; uint32_t size; };

   // One enum value per operation, each carrying only what it needs
   enum class SyncOpKind : uint8_t { Move, AdoptOrDownload, Download, Delete, Prune };
   ```

   Prefer fixed-size fields to `std::string` in anything held in bulk: RAM is the ceiling (`CLAUDE.md` § Resource Protocol).

5. **Keep functions pure; push state to the top**

   Core logic is pure: same input, same output, no I/O, no global. A thin top layer — the activity, the HAL glue — holds the state and calls into it.

   ```cpp
   // Pure: trivial to test
   bool isSyncablePath(std::string_view path);

   // Stateful: orchestrates, stays at the top (device-only)
   void GitSyncActivity::loop() { ... }
   ```

6. **Check preconditions at the boundary**

   Every public function checks what it requires and refuses with a logged error, so invalid input fails at the call site with a serial log that names it — never an `abort()` (`CLAUDE.md` § Error Handling Philosophy).

   ```cpp
   bool renameSynced(SyncStorage& storage, const char* from, const char* to) {
     if (!isSyncablePath(from) || !isSyncablePath(to)) {
       LOG_ERR("SYNC", "Refusing rename outside the synced set: %s -> %s", from, to);
       return false;
     }
     ...
   }
   ```

   `assert` is kept for states that are impossible by construction, not for bad input.
