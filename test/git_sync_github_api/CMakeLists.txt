add_executable(GitHubApiTest
  GitHubApiTest.cpp
  ${REPO_ROOT}/lib/GitSync/GitHubApi.cpp
  ${REPO_ROOT}/lib/GitSync/GitBlobHasher.cpp
)

target_include_directories(GitHubApiTest PRIVATE
  ${REPO_ROOT}/lib/GitSync
)

target_link_libraries(GitHubApiTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(GitHubApiTest)
