---
title: "GitSyncActivity: the Update screen that connects, syncs, can be cancelled and reports"
description: "The sync engine of step 9 has a single blocking call, but nothing on the device connects to Wi-Fi, runs it, shows its progress, lets the owner stop it with Back, or tells the owner what changed or why it failed."
status: needs-triage
---

## Problem Statement

The owner keeps their books in a private GitHub repository and wants **Update** on the
Xteink X4 to make the SD card mirror it. Phase 1 built the sync logic, step 7 loads
`/.crosspoint/github-sync.json`, step 8 extended the HTTP client, and step 9 wires everything
into one device call that runs a whole sync and returns a report.

That call is useless on its own:

- nothing loads the config and refuses cleanly when it is missing or invalid;
- nothing brings Wi-Fi up first, the way the OPDS browser does;
- a sync can take minutes (the first one hashes every book on the card), and nothing shows
  what it is doing, so the device looks frozen;
- the owner cannot stop it: the call blocks the activity loop, so the normal input
  dispatch never sees Back;
- nothing tells the owner what changed (added, updated, moved, deleted, adopted) or why it
  failed (bad token, network lost, too many files, SD card error);
- nothing keeps the device awake for the whole run, so auto-sleep could cut a sync short;
- nothing tears Wi-Fi down afterwards and clears the heap fragmentation it leaves.

## Solution

A new activity, **GitSyncActivity**, that the home menu (step 11) opens:

1. It loads the sync config. Missing or invalid → a clear screen saying so, with the file
   to fix from a computer, and no Wi-Fi is started.
2. It connects to Wi-Fi exactly as the OPDS browser does: use the current connection if
   there is one, otherwise open the Wi-Fi selection screen. A failed or cancelled
   connection → a "Wi-Fi connection failed" screen.
3. It runs the sync in the activity loop, as OPDS downloads and OTA installs do. The
   progress screen shows the current step (preparing, reading the repository, comparing
   with the SD card, then the operation in progress: downloading, checking, moving,
   deleting…) and `n/total : file`, with a progress bar during the apply phase.
4. Back cancels: the press is read from the sync's own progress callback. The screen says
   it will stop after the current file; the sync stops between files and the next Update
   resumes.
5. At the end, a summary shows the added, updated, moved, deleted and adopted counts (plus
   failed and skipped files when there are any), or the error in plain words.
6. Back, Confirm or a tap on the summary leaves; leaving with Wi-Fi on disconnects it and
   silently restarts to the home screen, as OPDS and OTA do.

Auto-sleep is blocked for as long as the activity is open.

## User Stories

