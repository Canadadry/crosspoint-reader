add_executable(HomeMenuTest
  HomeMenuTest.cpp
)

target_include_directories(HomeMenuTest PRIVATE
  ${REPO_ROOT}/src/activities/home
)

target_link_libraries(HomeMenuTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(HomeMenuTest)
