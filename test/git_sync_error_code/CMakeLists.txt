add_executable(GitSyncErrorCodeTest
  GitSyncErrorCodeTest.cpp
  ${REPO_ROOT}/src/util/GitSyncErrorCode.cpp
)

target_include_directories(GitSyncErrorCodeTest PRIVATE
  ${REPO_ROOT}/src/util
)

target_link_libraries(GitSyncErrorCodeTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(GitSyncErrorCodeTest)