1. As the library owner, I want Update to start syncing without further questions when my config and Wi-Fi are fine, so that one press is enough.
2. As the library owner, I want a clear message when `/.crosspoint/github-sync.json` is missing or invalid, so that I know what to fix from my computer.
3. As the library owner, I want no Wi-Fi to be started when the config is invalid, so that the device does not waste battery or reboot for nothing.
4. As the library owner, I want the sync to reuse the Wi-Fi connection that is already up, so that I am not asked for a network twice.
5. As the library owner, I want the usual Wi-Fi selection screen when the device is not connected, so that I can pick or join a network the way I do for OPDS.
6. As the library owner, I want a "Wi-Fi connection failed" screen when I cancel or the connection fails, so that I know why nothing happened.
7. As the library owner, I want the screen to show "Preparing…" as soon as the sync starts, so that I know my press was taken.
8. As the library owner, I want to see "Reading the repository…" with the number of files listed so far, so that a large repository does not look stuck.
9. As the library owner, I want to see "Comparing with the SD card…" while the plan is built, so that I know the device is still working.
10. As the library owner, I want to see what is being done to which file (downloading, checking, moving, deleting) with `n/total : file`, so that I can tell how far the sync is.
11. As the library owner, I want a progress bar during the apply phase, so that I can judge the remaining time at a glance.
12. As the library owner, I want the file shown by its name rather than a long folder path, so that it fits on the screen in every orientation.
13. As the library owner, I want the progress screen not to flash constantly, so that the e-ink panel stays readable and does not slow the sync.
14. As the library owner, I want a phase change to show at once, so that the screen never lags a whole step behind.
15. As the library owner, I want Back to stop the sync, so that I can interrupt a long first sync.
16. As the library owner, I want the screen to say the sync will stop after the current file as soon as I press Back, so that I do not press it again or pull the power.
17. As the library owner, I want the stop to happen between files, so that no book is left half-written.
18. As the library owner, I want the next Update to resume where the stopped one left off, so that stopping costs me nothing.
19. As the library owner, I want the home gesture (on touch devices) to stop the sync and then take me home, so that it behaves like Back plus home.
20. As the library owner, I want a summary with the added, updated, moved, deleted and adopted counts, so that I know what changed on my card.
21. As the library owner, I want "Already up to date" when nothing changed, so that I do not have to read five zeros.
22. As the library owner, I want to see how many files failed and will be retried next time, so that I know the sync was not complete.
23. As the library owner, I want to see how many files were skipped because of a name conflict, so that I can rename them in the repository.
24. As the library owner, I want each kind of failure explained in plain words (bad token or repository, network lost, too many files, SD card error, damaged sync record, redirect refused, not enough memory), so that I know what to do.
25. As the library owner, I want a stopped or interrupted sync to tell me that Update resumes it, so that I am not afraid to press it again.
26. As the library owner, I want the counts of what was already applied before a failure, so that I know the card is partly updated.
27. As the library owner, I want Back, Confirm or a tap to leave the summary, so that any natural gesture works.
28. As the library owner, I want the device to stay awake for the whole sync and its result, so that auto-sleep never interrupts it or hides the result.
29. As the library owner, I want Wi-Fi turned off when I leave, so that it does not drain the battery.
30. As the library owner, I want the device to return to the home screen with a clean memory state after a sync, so that the next Update or the next book opens reliably.
31. As the library owner, I want every screen of the sync readable in all four orientations, so that it works however I hold the device.
32. As the library owner, I want every screen of the sync in my language (English or French), so that the feature feels native.
33. As the library owner, I want my token never shown on screen nor kept in memory after the sync, so that it does not leak.
34. As a firmware developer, I want the sync to run in the same task context as the existing OPDS and OTA downloads, so that the wolfSSL stack needs are already proven.
35. As a firmware developer, I want the activity to hold only a few kilobytes and allocate nothing in the progress callback, so that the sync's 64 KB budget is left intact.
36. As a firmware developer, I want the free heap before the sync and its lowest value during the sync logged, so that the owner's step 12 check can compare it with the budget.
37. As a firmware developer, I want the loop task's stack high-water mark logged after the sync, so that the owner's step 12 check can confirm at least 1 KB is left.
38. As a firmware developer, I want the task watchdog fed from the progress callback, so that a long hash or download never trips it.
39. As a firmware developer, I want the progress state handed to the render task under the render lock, so that a redraw never reads a half-updated path or counter.
40. As a firmware developer, I want a missing case in the error-to-string mapping to produce a compiler warning, so that a new error kind cannot silently show nothing.
41. As a firmware developer, I want one `goToGitSync` entry on the activity manager, so that step 11 only has to call it from the home menu.

## Implementation Decisions

### Modules

- **GitSyncActivity (new)**, in the network activities area beside the web server and
  Wi-Fi selection activities. Activity name `"GitSync"`. Public surface: the usual
  constructor `(GfxRenderer&, MappedInputManager&)`, `onEnter`, `onExit`, `loop`, `render`,
  and `preventAutoSleep()` returning `true`.
- **ActivityManager (modified)**: one new `goToGitSync()` that builds the activity with
  `makeUniqueNoThrow` (on OOM: `LOG_ERR` and stay where we are, like `goToUsbDrive`) and
  calls `replaceActivity`. Replace, not push: the home activity, and its cover buffer
  (freed in its `onExit`), is destroyed before the sync starts. No `HomeMenuItem` value is
  added here; the home entry, its icon and the index mapping are step 11.
- **Translations (modified)**: new `STR_GIT_SYNC_*` keys in `english.yaml` and
  `french.yaml` (list below). Nothing else changes in the I18n library.
- Nothing in the GitSync library, the sync engine, `HttpDownloader`, the Wi-Fi selection
  activity or the themes changes.

### States

```
ConfigError | WifiSelection | WifiFailed | Starting | Running | Done
```

