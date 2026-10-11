#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "GitTreeParser.h"

namespace {

constexpr const char* TREE_SHA_HEX = "9fb037999f264ba9a7fc6274d15fa3ae2ab98312";

std::string shaHex(const uint8_t sha[GitBlobHasher::SHA_SIZE]) {
  char hex[SHA_HEX_LENGTH + 1];
  shaToHex(sha, hex);
  return hex;
}

bool countRecord(void* ctx, const GitTreeRecord&) {
  ++*static_cast<int*>(ctx);
  return true;
}

struct Harness {
  int records = 0;
  GitTreeParser parser{&records, countRecord};

  void feed(const std::string& body) { parser.feed(body.data(), body.size()); }
};

std::string emptyTreeBody(const char* truncated = "false") {
  return std::string(R"({"sha":")") + TREE_SHA_HEX +
         R"(","url":"https://api.github.com/repos/o/r/git/trees/9fb037999f264ba9a7fc6274d15fa3ae2ab98312","tree":[],"truncated":)" +
         truncated + "}";
}

}  // namespace

TEST(GitTreeParser, EmptyTreeResponseIsComplete) {
  Harness h;
  h.feed(emptyTreeBody());

  EXPECT_TRUE(h.parser.isComplete());
  EXPECT_FALSE(h.parser.isTruncated());
  EXPECT_FALSE(h.parser.hasError());
  EXPECT_EQ(shaHex(h.parser.treeSha()), TREE_SHA_HEX);
  EXPECT_EQ(h.records, 0);
}

TEST(GitTreeParser, TruncatedFlagIsReportedOnACompleteResponse) {
  Harness h;
  h.feed(emptyTreeBody("true"));

  EXPECT_TRUE(h.parser.isComplete());
  EXPECT_TRUE(h.parser.isTruncated());
  EXPECT_FALSE(h.parser.hasError());
}

