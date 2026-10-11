---
title: "Home menu entry: Update Library, shown only when the GitHub sync config is valid"
description: "The GitHub sync screen exists (step 10) but nothing on the home menu opens it, and the home menu's index math only knows about one optional entry (OPDS)."
status: needs-triage
---

## Problem Statement

The owner keeps their books in a private GitHub repository. Steps 1–10 built the whole sync:
the config loader, the engine and the `GitSyncActivity` screen that connects, syncs, can be
cancelled and reports. But the owner has no way to reach it: the home menu shows Browse
Files, Recent Books, the OPDS Browser (only when OPDS servers are configured), File Transfer
and Settings, and nothing else.

Two constraints come with the new entry:

- Owners who never set up GitHub sync (most users of this firmware) must not see it. The
  plan says a missing or invalid `/.crosspoint/github-sync.json` hides the entry.
- The home menu maps a flat selector index onto "recent book covers, then menu rows", and
  the menu rows onto `HomeMenuItem` values through two hand-written functions
  (`menuItemToIndex` / `indexToMenuItem`) plus a separately hand-built list of labels and
  icons in `render()`. The three are kept in step by hand around one optional row (OPDS).
  A second optional row doubles the combinations, and any drift between them opens the
  wrong screen or highlights the wrong row.

## Solution

When the owner's config file is present and valid, the home menu shows one more row,
**Update Library** ("Mettre à jour les livres" in French), with a download icon on the themes
that draw menu icons. It sits right after the OPDS Browser row (or after Recent Books when
there is no OPDS server), so the two "get books from the network" entries are together.
Selecting it, by button or by touch, opens the GitHub sync screen.

When the file is missing or invalid, the menu is exactly what it is today. The check runs
each time the home screen is entered, so a config written over USB or fixed from a PC shows
up (or disappears) the next time the owner comes back home, without a restart.

The menu order, the row count and the index ↔ item mapping come from one small pure module,
host-tested for every combination of {OPDS servers} × {Update entry}; the home screen builds
its row labels and icons from that same mapping, so render, navigation and touch can no
longer disagree.

## User Stories

