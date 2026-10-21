add_executable(GitTreeParserTest
  GitTreeParserTest.cpp
  ${REPO_ROOT}/lib/GitSync/GitTreeParser.cpp
  ${REPO_ROOT}/lib/GitSync/GitBlobHasher.cpp
  ${REPO_ROOT}/lib/JsonParser/StreamingJsonParser.cpp
)

target_include_directories(GitTreeParserTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/lib/GitSync
  ${REPO_ROOT}/lib/JsonParser
)

target_link_libraries(GitTreeParserTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(GitTreeParserTest)
