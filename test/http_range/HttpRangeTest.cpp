#include <HttpRange.h>
#include <gtest/gtest.h>

TEST(HttpRange, HeaderAsksForEverythingFromTheStart) {
  char header[HTTP_RANGE_HEADER_CAPACITY];
  ASSERT_TRUE(formatRangeHeader(header, sizeof(header), 300));
  EXPECT_STREQ(header, "bytes=300-");
}

TEST(HttpRange, LargestOffsetFitsTheCapacity) {
  char header[HTTP_RANGE_HEADER_CAPACITY];
  ASSERT_TRUE(formatRangeHeader(header, sizeof(header), UINT64_MAX));
  EXPECT_STREQ(header, "bytes=18446744073709551615-");
}

TEST(HttpRange, HeaderThatDoesNotFitIsRefusedEmpty) {
  char header[8] = "xxxxxxx";
  EXPECT_FALSE(formatRangeHeader(header, sizeof(header), 300));
  EXPECT_STREQ(header, "");
}

TEST(HttpRange, ContentRangeMustStartExactlyAtTheAskedByte) {
  EXPECT_TRUE(contentRangeStartsAt("bytes 300-999/1000", 300));
  EXPECT_TRUE(contentRangeStartsAt("bytes 300-999/*", 300));
  EXPECT_FALSE(contentRangeStartsAt("bytes 0-999/1000", 300));
  EXPECT_FALSE(contentRangeStartsAt("bytes 3000-3999/4000", 300));
  EXPECT_FALSE(contentRangeStartsAt("bytes */1000", 300));
  EXPECT_FALSE(contentRangeStartsAt("items 300-999/1000", 300));
  EXPECT_FALSE(contentRangeStartsAt("bytes 300", 300));
  EXPECT_FALSE(contentRangeStartsAt("bytes -300-999/1000", 300));
  EXPECT_FALSE(contentRangeStartsAt("", 300));
  EXPECT_FALSE(contentRangeStartsAt(nullptr, 300));
}