1. As the library owner, I want an Update Library entry on the home menu, so that I can start a sync without leaving the main screen.
2. As the library owner, I want the entry to appear only when my `/.crosspoint/github-sync.json` is present and valid, so that I am not offered a button that can only fail.
3. As a reader who never set up GitHub sync, I want my home menu unchanged, so that the firmware stays uncluttered for me.
4. As the library owner, I want the entry to appear after I copy the config file over USB and return home, so that I do not need to reboot to see it.
5. As the library owner, I want the entry to disappear when I delete or break the config file, so that the menu reflects the card's real state.
6. As the library owner, I want an invalid config to leave a `LOG_ERR` line in the serial log, so that I can find out why the entry is hidden.
7. As the library owner, I want a missing config to log nothing, so that the log of a device without sync stays clean.
8. As the library owner, I want the Update entry next to the OPDS Browser, so that the network sources of books are grouped.
9. As the library owner, I want a recognisable download icon on the Lyra themes, so that the entry is as easy to spot as the others.
10. As the library owner using the Classic or RoundedRaff theme, I want the entry to look like the other rows of that theme, so that the menu stays consistent (those themes draw no menu icons).
11. As the library owner, I want Confirm on the Update row to open the sync screen, so that it behaves like every other row.
12. As a touch-device owner, I want tapping the Update row to open the sync screen, and pressing it to highlight it, so that touch matches the buttons.
13. As the library owner, I want Up/Down navigation to wrap through the new row like any other, so that the menu count is right.
14. As the library owner with recent books, I want the cover tiles to stay selectable before the menu rows, so that the new row does not shift what Back or the covers do.
15. As the library owner on the RoundedRaff theme, I want the Continue Reading row to stay first, so that the new row does not break that theme's layout.
16. As the library owner with three recent covers (Lyra 3 Covers), I want the selector index to count the three covers before the menu, so that the right row is highlighted.
17. As the library owner, I want the cursor to come back on Update Library when I leave the sync screen without a restart (the invalid-config screen), so that I land where I was.
18. As the library owner whose config became invalid between the home screen and the sync screen, I want the cursor to fall back to the first row when I come back, so that it never points past the end of the menu.
19. As the library owner with OPDS servers and sync configured, I want both entries visible and each to open its own screen, so that neither hides the other.
20. As the library owner with OPDS servers but no sync config, I want the OPDS row to keep working exactly as before, so that nothing regresses.
21. As the library owner, I want returning home from the OPDS browser, the file browser, the recent books list, File Transfer or Settings to still put the cursor on that entry, so that the existing return behaviour is kept with the new row in between.
22. As the library owner, I want the French label translated, so that the menu reads naturally in French.
23. As the library owner, I want the entry label to say "Update Library", not just "Update", so that it is not confused with the firmware update in Settings.
24. As the library owner, I want opening the home screen to stay fast, so that checking for the config does not make home feel slower (one small SD read, and only a file-exists check when there is no config).
25. As the library owner, I want the home screen to free the memory used to check the config before it loads covers, so that the cover snapshot still gets its buffer.
26. As the library owner, I want the token never to be logged or kept in memory by the home screen, so that checking the config does not leak it.
27. As a firmware developer, I want one source of truth for the home menu order, so that render, button navigation, touch and the initial cursor cannot drift.
28. As a firmware developer, I want the menu index math covered by host tests for every option combination, so that adding a future optional row is a table change with a failing test, not a device hunt.
29. As a firmware developer, I want a new `HomeMenuItem` value to make the compiler flag every unhandled switch, so that a forgotten label, icon or action is a build warning.
30. As a firmware developer, I want the icon to be a 128-byte flash constant in the same format as the other home icons, so that it costs no RAM and needs no new drawing path.
31. As a firmware developer, I want the icon pixels to come from the Lucide `download` icon already in the compiled list-icon set, so that it matches the download icon used elsewhere in the UI.
32. As a firmware developer, I want the entry to call the activity manager's one sync launcher, so that the home screen knows nothing about Wi-Fi, the engine or the config contents.
33. As a reviewer, I want a written table of every {OPDS} × {recent books} × {Update} combination in this PRD, so that I can check the mapping without a device.
34. As the library owner, I want the existing home screen behaviours (Back opens the last book, cover loading popup, clean refresh after wake) unchanged, so that the new row is the only visible difference.

## Implementation Decisions

### Modules

- **HomeMenu (new, header-only, pure)**, beside `HomeActivity`. It owns:
  - the `HomeMenuItem` enum, moved out of the activity manager header (which then includes
    this header). One value is appended: `GIT_SYNC`, after `SETTINGS_MENU`. Appending rather
    than inserting keeps every existing numeric value; nothing persists the enum, but there
    is no reason to renumber. The enum order does not drive the display order.
  - `HomeMenuOptions { bool opds; bool gitSync; }`.
  - `constexpr int itemCount(HomeMenuOptions)`: the number of menu rows, recent books
    excluded (4 + opds + gitSync).
  - `constexpr int menuItemToIndex(HomeMenuItem, HomeMenuOptions)`: the row of a visible item;
    `0` for `NONE` and for an item hidden by the options (the existing OPDS fallback, now
    shared by Update).
  - `constexpr HomeMenuItem indexToMenuItem(int, HomeMenuOptions)`: the item of a row;
    `NONE` for a negative or out-of-range index.
  - All three walk one `constexpr` ordered table of `{item, which option gates it}`:
    `FILE_BROWSER`, `RECENTS`, `OPDS_BROWSER` (opds), `GIT_SYNC` (gitSync),
    `FILE_TRANSFER`, `SETTINGS_MENU`. No heap, no logging, no Arduino include, so it links
    into a host gtest. Names sit in a `home_menu` namespace; the plan's
    `menuItemToIndex` / `indexToMenuItem` names are kept.
