add_executable(GitSyncConfigTest
  GitSyncConfigTest.cpp
  ${REPO_ROOT}/lib/GitSync/GitSyncConfig.cpp
  ${REPO_ROOT}/lib/JsonParser/StreamingJsonParser.cpp
)

target_include_directories(GitSyncConfigTest PRIVATE
  ${REPO_ROOT}/lib/GitSync
  ${REPO_ROOT}/lib/JsonParser
  ${REPO_ROOT}/lib/Memory
)

target_link_libraries(GitSyncConfigTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(GitSyncConfigTest)
