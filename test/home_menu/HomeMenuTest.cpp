#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "HomeMenu.h"

using home_menu::HomeMenuOptions;
using home_menu::indexToMenuItem;
using home_menu::itemCount;
using home_menu::menuItemToIndex;

namespace {

struct Case {
  HomeMenuOptions options;
  std::vector<HomeMenuItem> rows;
};

const std::vector<Case>& cases() {
  static const std::vector<Case> all = {
      {{false, false},
       {HomeMenuItem::FILE_BROWSER, HomeMenuItem::RECENTS, HomeMenuItem::FILE_TRANSFER, HomeMenuItem::SETTINGS_MENU}},
      {{true, false},
       {HomeMenuItem::FILE_BROWSER, HomeMenuItem::RECENTS, HomeMenuItem::OPDS_BROWSER, HomeMenuItem::FILE_TRANSFER,
        HomeMenuItem::SETTINGS_MENU}},
      {{false, true},
       {HomeMenuItem::FILE_BROWSER, HomeMenuItem::RECENTS, HomeMenuItem::GIT_SYNC, HomeMenuItem::FILE_TRANSFER,
        HomeMenuItem::SETTINGS_MENU}},
      {{true, true},
       {HomeMenuItem::FILE_BROWSER, HomeMenuItem::RECENTS, HomeMenuItem::OPDS_BROWSER, HomeMenuItem::GIT_SYNC,
        HomeMenuItem::FILE_TRANSFER, HomeMenuItem::SETTINGS_MENU}},
  };
  return all;
}

std::vector<HomeMenuItem> visibleRows(HomeMenuOptions options) {
  std::vector<HomeMenuItem> rows;
  for (int i = 0; i < itemCount(options); ++i) rows.push_back(indexToMenuItem(i, options));
  return rows;
}

std::string label(HomeMenuOptions options) {
  return std::string("opds=") + (options.opds ? "1" : "0") + " gitSync=" + (options.gitSync ? "1" : "0");
}

}  // namespace

TEST(HomeMenu, ItemCountMatchesTheVisibleRows) {
  for (const Case& c : cases()) {
    EXPECT_EQ(itemCount(c.options), static_cast<int>(c.rows.size())) << label(c.options);
  }
}

TEST(HomeMenu, RowsAppearInMenuOrder) {
  for (const Case& c : cases()) {
    EXPECT_EQ(visibleRows(c.options), c.rows) << label(c.options);
  }
}

TEST(HomeMenu, EachVisibleItemMapsBackToItsRow) {
  for (const Case& c : cases()) {
    for (int i = 0; i < static_cast<int>(c.rows.size()); ++i) {
      EXPECT_EQ(menuItemToIndex(indexToMenuItem(i, c.options), c.options), i) << label(c.options) << " row " << i;
    }
  }
}

TEST(HomeMenu, HiddenItemsAndNoneMapToTheFirstRow) {
  const HomeMenuItem all[] = {HomeMenuItem::FILE_BROWSER,  HomeMenuItem::RECENTS,       HomeMenuItem::OPDS_BROWSER,
                              HomeMenuItem::FILE_TRANSFER, HomeMenuItem::SETTINGS_MENU, HomeMenuItem::GIT_SYNC};
  for (const Case& c : cases()) {
    EXPECT_EQ(menuItemToIndex(HomeMenuItem::NONE, c.options), 0) << label(c.options);
    for (const HomeMenuItem item : all) {
      if (std::find(c.rows.begin(), c.rows.end(), item) != c.rows.end()) continue;
      EXPECT_EQ(menuItemToIndex(item, c.options), 0) << label(c.options) << " item " << static_cast<int>(item);
    }
  }
}

TEST(HomeMenu, IndexOutsideTheMenuIsNone) {
  for (const Case& c : cases()) {
    EXPECT_EQ(indexToMenuItem(-1, c.options), HomeMenuItem::NONE) << label(c.options);
    EXPECT_EQ(indexToMenuItem(itemCount(c.options), c.options), HomeMenuItem::NONE) << label(c.options);
  }
}

TEST(HomeMenu, FixedRowsKeepTheirRelativeOrder) {
  const std::vector<HomeMenuItem> fixed = {HomeMenuItem::FILE_BROWSER, HomeMenuItem::RECENTS,
                                           HomeMenuItem::FILE_TRANSFER, HomeMenuItem::SETTINGS_MENU};
  for (const Case& c : cases()) {
    std::vector<HomeMenuItem> rows = visibleRows(c.options);
    rows.erase(std::remove_if(rows.begin(), rows.end(),
                              [](HomeMenuItem item) {
                                return item == HomeMenuItem::OPDS_BROWSER || item == HomeMenuItem::GIT_SYNC;
                              }),
               rows.end());
    EXPECT_EQ(rows, fixed) << label(c.options);
  }
}

TEST(HomeMenu, GitSyncFollowsOpdsWhenShownElseRecents) {
  for (const Case& c : cases()) {
    if (!c.options.gitSync) continue;
    const int row = menuItemToIndex(HomeMenuItem::GIT_SYNC, c.options);
    const HomeMenuItem before = c.options.opds ? HomeMenuItem::OPDS_BROWSER : HomeMenuItem::RECENTS;
    EXPECT_EQ(indexToMenuItem(row - 1, c.options), before) << label(c.options);
  }
}