TEST(GitTreeParser, ResponseMissingAFieldIsNotComplete) {
  const std::string sha = std::string(R"("sha":")") + TREE_SHA_HEX + R"(")";
  const std::string bodies[] = {
      "{" + std::string(R"("tree":[],"truncated":false)") + "}",
      "{" + sha + R"(,"truncated":false})",
      "{" + sha + R"(,"tree":[]})",
  };
  for (const auto& body : bodies) {
    Harness h;
    h.feed(body);
    EXPECT_FALSE(h.parser.isComplete()) << body;
    EXPECT_FALSE(h.parser.hasError()) << body;
  }
}

TEST(GitTreeParser, BadTopLevelShaIsAnError) {
  const std::string tail = R"(,"tree":[],"truncated":false})";
  const std::string bodies[] = {
      R"({"sha":"9fb037999f264ba9a7fc6274d15fa3ae2ab9831")" + tail,
      R"({"sha":"9fb037999f264ba9a7fc6274d15fa3ae2ab9831z")" + tail,
      R"({"sha":12)" + tail,
      R"({"sha":null)" + tail,
      R"({"sha":{})" + tail,
  };
  for (const auto& body : bodies) {
    Harness h;
    h.feed(body);
    EXPECT_TRUE(h.parser.hasError()) << body;
    EXPECT_FALSE(h.parser.isComplete()) << body;
  }
}

TEST(GitTreeParser, WrongTypeForTreeOrTruncatedIsAnError) {
  const std::string sha = std::string(R"({"sha":")") + TREE_SHA_HEX + R"(",)";
  const std::string bodies[] = {
      sha + R"("tree":{},"truncated":false})",   sha + R"("tree":"x","truncated":false})",
      sha + R"("tree":3,"truncated":false})",    sha + R"("tree":null,"truncated":false})",
      sha + R"("tree":true,"truncated":false})", sha + R"("tree":[],"truncated":"false"})",
      sha + R"("tree":[],"truncated":0})",       sha + R"("tree":[],"truncated":null})",
      sha + R"("tree":[],"truncated":[]})",
  };
  for (const auto& body : bodies) {
    Harness h;
    h.feed(body);
    EXPECT_TRUE(h.parser.hasError()) << body;
    EXPECT_FALSE(h.parser.isComplete()) << body;
  }
}

TEST(GitTreeParser, JsonSyntaxErrorIsAnError) {
  Harness h;
  h.feed(std::string(R"({"sha":")") + TREE_SHA_HEX + R"(","tree":[],"truncated":fasle})");

  EXPECT_TRUE(h.parser.hasError());
  EXPECT_FALSE(h.parser.isComplete());
}

TEST(GitTreeParser, BytesAfterTheTopLevelObjectAreIgnored) {
  const std::string trailers[] = {
      "\n",
      R"({"sha":"0000000000000000000000000000000000000000","tree":[],"truncated":true})",
      "[fasle]",
  };
  for (const auto& trailer : trailers) {
    Harness h;
    h.feed(emptyTreeBody() + trailer);
    EXPECT_TRUE(h.parser.isComplete()) << trailer;
    EXPECT_FALSE(h.parser.isTruncated()) << trailer;
    EXPECT_FALSE(h.parser.hasError()) << trailer;
    EXPECT_EQ(shaHex(h.parser.treeSha()), TREE_SHA_HEX) << trailer;
  }
}

TEST(GitTreeParser, KeysOutsideTheTopLevelAreIgnored) {
  Harness h;
  h.feed(std::string(R"({"sha":")") + TREE_SHA_HEX +
         R"(","meta":{"sha":"bad","truncated":true,"tree":{}},"list":[{"truncated":1},[["sha"]]],)"
         R"("tree":[{"path":"Livres","mode":"040000","type":"tree",)"
         R"("sha":"0000000000000000000000000000000000000000","url":"u","truncated":true}],"truncated":false})");

  EXPECT_TRUE(h.parser.isComplete());
  EXPECT_FALSE(h.parser.isTruncated());
  EXPECT_EQ(shaHex(h.parser.treeSha()), TREE_SHA_HEX);
  EXPECT_EQ(h.records, 0);
}

TEST(GitTreeParser, ValueOfADroppedOverLongKeyChangesNoField) {
  const std::string longKey(600, 'k');
  Harness h;
  h.feed(std::string(R"({"sha":")") + TREE_SHA_HEX + R"(",")" + longKey +
         R"(":"0000000000000000000000000000000000000000","truncated":false,")" + longKey + R"(":true,"tree":[],")" +
         longKey + R"(":[1]})");

  EXPECT_TRUE(h.parser.isComplete());
  EXPECT_FALSE(h.parser.isTruncated());
  EXPECT_EQ(shaHex(h.parser.treeSha()), TREE_SHA_HEX);
}

TEST(GitTreeParser, ByteByByteFeedingGivesTheSameResult) {
  const std::string body = emptyTreeBody("true");
  Harness h;
  for (char c : body) h.parser.feed(&c, 1);

  EXPECT_TRUE(h.parser.isComplete());
  EXPECT_TRUE(h.parser.isTruncated());
  EXPECT_EQ(shaHex(h.parser.treeSha()), TREE_SHA_HEX);
}

TEST(GitTreeParser, BodyCutBeforeTheTopLevelObjectClosesIsNeverComplete) {
  const std::string body = emptyTreeBody();
  for (size_t cut = 0; cut < body.size(); cut++) {
    Harness h;
    h.parser.feed(body.data(), cut);
    EXPECT_FALSE(h.parser.isComplete()) << "cut at " << cut;
  }
}

TEST(GitTreeParser, ResetMakesTheParserReusable) {
  const std::string firstBodies[] = {
      emptyTreeBody("true"),
      R"({"sha":"bad","tree":[],"truncated":false})",
      R"({"tree":[],"truncated":fasle})",
  };
  for (const auto& first : firstBodies) {
    Harness h;
    h.feed(first);
    h.parser.reset();
    h.feed(emptyTreeBody());
    EXPECT_TRUE(h.parser.isComplete()) << first;
    EXPECT_FALSE(h.parser.isTruncated()) << first;
    EXPECT_FALSE(h.parser.hasError()) << first;
    EXPECT_EQ(shaHex(h.parser.treeSha()), TREE_SHA_HEX) << first;
  }
}