- **HomeActivity (modified)**:
  - the two private static mapping functions are removed in favour of the HomeMenu ones;
  - `hasOpdsServers` is replaced by one member `HomeMenuOptions menuOptions` (2 bytes);
  - `onEnter()`: `menuOptions.opds = OPDS_STORE.hasServers()` (as today), then
    `menuOptions.gitSync = git_sync_config::exists()`, both **before** `loadRecentBooks`.
    `exists()` is called exactly once per `onEnter()` and never from `loop()` or
    `render()`;
  - `getMenuItemCount()` = recent books shown + `home_menu::itemCount(menuOptions)`;
  - `render()` builds the label and icon lists by walking `indexToMenuItem(i, menuOptions)`
    for `i` in `[0, itemCount)`, with a label switch and an icon switch over `HomeMenuItem`
    (no `default`, so `-Wswitch` flags a future value; `NONE` maps to an empty label and
    `UIIcon::None`). The Continue Reading row is still prepended when the theme asks for
    it. Both vectors get `.reserve(itemCount + 1)` before the pushes. The hard-coded
    "insert OPDS at position 2" goes away;
  - `loop()`'s action switch gains `GIT_SYNC → onGitSyncOpen()`, which calls
    `activityManager.goToGitSync()` (H-10-1);
  - touch rows, swipe, Back and the cover tiles are unchanged: they already derive from
    `getMenuItemCount()` and `recentBooks.size()`.
- **ActivityManager (modified)**: includes HomeMenu instead of declaring the enum; `goHome`'s
  name-to-item mapping gains `"GitSync" → HomeMenuItem::GIT_SYNC` (H-10-2), next to the
  existing `"OpdsBookBrowser" → OPDS_BROWSER`. `goToGitSync()` itself is step 10's
  (H-10-1); this step only calls it.
- **UIIcon (modified)**: one value `Sync` appended after `Usb`. Generic name like `Library` or
  `Transfer`, describing the picture's role, not the feature.
- **Lyra theme (modified)**: `iconForName` maps `UIIcon::Sync` to a new legacy 32×32 bitmap
  `SyncIcon`. Lyra 3 Covers inherits it. Classic (`BaseTheme`) and RoundedRaff ignore row
  icons and are not touched. `listIconFor` (list rows, a different bitmap format) is not
  touched: nothing in this step draws a sync icon in a list.
- **SyncIcon (new asset)**: a 128-byte `static const` array in the legacy `drawIcon` format
  (1 bpp, MSB first, 0 = ink, stored rotated 90° counter-clockwise), in its own header next
  to `library.h`, the same shape as every other home icon. Its pixels are the Lucide
  `download` icon at 32 px, i.e. exactly the pixels of `icon_download_32` already in the
  compiled list-icon set (`listIcons.manifest` lists `download`). Recipe (checked while
  writing this PRD; no SVG rasterizer is needed): dump the `icon_download_32` bits to a
  32×32 PNG (black on white, unrotated), then run the repository's
  `scripts/convert_icon.py <png> sync 32 32`, which rotates and packs it into `SyncIcon`.
  Rendering the result back through `drawIcon`'s mapping gives the download arrow over the
  tray, identical to `icon_download_32`. The PNG is a throw-away and is not committed.
- **Translations (modified)**: one new key `STR_GIT_SYNC_MENU` in `english.yaml`
  ("Update Library") and `french.yaml` ("Mettre à jour les livres"). Title case matches
  "Browse Files", "Recent Books", "File Transfer".

### Decisions taken without a `guideline` agent

No `guideline` agent was available in this run; each choice below follows the guideline
rules (make the plan work, keep it safe, smaller change between two working options) and is
recorded here:

1. **Where `exists()` is called: once per `onEnter()`, cached in `menuOptions.gitSync`.**
   Step 2.7 made `exists()` re-read the file on every call (USB mode can rewrite it), so a
   per-entry call is the freshest the menu can be without polling. Calling it in
   `render()` or `loop()` would read the SD card on every frame. Cost: with no config file,
   one `Storage.exists` and no heap; with a file, a read of at most 1 KB and about 2.3 KB of
   transient heap (the 656-byte config plus the 1,026-byte read buffer plus the file
   handle), all freed before `exists()` returns. It runs before `loadRecentBooks` and long
   before the cover snapshot buffer, so the transient block is released before any
   long-lived home allocation and cannot pin a hole under them.
