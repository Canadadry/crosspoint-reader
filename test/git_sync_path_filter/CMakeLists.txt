add_executable(SyncPathTest
  SyncPathTest.cpp
  ${REPO_ROOT}/lib/GitSync/SyncPath.cpp
)

target_include_directories(SyncPathTest PRIVATE
  ${REPO_ROOT}/lib/GitSync
  ${REPO_ROOT}/lib/JsonParser
)

target_link_libraries(SyncPathTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(SyncPathTest)