- **onEnter**: `Activity::onEnter()`, then `git_sync_config::load(config)` into the
  activity's member `GitSyncConfig` (653 B: a member, never a stack local, per its header).
  - Any status other than `Ok` → `ConfigError`, render, stop. The loader already logs the
    verdict (constant text, never the token); the activity adds no second log. Wi-Fi is
    not touched, so `onExit` will not restart.
  - `Ok` → the OPDS Wi-Fi flow: if `WiFi.status() == WL_CONNECTED` and the local IP is not
    `0.0.0.0` → `Starting`; otherwise `WifiSelection` and
    `startActivityForResult(WifiSelectionActivity)`, whose handler goes to `Starting` when
    connected and to `WifiFailed` when cancelled or failed (Wi-Fi left as is; `onExit`
    tears it down, as OPDS does).
- **Starting → Running is done in `loop()`, not in the result handler.** By the time
  `loop()` runs, the activity manager has finished the pop and destroyed the Wi-Fi
  selection activity, so its memory is back before the sync allocates. `loop()` sets
  `Running` with phase `Preparing` under the render lock, calls `requestUpdateAndWait()` so
  "Preparing…" is on screen, logs the heap, then calls the engine. This is the "activity
  loop" context of plan step 10, the same task that runs OPDS downloads
  (`OpdsBookBrowserActivity::downloadBook`) and the OTA install (called from the OTA
  activity's result handler, which also runs inside `ActivityManager::loop`). No task is
  created.
- **After the engine returns**: wipe the token (see Security), log heap and stack, store
  the report and set `Done` under the render lock, then either `onGoHome()` (home gesture
  during the run) or `requestUpdate()`.
- **Done / ConfigError / WifiFailed**: `loop()` leaves on Back release, Confirm release or
  a screen tap, through `onGoHome()`. There is no retry from this screen: leaving
  restarts the device to home, which clears the Wi-Fi heap fragmentation before the next
  Update allocates the planner's large block again. "Update again" from the home menu is
  the retry path, and the messages say so.

### Back during the blocking run (input pumped from the progress callback)

The engine blocks `loop()`, so `ActivityManager::loop` and its home-gesture check never
run during a sync. The progress callback is a static function with the activity as its
context (`GitSyncProgressSink`, H-9-2), and on **every** call, ticks included:

1. `resetTaskWatchdogIfSubscribed()` (the existing helper the web server uses during long
   SD work).
2. `mappedInput.update()`; `wasReleased(Back)` → latch `cancelRequested`;
   `wasHomeGesture()` → latch `cancelRequested` and `goHomeAfterRun` (same handling as
   the OPDS download callback, because this `update()` consumes the one-shot event).
3. Sample `ESP.getFreeHeap()` into a running minimum.
4. Update the pending progress snapshot and maybe publish it (next section).
5. Return `!cancelRequested`. The engine latches the cancel (H-9-2): during the tree
   download it stops at once; during the apply phase the file in flight completes or
   fails as a whole and the run ends before the next operation.

The first latch of `cancelRequested` forces an immediate publish so the screen shows
"Stopping after the current file…" and the Back hint disappears. Release, not press,
is the trigger, so the release of the cancelling press cannot also dismiss the summary.
During planning there are no callbacks (H-9-4): a Back press there is read at the first
`Applying` callback if the input layer still reports it, otherwise the owner presses
again; the screen's "Comparing with the SD card…" makes that clear enough.

### Screen refresh throttle

E-ink is slow and a fast refresh blocks the render task, so the callback never renders
itself and never takes the render lock on a tick unless it publishes:

- The callback keeps a **pending** snapshot (loop task only, no lock): phase, op kind,
  `done`, `total`, tree entries listed, the file name (the last segment of the repository
  path, copied into a fixed 256-byte buffer, `MAX_SYNC_PATH_BYTES + 1`, because the
  engine's pointer is valid only during the call, H-9-3).
- It **publishes** (copies pending into the **shown** snapshot under `RenderLock(*this)`,
  then `requestUpdate(true)`, since the deferred flag is only read at the end of a loop
  iteration that is blocked) when:
  - the phase changed (always, so the screen is never a step behind), or
  - the cancel latch was just set, or
  - at least `GIT_SYNC_PROGRESS_MIN_REDRAW_MS = 2000` ms passed since the last publish.
- An operation start within the 2 s window is not published by itself; the next callback
  after the window shows the latest operation. Multiple `requestUpdate(true)` calls
  coalesce in the render task's notification count, so renders never queue up.
- Progress renders use the renderer's default `FAST_REFRESH`. The **Done**,
  **ConfigError** and **WifiFailed** screens render with `HALF_REFRESH`, which clears
  the ghosting left by a long series of fast refreshes.
- 2 s rather than OPDS's 5 s: a sync's operations are many and short, and 2 s keeps
  `n/total` visibly moving while costing well under the refresh time per second of sync.

### Screens and layout (all four orientations)

All geometry comes from `renderer.getScreenWidth()` / `getScreenHeight()` and the theme
metrics, never from 800 or 480. Every screen draws `GUI.drawHeader` with
`tr(STR_GIT_SYNC_TITLE)` at `topPadding`, and `GUI.drawButtonHints` with labels from
`mappedInput.mapLabels(...)` so the remapped and rotated front buttons are labelled
correctly. Content lives between the header's bottom and the hint band's top, inset by
`contentSidePadding`; every text that can be long is cut to that width
(`renderer.truncatedText` for single lines, `UITheme::drawCenteredWrappedText` with at
most 2 lines for error details). Font: `UI_10_FONT_ID`, titles in bold, as the OTA and
clear-cache screens do.

- **Running**:
  - step line, bold, centred: the phase label, or during `Applying` the op-kind label;
  - during `FetchingTree`, when at least one entry was listed: `STR_GIT_SYNC_TREE_ENTRIES_FORMAT`;
  - during `Applying` with `total > 0`: `GUI.drawProgressBar(done, total)` across the
    content width, then `STR_GIT_SYNC_PROGRESS_FORMAT` (`n/total : name`), truncated;
  - after a cancel request: `STR_GIT_SYNC_CANCELLING`;
  - hints: Back = `STR_CANCEL` until a cancel is requested, then empty.
- **Done, success** (`GitSyncError::Ok`):
  - title `STR_GIT_SYNC_COMPLETE`, bold;
  - when added, updated, moved, deleted, adopted and file errors are all zero:
    `STR_GIT_SYNC_UP_TO_DATE`; otherwise the five count lines in that order (added,
    updated, moved, deleted, adopted);
  - then, only when non-zero: the failed-files line (`fileErrors`) and the skipped line
    (`skippedCaseCollision + skippedUnsafeName`).
- **Done, error**:
  - title `STR_GIT_SYNC_STOPPED` for `Cancelled`, `STR_SYNC_FAILED_MSG` (existing
    "Sync failed") otherwise, bold;
  - the error detail (table below), wrapped to 2 lines;
  - when any of the five counts is non-zero, the five count lines, then the optional
    failed and skipped lines, as above.
- **ConfigError**: title `STR_GIT_SYNC_CONFIG_INVALID`, bold; detail
  `STR_GIT_SYNC_CONFIG_HINT`, wrapped.
- **WifiFailed**: `STR_WIFI_CONN_FAILED` (existing), bold.
- Hints on the three end screens: Back = `STR_BACK`, Confirm = `STR_OK_BUTTON` (both
  leave).
- **Vertical fit**: lines are stacked from the top of the content area at
  `lineHeight + verticalSpacing / 2`; a line that would cross the hint band is not drawn.
  The worst case (title, 2 detail lines, 5 counts, 2 optional lines = 10 lines) fits the
  landscape content height (480 − header band − hint band) with the base metrics, and
  the most important lines come first if a theme's metrics are larger. Count lines are
  built with `snprintf` into a 64-byte buffer on the render task's 8 KB stack.
- The orientation review checks each screen in Portrait, Inverted, Landscape CW and
  Landscape CCW against: no constant width or height, every text cut to the content
  width, the hint labels coming from `mapLabels`, and the line-fit rule above.

### Error kind → string (one `tr()` key per `GitSyncError`, H-9-6)

| `GitSyncError` | Title | Detail key |
| --- | --- | --- |
| `Ok` | `STR_GIT_SYNC_COMPLETE` | none (counts) |
| `Cancelled` | `STR_GIT_SYNC_STOPPED` | `STR_GIT_SYNC_ERR_CANCELLED` |
| `OutOfMemory` | `STR_SYNC_FAILED_MSG` | `STR_GIT_SYNC_ERR_MEMORY` |
| `StorageError` | `STR_SYNC_FAILED_MSG` | `STR_GIT_SYNC_ERR_STORAGE` |
| `ManifestCorrupt` | `STR_SYNC_FAILED_MSG` | `STR_GIT_SYNC_ERR_MANIFEST` |
| `TreeDownloadFailed` | `STR_SYNC_FAILED_MSG` | `STR_GIT_SYNC_ERR_TREE_DOWNLOAD` |
| `TreeInvalid` | `STR_SYNC_FAILED_MSG` | `STR_GIT_SYNC_ERR_TREE_INVALID` |
| `TreeTruncated` | `STR_SYNC_FAILED_MSG` | `STR_GIT_SYNC_ERR_TREE_TRUNCATED` |
| `TooManyFiles` | `STR_SYNC_FAILED_MSG` | `STR_GIT_SYNC_ERR_TOO_MANY_FILES_FORMAT` with `MAX_SYNC_FILES` |
| `RedirectRefused` | `STR_SYNC_FAILED_MSG` | `STR_GIT_SYNC_ERR_REDIRECT` |
| `TooManyFetchFailures` | `STR_SYNC_FAILED_MSG` | `STR_GIT_SYNC_ERR_NETWORK` |

The mapping is a `switch` over every enumerator with **no `default`**, followed by a
fallback return of `STR_SYNC_FAILED_MSG` after the switch: an enumerator added later
triggers `-Wswitch` at build time, and an out-of-range value still shows a string. The
engine logs `describeGitSyncError` itself (H-9-5); the activity logs nothing more about
the error.

Op kind → step label: `Move` → `STR_GIT_SYNC_STEP_MOVE`, `AdoptOrDownload` →
`STR_GIT_SYNC_STEP_CHECK`, `Download` → `STR_GIT_SYNC_STEP_DOWNLOAD`, `Delete` →
`STR_GIT_SYNC_STEP_DELETE`, `Prune` → `STR_GIT_SYNC_STEP_PRUNE`, `Skip` →
`STR_GIT_SYNC_STEP_SKIP`, with the same no-`default` rule. Phase → label: `Preparing` →
`STR_GIT_SYNC_PREPARING`, `FetchingTree` → `STR_GIT_SYNC_FETCHING_TREE`, `Planning` →
`STR_GIT_SYNC_PLANNING`, `Applying` → the op-kind label.

### New `tr()` keys

Appended at the end of both files. Format specifiers are identical in both languages.

| Key | English | French |
| --- | --- | --- |
| `STR_GIT_SYNC_TITLE` | Library update | Mise à jour des livres |
| `STR_GIT_SYNC_CONFIG_INVALID` | Sync settings missing or invalid | Réglages de synchro absents ou invalides |
| `STR_GIT_SYNC_CONFIG_HINT` | Fix /.crosspoint/github-sync.json from a computer | Corrigez /.crosspoint/github-sync.json depuis un ordinateur |
| `STR_GIT_SYNC_PREPARING` | Preparing... | Préparation... |
| `STR_GIT_SYNC_FETCHING_TREE` | Reading the repository... | Lecture du dépôt... |
| `STR_GIT_SYNC_TREE_ENTRIES_FORMAT` | %u files listed | %u fichiers listés |
| `STR_GIT_SYNC_PLANNING` | Comparing with the SD card... | Comparaison avec la carte SD... |
| `STR_GIT_SYNC_STEP_MOVE` | Moving | Déplacement |
| `STR_GIT_SYNC_STEP_CHECK` | Checking | Vérification |
| `STR_GIT_SYNC_STEP_DOWNLOAD` | Downloading | Téléchargement |
| `STR_GIT_SYNC_STEP_DELETE` | Deleting | Suppression |
| `STR_GIT_SYNC_STEP_PRUNE` | Removing empty folder | Suppression d'un dossier vide |
| `STR_GIT_SYNC_STEP_SKIP` | Skipping | Fichier ignoré |
| `STR_GIT_SYNC_PROGRESS_FORMAT` | %u/%u : %s | %u/%u : %s |
| `STR_GIT_SYNC_CANCELLING` | Stopping after the current file... | Arrêt après le fichier en cours... |
| `STR_GIT_SYNC_COMPLETE` | Library updated | Bibliothèque à jour |
| `STR_GIT_SYNC_UP_TO_DATE` | Already up to date | Déjà à jour |
| `STR_GIT_SYNC_STOPPED` | Sync stopped | Synchro arrêtée |
| `STR_GIT_SYNC_ADDED_FORMAT` | Added: %u | Ajoutés : %u |
| `STR_GIT_SYNC_UPDATED_FORMAT` | Updated: %u | Mis à jour : %u |
| `STR_GIT_SYNC_MOVED_FORMAT` | Moved: %u | Déplacés : %u |
| `STR_GIT_SYNC_DELETED_FORMAT` | Deleted: %u | Supprimés : %u |
| `STR_GIT_SYNC_ADOPTED_FORMAT` | Already on card: %u | Déjà sur la carte : %u |
| `STR_GIT_SYNC_FILE_ERRORS_FORMAT` | Failed, retried next time: %u | Échecs, réessayés la prochaine fois : %u |
| `STR_GIT_SYNC_SKIPPED_FORMAT` | Skipped (name conflict): %u | Ignorés (conflit de nom) : %u |
| `STR_GIT_SYNC_ERR_CANCELLED` | Press Update again to resume. | Relancez la mise à jour pour reprendre. |
| `STR_GIT_SYNC_ERR_MEMORY` | Not enough memory. Restart and try again. | Mémoire insuffisante. Redémarrez et réessayez. |
| `STR_GIT_SYNC_ERR_STORAGE` | SD card error. Check the card. | Erreur de carte SD. Vérifiez la carte. |
| `STR_GIT_SYNC_ERR_MANIFEST` | Sync record damaged. See the user guide. | Registre de synchro endommagé. Voir le guide. |
| `STR_GIT_SYNC_ERR_TREE_DOWNLOAD` | Cannot read the repository. Check token, name and Wi-Fi. | Lecture du dépôt impossible. Vérifiez jeton, nom et Wi-Fi. |
| `STR_GIT_SYNC_ERR_TREE_INVALID` | GitHub sent an invalid file list. | GitHub a envoyé une liste de fichiers invalide. |
| `STR_GIT_SYNC_ERR_TREE_TRUNCATED` | Repository too large for GitHub's file list. | Dépôt trop grand pour la liste de GitHub. |
| `STR_GIT_SYNC_ERR_TOO_MANY_FILES_FORMAT` | Too many files (limit %u). | Trop de fichiers (limite %u). |
| `STR_GIT_SYNC_ERR_REDIRECT` | GitHub redirected to another site. Sync stopped. | GitHub a redirigé vers un autre site. Synchro arrêtée. |
| `STR_GIT_SYNC_ERR_NETWORK` | Network lost. Press Update again to resume. | Réseau perdu. Relancez la mise à jour pour reprendre. |

Reused existing keys: `STR_CANCEL`, `STR_BACK`, `STR_OK_BUTTON`, `STR_WIFI_CONN_FAILED`,
`STR_SYNC_FAILED_MSG`. `%u` arguments are passed as `unsigned` casts of the `uint32_t`
counts and of `MAX_SYNC_FILES`.

### Memory

- **Activity size**: the member config (656 B), the pending and shown progress snapshots
  (each about 270 B: the 256-byte name plus a few counters), the stored report (H-9-5,
  about 100 B), flags and timestamps. About 1.4 KB beyond the `Activity` base, on the heap
  as every activity is. The two snapshots are fixed-size structs, no `std::string`, so the
  callback never allocates; a `static_assert` keeps each snapshot at or below 288 B.
- **Before the run**: the home activity is destroyed by `replaceActivity` (its cover
  buffer is freed in its `onExit`), the reader is not on the stack (home was reached by
  replacing it), and the Wi-Fi selection activity is popped before `loop()` starts the
  sync. Nothing the activity owns grows during the run.
- **During the run**: the callback copies into fixed buffers only; the render task's
  transient strings (`truncatedText`, `wrappedText`) are on its side, at most one render
  per 2 s, and freed at the end of each render. Their cost is small next to the 64 KB
  budget and is not part of it (the budget counts the sync's own objects, H-9-7).
- **Logs** (verification aids for the owner's step 12 checklist): before the run,
  `LOG_INF` free heap and largest free block (`ESP.getFreeHeap()`,
  `ESP.getMaxAllocHeap()`); after the run, `LOG_INF` the lowest free heap sampled in the
  callback and `uxTaskGetStackHighWaterMark(nullptr)` of the loop task.

### Exit, Wi-Fi and security

- `onExit`: `Activity::onExit()`, wipe the member config with volatile stores (the same
  wiping the config loader uses), then, as OPDS and OTA do, when
  `WiFi.getMode() != WIFI_MODE_NULL`: `WiFi.disconnect(false)`, `delay(30)`,
  `silentRestart()`. With an invalid config Wi-Fi was never started, so there is no
  restart.
- The token is also wiped right after the engine returns, since there is no rerun from
  this activity. The token is never logged nor drawn.
- `preventAutoSleep()` is `true` in every state, as in the OPDS browser. A
  state-dependent value would be wrong here: the main loop resets its inactivity timer
  before calling the activity loop, so after a 20-minute blocking run the first
  iteration on the summary would find the timer 20 minutes old and sleep at once. The
  owner leaves with one press, and leaving restarts the device.
- `skipLoopDelay()` and `requiresExclusiveStorageLoop()` keep their defaults.

### Decisions taken without a `guideline` agent

All choices above follow from the plan's step 10 and UI rules, the step notes, the step 9
PRD and the code; none needed a `guideline` call:

- running in `loop()` instead of the Wi-Fi result handler (frees the Wi-Fi activity
  first; same task either way);
- no retry from the end screens (smaller change; the restart on exit protects the
  planner's large allocation from Wi-Fi heap fragmentation);
- the 2 s throttle, fast refresh during progress and half refresh at the end;
- `preventAutoSleep()` always true (main loop timer order, above);
- the file shown by its last path segment (plan: "`n/total : file`");
- `goToGitSync()` added here so the activity has its launch path; the menu entry stays in
  step 11;
- reusing `STR_SYNC_FAILED_MSG`, `STR_WIFI_CONN_FAILED`, `STR_CANCEL`, `STR_BACK`,
  `STR_OK_BUTTON`.

## Implementation Chunks

1. **Shell and strings.** All new `tr()` keys in both YAML files; the activity with config
   load, `ConfigError`, the OPDS Wi-Fi flow, `WifiFailed`, the end-screen input,
   `preventAutoSleep`, `onExit` (wipe, disconnect, silent restart); `goToGitSync()`.
   `Starting` temporarily goes straight to an end screen. `pio run`.
2. **Run, progress and summary.** The engine call from `loop()`, the progress callback
   (watchdog, input pump, cancel and home latches, heap sampling, throttled publish
   under the render lock), the Running screen, the Done screens with the error table and
   counts, the heap and stack logs. Needs step 9 committed; each hypothesis below is
   checked against it first and this PRD's shapes are adjusted to the real names. `pio
   run`, `./bin/clang-format-fix -g`, orientation review.

## Testing Decisions

- No host tests: the activity depends on Wi-Fi, the renderer, the input manager, the
  render task and the device engine, none of which link into the host suites, and the
  plan's *Verified by* for step 10 is a build, formatting and an orientation review. The
  logic it would test (cancel latching, phase order, error kinds) is already host-tested
  in step 9's sync run suite.
- Verification, all by the agent:
  - `pio run` (default environment) with no new warnings, which also proves the error and
    op-kind switches are total (`-Wswitch`);
  - `./bin/clang-format-fix -g` leaves no diff;
  - the I18n generator accepts both YAML files (it runs as part of the build) and every
    new key exists in both;
  - the orientation review described under "Screens and layout", for each of the four
    orientations and each of the six states;
  - a review that the callback allocates nothing, takes the render lock only when it
    publishes, and returns false once a cancel is latched;
  - a review that `onExit` wipes the config and tears Wi-Fi down exactly as OPDS does.
- Device checks for the owner (added to the step 12 checklist, not run here): Back during
  the tree download, during adoption and during downloads, each followed by a resuming
  Update; the summary counts after an add, a modify, a move and a delete; each orientation
  on the progress and summary screens; the logged heap minimum and stack high-water mark.
- Prior art: the OTA update activity (progress driven from a blocking call in the
  activity loop, render-lock state changes, `GUI` header and progress bar) and the OPDS
  download (input pumped from the progress callback, home gesture, redraw throttle).

## Out of Scope

- The home menu entry, its icon and the `HomeMenuItem` / index mapping (step 11).
- The user guide and the owner's checklist text (step 12).
- Any change to the sync engine, the GitSync library, `HttpDownloader`, the Wi-Fi
  selection activity or the themes.
- A retry button, a "details" list of failed files, or a per-file log on screen.
- Cancelling a single large download mid-transfer (the plan says between files).
- Telling a bad token from an unknown repository (step 8 exposes no HTTP status).
- Touch-specific controls on the progress screen (a tap does nothing while running; the
  home gesture cancels).

## Further Notes

- **Planning has no progress ticks** (step 9 note). On a 2,000-file library the
  "Comparing with the SD card…" screen may stay still for a while; the task watchdog is
  fed again at the first `Applying` callback. If the owner's step 12 timing shows planning
  near the watchdog timeout, the fix belongs in step 9's engine (a planning heartbeat), not
  here.
- **Render task concurrency.** The render task runs on its own 8 KB stack and reads only
  the shown snapshot, the stored report and the state, all written under the render
  lock. The pending snapshot is touched by the loop task alone.
- **Hold-to-release edge case.** A Back press that is held past the end of the run is
  released on the summary and leaves it at once. This is accepted; the result is also in
  the serial log.

### Hypotheses

Step 9 is being implemented while this PRD is written; its PRD
(`docs/prd/triage/phase2-step9-sync-engine-glue.md`) is the only source. Chunk 2 starts by
checking each hypothesis against the committed step 9 code, records the outcome here
(confirmed / corrected), and adapts names without changing the decisions above.

- **H-9-1 — entry point.** `git_sync_engine::run(const GitSyncConfig& config, const
  GitSyncProgressSink& progress) → GitSyncReport`, declared in the firmware network area
  beside `git_sync_config`. The caller owns the config, must already be on Wi-Fi, and
  the call blocks the calling task until the run ends. The engine neither connects nor
  disconnects Wi-Fi, renders, nor reads input.
- **H-9-2 — progress sink and cancel.** `GitSyncProgressSink { void* ctx; bool
  (*onProgress)(void* ctx, const GitSyncProgress&); }`, plain function pointer, no
  `std::function`. Returning false requests a cancel that the engine latches for the
  rest of the run: during the tree download the transfer stops at once; during the apply
  phase the current file completes or fails as a whole and the run ends before the next
  operation, reporting `Cancelled`.
- **H-9-3 — progress record.** `GitSyncProgress { GitSyncPhase phase; bool tick;
  uint32_t done; uint32_t total; SyncOpKind kind; const char* repoPath; uint32_t
  treeEntries; }` with `GitSyncPhase { Preparing, FetchingTree, Planning, Applying }`.
  `tick` is true for a heartbeat with nothing new and false on a phase change or a new
  operation. `done` runs 1..`total` during `Applying`. `repoPath` is repository-relative
  (no leading `/`), at most `MAX_SYNC_PATH_BYTES` bytes, valid only during the call, and
  may be null outside `Applying`. `treeEntries` counts the entries written so far during
  `FetchingTree`.
- **H-9-4 — callback timing.** Called synchronously on the calling task: at each phase
  change, at each operation start, and as a tick after every transfer chunk (about 1 KB)
  and every 4 KB hashed. No calls during planning. The library has no clock, so the
  activity throttles.
- **H-9-5 — report.** `GitSyncReport { GitSyncError error; SyncRunResult run; … }`, a
  small value type returned by copy. The counts are `run.added`, `run.updated`,
  `run.moved`, `run.deleted`, `run.adopted`, `run.fileErrors`,
  `run.skippedCaseCollision`, `run.skippedUnsafeName` (these `SyncRunResult` fields are
  committed). `Ok` with `run.fileErrors > 0` is a completed sync whose failed files the
  next run retries. On any error, the counts still hold what was applied before the stop.
  The engine logs the final report itself (`describeGitSyncError` on failure).
- **H-9-6 — error kinds.** `GitSyncError { Ok, Cancelled, OutOfMemory, StorageError,
  ManifestCorrupt, TreeDownloadFailed, TreeInvalid, TreeTruncated, TooManyFiles,
  RedirectRefused, TooManyFetchFailures }`. If step 9 lands with other values, the
  mapping table gets one row per real value, and the build's `-Wswitch` warning forces
  it.
- **H-9-7 — heap during the run.** The engine allocates its own objects with
  `makeUniqueNoThrow` and stays within its 64 KB budget (excluding the TLS session), with
  the planner's ~49 KB block allocated after the tree download; an allocation failure is
  reported as `OutOfMemory`, never an abort. It needs nothing from the caller beyond the
  config and the sink, so the activity's only duties are to hold little and allocate
  nothing in the callback.
- **H-9-8 — reading state during the run.** The engine's move hook updates
  `RECENT_BOOKS` and `APP_STATE.openEpubPath` from the calling task during the run; the
  activity holds no copy of either, so nothing it shows or keeps goes stale.