2. **Position: after OPDS, before File Transfer.** The plan says "built like the OPDS entry";
   grouping the two network book sources is the natural reading, and it keeps the four
   always-present rows in their current relative order.
3. **Label: a new key, not a reuse.** Step 10's key list has no menu label (its
   `STR_GIT_SYNC_TITLE`, "Library update", is a screen title), and the existing
   `STR_UPDATE` ("Update") means the firmware update. A dedicated `STR_GIT_SYNC_MENU`
   keeps one meaning per key and avoids confusion with Settings → Check for updates. The
   text keeps the plan's word "Update". If step 10 lands a menu-label key after all, reuse
   it instead (H-10-3).
4. **Icon: Lucide `download`, as a new legacy bitmap.** It is the only "update/sync"-like
   picture already in the compiled icon set (`refresh-cw`, `folder-sync` and
   `cloud-download` exist only as SVGs in the SDK, and no SVG rasterizer is available to
   the build tooling here). The home menu's only icon-drawing theme (Lyra) uses the legacy
   `drawIcon` format, so a 128-byte flash array is the smallest change; drawing SDK-format
   icons in Lyra's menu would be a new renderer path for one row.
5. **Extract the index math into a pure module and test it (yes).** It costs one small
   header and one short gtest, removes the three hand-synchronised copies of the menu order
   (two mapping functions and the render list), and turns the plan's "review that the index
   math still holds" into a test plus the review table below.
6. **The `goHome` mapping for `"GitSync"` is in scope.** It is the return half of the index
   mapping; without it, leaving the sync screen's invalid-config state lands on the first
   row instead of Update. When the config became invalid in between, `menuItemToIndex`
   falls back to row 0, so the cursor can never point past the end.
7. **The menu overflow on two themes is accepted, not fixed here** (see Further Notes):
   only the six-row case (OPDS servers and sync both configured) on physical-button devices
   with the Classic or Lyra 3 Covers theme is affected, the Update row itself stays fully
   visible, and fixing it means changing theme metrics for every user. It goes on the
   owner's step 12 device checklist.

### Index review table

`N` is the number of recent books shown as cover tiles (`recentBooks.size()`, at most the
theme's `homeRecentBooksCount`: 1 on Classic, Lyra and RoundedRaff, 3 on Lyra 3 Covers; 0
when there are none). Selector indices `0 … N−1` are the covers; menu row `k` is selector
`N + k`; the menu count is `N + itemCount`. On RoundedRaff (`homeContinueReadingInMenu`),
the Continue Reading row is drawn as rendered row 0 and is selector 0 when `N = 1`, so
rendered row `r` is selector `r`; on the other themes the rendered selection is
`selector − N` (−1, no row highlighted, while a cover is selected).

| OPDS | Update | itemCount | Rows (menu index → item) | `GIT_SYNC` → selector | `OPDS_BROWSER` → selector | `SETTINGS_MENU` → selector | Menu count |
| --- | --- | --- | --- | --- | --- | --- | --- |
| no | no | 4 | 0 Browse, 1 Recents, 2 Transfer, 3 Settings | N + 0 (hidden) | N + 0 (hidden) | N + 3 | N + 4 |
| yes | no | 5 | 0 Browse, 1 Recents, 2 OPDS, 3 Transfer, 4 Settings | N + 0 (hidden) | N + 2 | N + 4 | N + 5 |
| no | yes | 5 | 0 Browse, 1 Recents, 2 Update, 3 Transfer, 4 Settings | N + 2 | N + 0 (hidden) | N + 4 | N + 5 |
| yes | yes | 6 | 0 Browse, 1 Recents, 2 OPDS, 3 Update, 4 Transfer, 5 Settings | N + 3 | N + 2 | N + 5 | N + 6 |

