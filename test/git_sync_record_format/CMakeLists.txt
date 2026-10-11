add_executable(RecordFormatTest
  RecordFormatTest.cpp
  ${REPO_ROOT}/lib/GitSync/Crc32.cpp
  ${REPO_ROOT}/lib/GitSync/RecordReader.cpp
  ${REPO_ROOT}/lib/GitSync/RecordWriter.cpp
)

target_include_directories(RecordFormatTest PRIVATE
  ${REPO_ROOT}/lib/GitSync
)

target_link_libraries(RecordFormatTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(RecordFormatTest)
