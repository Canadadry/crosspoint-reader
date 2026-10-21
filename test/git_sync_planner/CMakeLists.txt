add_executable(SyncPlannerTest
  SyncPlannerTest.cpp
  ${REPO_ROOT}/lib/GitSync/Crc32.cpp
  ${REPO_ROOT}/lib/GitSync/RecordReader.cpp
  ${REPO_ROOT}/lib/GitSync/RecordWriter.cpp
  ${REPO_ROOT}/lib/GitSync/SyncPath.cpp
  ${REPO_ROOT}/lib/GitSync/SyncPathFold.cpp
  ${REPO_ROOT}/lib/GitSync/SyncPlanner.cpp
)

target_include_directories(SyncPlannerTest PRIVATE
  ${REPO_ROOT}/lib/GitSync
)

target_link_libraries(SyncPlannerTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(SyncPlannerTest)