The recent-books dimension, for each row of the table:

| Recent books | Theme | Selector 0 | First menu row | Rendered rows |
| --- | --- | --- | --- | --- |
| none (N = 0) | any | Browse Files | selector 0 | itemCount |
| N = 1 | Classic, Lyra | the cover | selector 1 | itemCount, selection = selector − 1 |
| N = 1 | RoundedRaff | Continue Reading (rendered row 0) | selector 1 (rendered row 1) | itemCount + 1, selection = selector |
| N = 1…3 | Lyra 3 Covers | the first cover | selector N | itemCount, selection = selector − N |

`indexToMenuItem` is only ever called with `selector − N` after the `selector < N` cover
check in `activateSelection`, so its argument is in `[0, itemCount)`; anything else yields
`NONE`, which the action switch ignores. `menuItemToIndex` for a hidden item or `NONE`
yields 0, so `onEnter`'s `N + menuItemToIndex(...)` is always a valid selector.

### Memory and flash

- RAM: one extra `bool` in the activity (inside `menuOptions`); the label and icon vectors
  grow by one element each and are reserved once per render, as before (now without the
  mid-vector inserts). The 2.3 KB transient of `exists()` is described in decision 1.
- Flash: `SyncIcon` 128 B; two translation strings; the HomeMenu functions are `constexpr`
  and fold into their callers.
- No new task, no `std::function` added (the theme's existing `drawButtonMenu` signature is
  unchanged), no `std::string` in new code.

### Orientation

The home screen always renders in Portrait (the reader resets the orientation to Portrait on
exit, and `drawIcon` documents that the themes render in forced Portrait), so there is no
landscape or inverted layout of the home menu to review. The review covers Portrait on the
four themes (Classic, Lyra, Lyra 3 Covers, RoundedRaff), with and without touch.

## Implementation Chunks

1. **HomeMenu module and its test, no visible change.** The pure header with the enum moved
   in and `GIT_SYNC` appended, the table-driven gtest, `HomeActivity` and `ActivityManager`
   switched over to it with `menuOptions.gitSync` always false, `render()` built from
   `indexToMenuItem`. The `GIT_SYNC` cases in `HomeActivity`'s switches do nothing yet.
   Host tests and `pio run`.
2. **The entry.** `git_sync_config::exists()` in `onEnter()`, `STR_GIT_SYNC_MENU` in both
   YAML files, `UIIcon::Sync` and `SyncIcon` with Lyra's mapping, `onGitSyncOpen()` →
   `goToGitSync()`, and the `"GitSync"` name mapping in `goHome`. Needs step 10 committed:
   check H-10-1 … H-10-5 against it first and adjust the names. `pio run`,
   `./bin/clang-format-fix -g`, and the index review against the table above.

## Testing Decisions

- A good test here checks the public mapping only: for a given `HomeMenuOptions`, the
  ordered list of visible items, the count, and the round trips. It does not look at the
  table's layout or at how the functions walk it.
- **New host suite `home_menu`** (gtest, registered in the test CMake list; header-only
  module, so the executable is just the test file plus the common test target). Cases,
  table-driven over the four `{opds, gitSync}` combinations, each with its expected
  ordered row list:
  - `itemCount` equals the expected list length;
  - `indexToMenuItem(i)` equals the expected item for every `i` in range;
  - `menuItemToIndex(indexToMenuItem(i)) == i` for every `i` in range;
  - every item absent from the expected list maps to 0, and `NONE` maps to 0;
  - `indexToMenuItem` returns `NONE` for −1 and for `itemCount`;
  - the four always-present items keep their relative order (Browse, Recents, Transfer,
    Settings) in every combination;
  - `GIT_SYNC` sits immediately after `OPDS_BROWSER` when both are visible, and after
    `RECENTS` otherwise.
  The recent-books offset and the RoundedRaff Continue row live in `HomeActivity`, which
  does not link on the host; they are covered by the review table above.
