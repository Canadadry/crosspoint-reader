add_executable(SyncPathFoldTest
  SyncPathFoldTest.cpp
  ${REPO_ROOT}/lib/GitSync/SyncPathFold.cpp
)

target_include_directories(SyncPathFoldTest PRIVATE
  ${REPO_ROOT}/lib/GitSync
)

target_link_libraries(SyncPathFoldTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(SyncPathFoldTest)
