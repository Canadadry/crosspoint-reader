#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include "GitTreeParser.h"
#include "TreeFixture.h"

namespace {

constexpr const char* TREE_SHA_HEX = "9fb037999f264ba9a7fc6274d15fa3ae2ab98312";

std::string shaHex(const uint8_t sha[GitBlobHasher::SHA_SIZE]) {
  char hex[SHA_HEX_LENGTH + 1];
  shaToHex(sha, hex);
  return hex;
}

struct Rec {
  std::string path;
  std::string sha;
  uint64_t size;

  bool operator==(const Rec&) const = default;
};

std::ostream& operator<<(std::ostream& os, const Rec& r) {
  return os << "{" << r.path << ", " << r.sha << ", " << r.size << "}";
}

bool collectRecord(void* ctx, const GitTreeRecord& record) {
  auto* records = static_cast<std::vector<Rec>*>(ctx);
  EXPECT_EQ(record.path[record.pathLen], '\0');
  records->push_back({std::string(record.path, record.pathLen), shaHex(record.sha), record.size});
  return true;
}

struct Harness {
  std::vector<Rec> records;
  GitTreeParser parser{&records, collectRecord};

  void feed(const std::string& body) { parser.feed(body.data(), body.size()); }
};

std::vector<Rec> fixtureRecords() {
  std::vector<Rec> records;
  for (const auto& blob : TREE_FIXTURE_BLOBS) records.push_back({blob.path, blob.shaHex, blob.size});
  return records;
}

void expectFixtureResult(Harness& h) {
  EXPECT_TRUE(h.parser.isComplete());
  EXPECT_FALSE(h.parser.isTruncated());
  EXPECT_FALSE(h.parser.hasError());
  EXPECT_FALSE(h.parser.wasAborted());
  EXPECT_EQ(shaHex(h.parser.treeSha()), TREE_FIXTURE_SHA_HEX);
  EXPECT_EQ(h.records, fixtureRecords());
}

// Indents like a pretty-printing JSON encoder; the fixture's strings hold no quote or brace.
std::string prettyPrinted(const std::string& minified) {
  std::string out;
  int indent = 0;
  bool inString = false;
  for (char c : minified) {
    if (c == '"') inString = !inString;
    if (inString || c == '"') {
      out += c;
      continue;
    }
    if (c == '}' || c == ']') out += "\n" + std::string(2 * --indent, ' ');
    out += c;
    if (c == ':') out += ' ';
    if (c == '{' || c == '[') out += "\n" + std::string(2 * ++indent, ' ');
    if (c == ',') out += "\n" + std::string(2 * indent, ' ');
  }
  return out + "\n";
}

std::string bodyWithEntries(const std::string& entries) {
  return std::string(R"({"sha":")") + TREE_SHA_HEX + R"(","tree":[)" + entries + R"(],"truncated":false})";
}

constexpr const char* README_SHA_HEX = "4f9fafdb6017bcf0a8f4eb0bf9b7475e447afa73";

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
  EXPECT_TRUE(h.records.empty());
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
  EXPECT_TRUE(h.records.empty());
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

TEST(GitTreeParser, BlobEntryIsEmittedAsARecord) {
  Harness h;
  h.feed(std::string(R"({"sha":")") + TREE_SHA_HEX +
         R"(","tree":[{"path":"README.md","mode":"100644","type":"blob",)"
         R"("sha":"4f9fafdb6017bcf0a8f4eb0bf9b7475e447afa73","size":8,"url":"u"}],"truncated":false})");

  EXPECT_TRUE(h.parser.isComplete());
  const std::vector<Rec> expected = {{"README.md", "4f9fafdb6017bcf0a8f4eb0bf9b7475e447afa73", 8}};
  EXPECT_EQ(h.records, expected);
}

TEST(GitTreeParser, FixtureBlobShasAreTheGitHashesOfTheirContents) {
  for (const auto& blob : TREE_FIXTURE_BLOBS) {
    const size_t len = strlen(blob.content);
    ASSERT_EQ(len, blob.size) << blob.path;
    GitBlobHasher hasher;
    hasher.begin(len);
    hasher.update(reinterpret_cast<const uint8_t*>(blob.content), len);
    uint8_t sha[GitBlobHasher::SHA_SIZE];
    ASSERT_TRUE(hasher.finish(sha));
    EXPECT_EQ(shaHex(sha), blob.shaHex) << blob.path;
  }
}

TEST(GitTreeParser, FixtureFedWholeYieldsItsBlobs) {
  Harness h;
  h.feed(TREE_FIXTURE_BODY);
  expectFixtureResult(h);
}

TEST(GitTreeParser, FixtureFedByteByByteYieldsItsBlobs) {
  const std::string body = TREE_FIXTURE_BODY;
  Harness h;
  for (char c : body) h.parser.feed(&c, 1);
  expectFixtureResult(h);
}

TEST(GitTreeParser, FixtureFedInRandomChunksYieldsItsBlobs) {
  const std::string body = TREE_FIXTURE_BODY;
  for (const uint32_t seed : {1u, 7u, 42u, 1234u, 99991u}) {
    SCOPED_TRACE("seed " + std::to_string(seed));
    std::mt19937 rng(seed);
    std::uniform_int_distribution<size_t> chunkSize(1, 64);
    Harness h;
    for (size_t pos = 0; pos < body.size();) {
      const size_t len = std::min(chunkSize(rng), body.size() - pos);
      h.parser.feed(body.data() + pos, len);
      pos += len;
    }
    expectFixtureResult(h);
  }
}

TEST(GitTreeParser, CallbackReturningFalseStopsTheParser) {
  struct Stopper {
    int calls = 0;
  } stopper;
  GitTreeParser parser(&stopper,
                       [](void* ctx, const GitTreeRecord&) { return ++static_cast<Stopper*>(ctx)->calls < 2; });
  const std::string body = TREE_FIXTURE_BODY;
  parser.feed(body.data(), body.size() / 2);
  parser.feed(body.data() + body.size() / 2, body.size() - body.size() / 2);

  EXPECT_EQ(stopper.calls, 2);
  EXPECT_TRUE(parser.wasAborted());
  EXPECT_FALSE(parser.isComplete());
}

TEST(GitTreeParser, EntryKeyOrderDoesNotMatter) {
  Harness h;
  h.feed(bodyWithEntries(R"({"size":8,"sha":"4f9fafdb6017bcf0a8f4eb0bf9b7475e447afa73","url":"u",)"
                         R"("path":"README.md","type":"blob","mode":"100644"})"));

  EXPECT_TRUE(h.parser.isComplete());
  const std::vector<Rec> expected = {{"README.md", README_SHA_HEX, 8}};
  EXPECT_EQ(h.records, expected);
}

TEST(GitTreeParser, UnknownKeysAndNestedValuesInsideAnEntryAreIgnored) {
  Harness h;
  h.feed(bodyWithEntries(
      R"({"path":"README.md","extra":{"path":"x","type":"tree","sha":"0","size":-1,"deep":[{"size":2}]},)"
      R"("list":["path",{"type":"commit"},[1,null,true]],"type":"blob","n":null,"b":false,"f":1.5,)"
      R"("sha":"4f9fafdb6017bcf0a8f4eb0bf9b7475e447afa73","size":8,"mode":"100644"})"));

  EXPECT_TRUE(h.parser.isComplete());
  const std::vector<Rec> expected = {{"README.md", README_SHA_HEX, 8}};
  EXPECT_EQ(h.records, expected);
}

TEST(GitTreeParser, MalformedEntryIsAResponseError) {
  const std::string sha = R"("sha":"4f9fafdb6017bcf0a8f4eb0bf9b7475e447afa73")";
  const std::string entries[] = {
      R"({"path":"README.md","type":"blob","size":8})",
      R"({"path":"README.md","type":"blob","sha":"4f9fafdb6017bcf0a8f4eb0bf9b7475e447afa7","size":8})",
      R"({"path":"README.md","type":"blob","sha":"4f9fafdb6017bcf0a8f4eb0bf9b7475e447afa7g","size":8})",
      R"({"path":"README.md","type":"blob","sha":null,"size":8})",
      R"({"path":"README.md","type":"blob",)" + sha + "}",
      R"({"path":"README.md","type":"blob",)" + sha + R"(,"size":-8})",
      R"({"path":"README.md","type":"blob",)" + sha + R"(,"size":8.5})",
      R"({"path":"README.md","type":"blob",)" + sha + R"(,"size":8e2})",
      R"({"path":"README.md","type":"blob",)" + sha + R"(,"size":"8"})",
      R"({"path":"README.md","type":"blob",)" + sha + R"(,"size":null})",
      R"({"type":"blob",)" + sha + R"(,"size":8})",
      R"({"path":12,"type":"blob",)" + sha + R"(,"size":8})",
      R"({"path":["README.md"],"type":"blob",)" + sha + R"(,"size":8})",
      R"({"path":"README.md",)" + sha + R"(,"size":8})",
      R"({"path":"README.md","type":"symlink",)" + sha + R"(,"size":8})",
      R"({"path":"README.md","type":1,)" + sha + R"(,"size":8})",
      R"({"path":"README.md","type":{"name":"blob"},)" + sha + R"(,"size":8})",
  };
  for (const auto& entry : entries) {
    Harness h;
    h.feed(bodyWithEntries(entry));
    EXPECT_TRUE(h.parser.hasError()) << entry;
    EXPECT_FALSE(h.parser.isComplete()) << entry;
    EXPECT_TRUE(h.records.empty()) << entry;
  }
}

TEST(GitTreeParser, TreeAndCommitEntriesAreSkippedWithoutValidation) {
  Harness h;
  h.feed(bodyWithEntries(R"({"path":"Livres","mode":"040000","type":"tree"},)"
                         R"({"type":"commit","sha":"not-a-sha","size":-1,"path":7},)"
                         R"({"type":"tree","path":{"x":1},"size":"big","sha":null})"));

  EXPECT_TRUE(h.parser.isComplete());
  EXPECT_FALSE(h.parser.hasError());
  EXPECT_TRUE(h.records.empty());
}

TEST(GitTreeParser, NonObjectElementInTreeIsAnError) {
  const std::string elements[] = {R"("README.md")", "1", "true", "null", "[]", R"([{"type":"tree"}])"};
  for (const auto& element : elements) {
    Harness h;
    h.feed(bodyWithEntries(element));
    EXPECT_TRUE(h.parser.hasError()) << element;
    EXPECT_FALSE(h.parser.isComplete()) << element;
  }
}

TEST(GitTreeParser, SizeIsParsedExactlyUpToUint64Max) {
  const std::pair<const char*, uint64_t> sizes[] = {
      {"0", 0}, {"5000000000", 5000000000ULL}, {"18446744073709551615", UINT64_MAX}};
  for (const auto& [text, value] : sizes) {
    Harness h;
    h.feed(bodyWithEntries(std::string(R"({"path":"a.epub","type":"blob","sha":")") + README_SHA_HEX + R"(","size":)" +
                           text + "}"));
    EXPECT_TRUE(h.parser.isComplete()) << text;
    const std::vector<Rec> expected = {{"a.epub", README_SHA_HEX, value}};
    EXPECT_EQ(h.records, expected) << text;
  }
}

TEST(GitTreeParser, SizeAboveUint64MaxIsAnError) {
  for (const char* text : {"18446744073709551616", "99999999999999999999"}) {
    Harness h;
    h.feed(bodyWithEntries(std::string(R"({"path":"a.epub","type":"blob","sha":")") + README_SHA_HEX + R"(","size":)" +
                           text + "}"));
    EXPECT_TRUE(h.parser.hasError()) << text;
    EXPECT_TRUE(h.records.empty()) << text;
  }
}

TEST(GitTreeParser, PrettyPrintedFixtureYieldsTheSameResult) {
  Harness h;
  h.feed(prettyPrinted(TREE_FIXTURE_BODY));
  expectFixtureResult(h);
}

TEST(GitTreeParser, TruncatedFixtureEmitsTheSameRecords) {
  std::string body = TREE_FIXTURE_BODY;
  const std::string flag = R"("truncated":false)";
  body.replace(body.find(flag), flag.size(), R"("truncated":true)");
  Harness h;
  h.feed(body);

  EXPECT_TRUE(h.parser.isComplete());
  EXPECT_TRUE(h.parser.isTruncated());
  EXPECT_EQ(h.records, fixtureRecords());
}

TEST(GitTreeParser, FixtureCutBeforeItsEndIsNeverComplete) {
  const std::string body = TREE_FIXTURE_BODY;
  for (size_t cut = 0; cut < body.size(); cut++) {
    Harness h;
    h.parser.feed(body.data(), cut);
    EXPECT_FALSE(h.parser.isComplete()) << "cut at " << cut;
  }
}

TEST(GitTreeParser, ResetAfterAFixtureParseOrAnEntryErrorGivesTheFixtureResult) {
  const std::string firstBodies[] = {
      TREE_FIXTURE_BODY,
      bodyWithEntries(R"({"path":"README.md","type":"blob","size":8})"),
      std::string(TREE_FIXTURE_BODY).substr(0, 700),
  };
  for (const auto& first : firstBodies) {
    Harness h;
    h.feed(first);
    h.parser.reset();
    h.records.clear();
    h.feed(TREE_FIXTURE_BODY);
    expectFixtureResult(h);
  }
}