- **Prior art**: `test/url_origin/` (a pure `src/` module tested on the host) for the
  layout; the table-driven style of `test/git_sync_path_filter/`.
- **Agent verification**: the host test suite; `pio run` (default environment) with no new
  warnings, which also proves the label, icon and action switches cover `GIT_SYNC`
  (`-Wswitch`); `./bin/clang-format-fix -g`; the I18n generator accepts both YAML files;
  rendering `SyncIcon` back through `drawIcon`'s pixel mapping gives the
  `icon_download_32` picture; and a review of `HomeActivity` against the index table.
- **Device checks for the owner** (to add to the step 12 checklist, not run here): the entry
  is absent with no config, absent with a broken config (and the serial log shows the
  `GSYNC` error), present after copying a valid config over USB and returning home; Confirm
  and tap open the sync screen; on Lyra the download icon is drawn; on Classic and Lyra 3
  Covers with buttons and OPDS servers configured, how much of the Settings row the button
  hints cover.

## Out of Scope

- `GitSyncActivity`, `goToGitSync()`, the sync strings and screens (step 10).
- The user guide and the owner's checklist text (step 12).
- Any change to the config loader, the sync engine, the OPDS store, the theme metrics or
  the themes' menu drawing (beyond Lyra's icon lookup).
- Fixing the six-row overlap with the button hints on Classic and Lyra 3 Covers.
- A sync icon for list rows (`listIconFor`), the network mode menu or anywhere else.
- Polling for the config while the home screen is open; it is checked on entry only.
- Showing the last sync time, a badge or any status on the home menu.

## Further Notes

### Six rows on button devices

With OPDS servers and a valid sync config, the menu has six rows. In Portrait (800 px tall),
with physical-button hints (40 px at the bottom):

- Lyra: rows start at 314 px, step 72 px; the sixth row ends at 738 px. Fits.
- Classic: rows start at 460 px, step 53 px; the sixth row ends at 770 px, 10 px into the
  hint bar. The row frame's bottom is covered; its label (centered) stays readable.
- Lyra 3 Covers: rows start at 372 px, step 72 px; the sixth row (Settings) spans 732–796 px,
  so its label sits partly under the hints.
- RoundedRaff: paginates the menu itself. Fits.
- Touch devices have no hint bar. Fits on every theme.

The Update row is the fourth row in that case and is always fully visible. Recorded as a
known limitation for the owner (decision 7).

### Hypotheses

Step 10 is being implemented while this PRD is written; it was read from its PRD only.
Each assumption about its outcome is checked against the committed step 10 code at the
start of chunk 2, and this PRD's names are adjusted to the real ones.

- **H-10-1**: `ActivityManager` gains a public `void goToGitSync()` with no arguments, which
  replaces the current activity (home included) with `GitSyncActivity`, and on OOM logs and
  leaves the home screen in place. The home screen calls nothing else of step 10.
- **H-10-2**: `GitSyncActivity`'s activity name is `"GitSync"`, the string `goHome` matches
  to restore the cursor.
- **H-10-3**: step 10 adds no menu-label key (its keys are `STR_GIT_SYNC_*` screen texts,
  none named `STR_GIT_SYNC_MENU`), no `HomeMenuItem` value and no `UIIcon` value. If it
  adds a menu-label key, reuse it instead of adding `STR_GIT_SYNC_MENU`; if it adds
  `HomeMenuItem::GIT_SYNC` or a sync `UIIcon`, keep its definition and only move the enum
  into HomeMenu.
- **H-10-4**: step 10 does not modify `HomeActivity` or the home menu.
- **H-10-5**: `GitSyncActivity` loads and validates the config itself on entry (it does not
  trust the home screen's `exists()`), shows its own invalid-config screen if the file
  changed in between, and leaves through `onGoHome()` without a restart in that case; after
  a run with Wi-Fi it restarts silently to the home screen with no initial item. So the
  `"GitSync"` mapping only matters on the no-Wi-Fi exit, and is harmless otherwise.
