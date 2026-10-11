add_executable(ProtectedPathTest
  ProtectedPathTest.cpp
  ${REPO_ROOT}/src/util/ProtectedPath.cpp
)

target_include_directories(ProtectedPathTest PRIVATE
  ${REPO_ROOT}/src/util
)

target_link_libraries(ProtectedPathTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ProtectedPathTest)
