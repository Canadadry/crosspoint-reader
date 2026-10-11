add_executable(SyncExecutorTest
  BookPathTest.cpp
  MemorySyncStorageTest.cpp
  ${REPO_ROOT}/lib/GitSync/SyncPathFold.cpp
  ${REPO_ROOT}/lib/GitSync/BookmarkPath.cpp
  ${REPO_ROOT}/src/util/BookmarkUtil.cpp
)

target_include_directories(SyncExecutorTest PRIVATE
  ${REPO_ROOT}/test/git_sync_tree_parser/stubs
  ${REPO_ROOT}/lib/BookCachePath
  ${REPO_ROOT}/lib/GitSync
  ${REPO_ROOT}/src/util
)

target_link_libraries(SyncExecutorTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(SyncExecutorTest)
