add_executable(HttpRangeTest
  HttpRangeTest.cpp
  ${REPO_ROOT}/src/util/HttpRange.cpp
)

target_include_directories(HttpRangeTest PRIVATE
  ${REPO_ROOT}/src/util
)

target_link_libraries(HttpRangeTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(HttpRangeTest)
