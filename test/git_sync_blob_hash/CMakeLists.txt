add_executable(GitBlobHasherTest
  GitBlobHasherTest.cpp
  ${REPO_ROOT}/lib/GitSync/GitBlobHasher.cpp
)

target_include_directories(GitBlobHasherTest PRIVATE
  ${REPO_ROOT}/lib/GitSync
)

target_link_libraries(GitBlobHasherTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(GitBlobHasherTest)
