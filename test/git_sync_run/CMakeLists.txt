add_executable(GitSyncRunTest
  GitSyncRunTest.cpp
  ${REPO_ROOT}/lib/GitSync/Crc32.cpp
  ${REPO_ROOT}/lib/GitSync/GitBlobHasher.cpp
  ${REPO_ROOT}/lib/GitSync/GitBlobSourceHash.cpp
  ${REPO_ROOT}/lib/GitSync/GitSyncRun.cpp
  ${REPO_ROOT}/lib/GitSync/GitTreeParser.cpp
  ${REPO_ROOT}/lib/GitSync/ManifestCompactor.cpp
  ${REPO_ROOT}/lib/GitSync/RecordReader.cpp
  ${REPO_ROOT}/lib/GitSync/RecordWriter.cpp
  ${REPO_ROOT}/lib/GitSync/SyncExecutor.cpp
  ${REPO_ROOT}/lib/GitSync/SyncPath.cpp
  ${REPO_ROOT}/lib/GitSync/SyncPathFold.cpp
  ${REPO_ROOT}/lib/GitSync/SyncPlanner.cpp
  ${REPO_ROOT}/lib/GitSync/TreeSnapshotWriter.cpp
  ${REPO_ROOT}/lib/GitSync/BookmarkPath.cpp
  ${REPO_ROOT}/lib/JsonParser/StreamingJsonParser.cpp
)

target_include_directories(GitSyncRunTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/lib/BookCachePath
  ${REPO_ROOT}/lib/GitSync
  ${REPO_ROOT}/lib/JsonParser
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/src/util
  ${REPO_ROOT}/test/git_sync_executor
  ${REPO_ROOT}/test/git_sync_tree_parser
)

target_link_libraries(GitSyncRunTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(GitSyncRunTest)
