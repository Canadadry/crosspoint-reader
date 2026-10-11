#pragma once

// Values are appended, never inserted; the display order comes from home_menu's table.
enum class HomeMenuItem { NONE, FILE_BROWSER, RECENTS, OPDS_BROWSER, FILE_TRANSFER, SETTINGS_MENU, GIT_SYNC };

namespace home_menu {

struct HomeMenuOptions {
  bool opds = false;
  bool gitSync = false;
};

enum class Gate { ALWAYS, OPDS, GIT_SYNC };

struct Row {
  HomeMenuItem item;
  Gate gate;
};

inline constexpr Row ROWS[] = {
    {HomeMenuItem::FILE_BROWSER, Gate::ALWAYS},  {HomeMenuItem::RECENTS, Gate::ALWAYS},
    {HomeMenuItem::OPDS_BROWSER, Gate::OPDS},    {HomeMenuItem::GIT_SYNC, Gate::GIT_SYNC},
    {HomeMenuItem::FILE_TRANSFER, Gate::ALWAYS}, {HomeMenuItem::SETTINGS_MENU, Gate::ALWAYS},
};

constexpr bool isVisible(const Row& row, HomeMenuOptions options) {
  switch (row.gate) {
    case Gate::ALWAYS:
      return true;
    case Gate::OPDS:
      return options.opds;
    case Gate::GIT_SYNC:
      return options.gitSync;
  }
  return false;
}

// Number of menu rows, recent book covers excluded.
constexpr int itemCount(HomeMenuOptions options) {
  int count = 0;
  for (const Row& row : ROWS) {
    if (isVisible(row, options)) ++count;
  }
  return count;
}

// 0 for NONE and for an item the options hide.
constexpr int menuItemToIndex(HomeMenuItem item, HomeMenuOptions options) {
  int i = 0;
  for (const Row& row : ROWS) {
    if (!isVisible(row, options)) continue;
    if (row.item == item) return i;
    ++i;
  }
  return 0;
}

// NONE for an index outside [0, itemCount).
constexpr HomeMenuItem indexToMenuItem(int index, HomeMenuOptions options) {
  if (index < 0) return HomeMenuItem::NONE;
  int i = 0;
  for (const Row& row : ROWS) {
    if (!isVisible(row, options)) continue;
    if (i == index) return row.item;
    ++i;
  }
  return HomeMenuItem::NONE;
}

}  // namespace home_menu
