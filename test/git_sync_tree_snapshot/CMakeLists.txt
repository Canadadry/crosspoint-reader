add_executable(TreeSnapshotWriterTest
  TreeSnapshotWriterTest.cpp
  ${REPO_ROOT}/lib/GitSync/Crc32.cpp
  ${REPO_ROOT}/lib/GitSync/GitBlobHasher.cpp
  ${REPO_ROOT}/lib/GitSync/GitTreeParser.cpp
  ${REPO_ROOT}/lib/GitSync/RecordReader.cpp
  ${REPO_ROOT}/lib/GitSync/RecordWriter.cpp
  ${REPO_ROOT}/lib/GitSync/SyncPath.cpp
  ${REPO_ROOT}/lib/GitSync/TreeSnapshotWriter.cpp
  ${REPO_ROOT}/lib/JsonParser/StreamingJsonParser.cpp
)

target_include_directories(TreeSnapshotWriterTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/lib/GitSync
  ${REPO_ROOT}/lib/JsonParser
  ${REPO_ROOT}/test/git_sync_tree_parser
)

target_link_libraries(TreeSnapshotWriterTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(TreeSnapshotWriterTest)
