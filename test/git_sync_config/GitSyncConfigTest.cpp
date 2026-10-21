#include <gtest/gtest.h>

#include <cstring>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "GitSyncConfig.h"

namespace {

constexpr const char* FINE_GRAINED_TOKEN =
    "github_pat_11ABCDEFG0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789a";

struct Parsed {
  GitSyncConfigResult result;
  std::unique_ptr<GitSyncConfig> config;
};

constexpr const char* VALID = R"({"owner":"jerome","repo":"books","branch":"main","token":"ghp_x"})";

Parsed parse(const std::string& json) {
  Parsed parsed{{}, std::make_unique<GitSyncConfig>()};
  parsed.result = parseGitSyncConfig(json.data(), json.size(), *parsed.config);
  return parsed;
}

void expectOk(const Parsed& parsed, const std::string& owner, const std::string& repo, const std::string& branch,
              const std::string& token) {
  ASSERT_EQ(parsed.result.status, GitSyncConfigStatus::Ok);
  EXPECT_EQ(parsed.result.field, GitSyncConfigField::None);
  EXPECT_EQ(std::string(parsed.config->owner), owner);
  EXPECT_EQ(std::string(parsed.config->repo), repo);
  EXPECT_EQ(std::string(parsed.config->branch), branch);
  EXPECT_EQ(std::string(parsed.config->token), token);
}

// Starts from a sentinel-filled config so a refusal must zero every byte.
void expectRefused(const std::string& json, GitSyncConfigStatus status,
                   GitSyncConfigField field = GitSyncConfigField::None) {
  auto config = std::make_unique<GitSyncConfig>();
  memset(config.get(), 0xA5, sizeof(GitSyncConfig));
  const GitSyncConfigResult result = parseGitSyncConfig(json.data(), json.size(), *config);
  EXPECT_EQ(result.status, status) << json;
  EXPECT_EQ(result.field, field) << json;
  const auto* bytes = reinterpret_cast<const unsigned char*>(config.get());
  for (size_t i = 0; i < sizeof(GitSyncConfig); i++) {
    ASSERT_EQ(bytes[i], 0) << "byte " << i << " not zeroed for " << json;
  }
}

void expectMalformed(const std::string& json) { expectRefused(json, GitSyncConfigStatus::Malformed); }

void expectMissing(const std::string& json, GitSyncConfigField field) {
  expectRefused(json, GitSyncConfigStatus::MissingField, field);
}

void expectInvalid(const std::string& json, GitSyncConfigField field) {
  expectRefused(json, GitSyncConfigStatus::InvalidField, field);
}

// VALID with the value of one key replaced by raw JSON text.
std::string withRaw(const char* key, const std::string& raw) {
  std::string owner = R"("jerome")", repo = R"("books")", branch = R"("main")", token = R"("ghp_x")";
  if (strcmp(key, "owner") == 0) owner = raw;
  if (strcmp(key, "repo") == 0) repo = raw;
  if (strcmp(key, "branch") == 0) branch = raw;
  if (strcmp(key, "token") == 0) token = raw;
  return "{\"owner\":" + owner + ",\"repo\":" + repo + ",\"branch\":" + branch + ",\"token\":" + token + "}";
}

// VALID with one key's string value replaced; value is raw JSON string content (escapes kept).
std::string with(const char* key, const std::string& value) { return withRaw(key, "\"" + value + "\""); }

struct KeyField {
  const char* key;
  GitSyncConfigField field;
};

constexpr KeyField KEYS[] = {{"owner", GitSyncConfigField::Owner},
                             {"repo", GitSyncConfigField::Repo},
                             {"branch", GitSyncConfigField::Branch},
                             {"token", GitSyncConfigField::Token}};

}  // namespace

TEST(GitSyncConfig, PlanExampleWithFineGrainedTokenIsOk) {
  ASSERT_EQ(std::string(FINE_GRAINED_TOKEN).size(), 93u);
  const std::string json = std::string(R"({ "owner": "jerome", "repo": "books", "branch": "main", "token": ")") +
                           FINE_GRAINED_TOKEN + "\" }";
  expectOk(parse(json), "jerome", "books", "main", FINE_GRAINED_TOKEN);
}

TEST(GitSyncConfig, AbsentBranchDefaultsToMain) {
  expectOk(parse(R"({"owner":"jerome","repo":"books","token":"ghp_x"})"), "jerome", "books", "main", "ghp_x");
}

TEST(GitSyncConfig, EachRequiredFieldAbsentIsMissing) {
  expectMissing(R"({"repo":"books","branch":"main","token":"t"})", GitSyncConfigField::Owner);
  expectMissing(R"({"owner":"jerome","branch":"main","token":"t"})", GitSyncConfigField::Repo);
  expectMissing(R"({"owner":"jerome","repo":"books","branch":"main"})", GitSyncConfigField::Token);
}

TEST(GitSyncConfig, MissingFieldsAreReportedInOwnerRepoTokenOrder) {
  expectMissing(R"({"branch":"main"})", GitSyncConfigField::Owner);
  expectMissing(R"({"token":"t"})", GitSyncConfigField::Owner);
  expectMissing(R"({"owner":"jerome"})", GitSyncConfigField::Repo);
  expectMissing("{}", GitSyncConfigField::Owner);
}

TEST(GitSyncConfig, InputThatIsNotAnObjectIsMalformed) {
  expectMalformed("");
  expectMalformed(" \t\r\n ");
  expectMalformed("hello");
  expectMalformed(R"("hello")");
  expectMalformed("[]");
  expectMalformed(R"([{"owner":"a","repo":"b","token":"c"}])");
}

TEST(GitSyncConfig, EveryPrefixCutBeforeTheClosingBraceIsMalformed) {
  const std::string json = std::string(R"({ "owner" : "jerome", "repo": "books", "branch": "main", "token": ")") +
                           FINE_GRAINED_TOKEN + "\", \"n\": 12, \"b\": true }";
  for (size_t cut = 0; cut < json.size() - 1; cut++) {
    expectMalformed(json.substr(0, cut));
  }
}

TEST(GitSyncConfig, AnythingButWhitespaceAfterTheRootObjectIsMalformed) {
  expectMalformed(std::string(VALID) + "x");
  expectMalformed(std::string(VALID) + " \r\n}");
  expectMalformed(std::string(VALID) + VALID);
  expectMalformed(std::string(VALID) + "\n" + VALID);
  expectOk(parse(std::string(" \r\n\t") + VALID + " \r\n\t"), "jerome", "books", "main", "ghp_x");
}

TEST(GitSyncConfig, NestedObjectOrArrayAnywhereIsMalformed) {
  expectMalformed(R"({"owner":"jerome","repo":"books","token":"t","extra":{"a":"b"}})");
  expectMalformed(R"({"owner":"jerome","repo":"books","token":"t","extra":["a"]})");
  expectMalformed(R"({"owner":{"name":"jerome"},"repo":"books","token":"t"})");
  expectMalformed(R"({"owner":"jerome","repo":"books","token":"t","extra":[]})");
}

TEST(GitSyncConfig, StrayClosingBracketIsMalformed) {
  expectMalformed(R"({"owner":"jerome","repo":"books","token":"t"]})");
  expectMalformed(R"({"owner":"jerome","repo":"books"],"token":"t"})");
}

TEST(GitSyncConfig, BrokenLiteralIsMalformed) {
  expectMalformed(R"({"owner":"jerome","repo":"books","token":"t","extra":nul})");
  expectMalformed(R"({"owner":"jerome","repo":"books","token":"t","extra":tru})");
}

TEST(GitSyncConfig, KnownKeyWithNonStringValueIsInvalid) {
  for (const auto& k : KEYS) {
    for (const char* raw : {"12", "-1.5e3", "true", "false", "null"}) {
      expectInvalid(withRaw(k.key, raw), k.field);
    }
  }
}

TEST(GitSyncConfig, KnownKeyWhoseValueNeverArrivesIsInvalid) {
  for (const auto& k : KEYS) {
    expectInvalid(with(k.key, std::string(600, 'a')), k.field);
  }
  // The dropped value is the last one before the root closes.
  expectInvalid(R"({"owner":"jerome","repo":"books","token":")" + std::string(600, 'a') + "\"}",
                GitSyncConfigField::Token);
  expectInvalid(R"({"owner":"jerome","repo","token":"t"})", GitSyncConfigField::Repo);
}

TEST(GitSyncConfig, KnownKeyGivenTwiceIsInvalid) {
  for (const auto& k : KEYS) {
    const std::string base = with(k.key, "a");
    const std::string head = base.substr(0, base.size() - 1);
    expectInvalid(head + ",\"" + k.key + "\":\"a\"}", k.field);
    expectInvalid(head + ",\"" + k.key + "\":\"b\"}", k.field);
  }
}

TEST(GitSyncConfig, EmptyValueIsInvalidForEveryField) {
  for (const auto& k : KEYS) {
    expectInvalid(with(k.key, ""), k.field);
  }
}

TEST(GitSyncConfig, EveryFieldAtItsMaximumLengthIsOk) {
  const std::string owner(GitSyncConfig::OWNER_MAX, 'o');
  const std::string repo(GitSyncConfig::REPO_MAX, 'r');
  const std::string branch(GitSyncConfig::BRANCH_MAX, 'b');
  const std::string token(GitSyncConfig::TOKEN_MAX, 't');
  const std::string json = "{\"owner\":\"" + owner + "\",\"repo\":\"" + repo + "\",\"branch\":\"" + branch +
                           "\",\"token\":\"" + token + "\"}";
  expectOk(parse(json), owner, repo, branch, token);
}

TEST(GitSyncConfig, OneByteOverTheMaximumIsInvalid) {
  expectInvalid(with("owner", std::string(GitSyncConfig::OWNER_MAX + 1, 'o')), GitSyncConfigField::Owner);
  expectInvalid(with("repo", std::string(GitSyncConfig::REPO_MAX + 1, 'r')), GitSyncConfigField::Repo);
  expectInvalid(with("branch", std::string(GitSyncConfig::BRANCH_MAX + 1, 'b')), GitSyncConfigField::Branch);
  expectInvalid(with("token", std::string(GitSyncConfig::TOKEN_MAX + 1, 't')), GitSyncConfigField::Token);
}

TEST(GitSyncConfig, OwnerAcceptsLettersDigitsAndHyphen) {
  expectOk(parse(with("owner", "Jerome-M0urey")), "Jerome-M0urey", "books", "main", "ghp_x");
}

TEST(GitSyncConfig, OwnerOutsideItsAlphabetIsInvalid) {
  for (const char* owner : {"a/b", "a b", "a.b", "a?", "a#", "a%20", "a_b", "\xC3\xA9", "a\\\\b", "a\\nb"}) {
    expectInvalid(with("owner", owner), GitSyncConfigField::Owner);
  }
}

TEST(GitSyncConfig, RepoAcceptsGithubRepositoryNames) {
  for (const char* repo : {"my-books_2026.v1", ".github", "...", "a..b", ".x"}) {
    expectOk(parse(with("repo", repo)), "jerome", repo, "main", "ghp_x");
  }
}

TEST(GitSyncConfig, RepoDotSegmentOrOutsideItsAlphabetIsInvalid) {
  for (const char* repo : {".", "..", "a/b", "a#b", "a%20b", "a b", "a?b", "a\\/b", "\xC3\xA9", "a\\u0041"}) {
    expectInvalid(with("repo", repo), GitSyncConfigField::Repo);
  }
}

TEST(GitSyncConfig, BranchAcceptsSlashesAndGitNameCharacters) {
  for (const char* branch : {"release/2026", "a/b.c/d-e_f", "v1.2", "feature/x.lock-free", "lock"}) {
    expectOk(parse(with("branch", branch)), "jerome", "books", branch, "ghp_x");
  }
}

TEST(GitSyncConfig, EscapedSlashInBranchDecodesToSlash) {
  expectOk(parse(with("branch", "release\\/2026")), "jerome", "books", "release/2026", "ghp_x");
}

TEST(GitSyncConfig, BranchBreakingGitRefRulesOrOutsideItsAlphabetIsInvalid) {
  for (const char* branch : {"/main", "main/", "a//b", "a..b", "a/.b", ".a", "a.lock", "a/b.lock/c", "main.", "a b",
                             "a?b", "a#b", "a%b", "a+b", "a@b", "\xC3\x89t\xC3\xA9", "a\\\\b", "a\\u0041", "a\\tb"}) {
    expectInvalid(with("branch", branch), GitSyncConfigField::Branch);
  }
}

TEST(GitSyncConfig, ClassicTokenIsOk) {
  const std::string token = "ghp_" + std::string(36, 'A');
  expectOk(parse(with("token", token)), "jerome", "books", "main", token);
}

TEST(GitSyncConfig, TokenAcceptsEveryVisibleAsciiByteButBackslash) {
  std::string token;
  for (char c = 0x21; c <= 0x7E; c++) {
    if (c != '"' && c != '\\') token += c;
  }
  expectOk(parse(with("token", token + "\\\"")), "jerome", "books", "main", token + "\"");
}

TEST(GitSyncConfig, TokenWithWhitespaceControlBackslashOrNonAsciiByteIsInvalid) {
  for (const char* token : {"ghp_x ", " ghp_x", "ghp_x\\n", "ghp_x\\t", "ghp_x\\r", "gh p", "ghp_\xC3\xA9", "ghp_\x7F",
                            "ghp_\\\\x", "ghp_\\u0041"}) {
    expectInvalid(with("token", token), GitSyncConfigField::Token);
  }
}

TEST(GitSyncConfig, NotepadFileWithBomCrlfAndIndentationIsOk) {
  const std::string json =
      "\xEF\xBB\xBF{\r\n\t\"owner\": \"jerome\",\r\n    \"repo\" : \"books\",\r\n\t\"branch\":\t\"main\",\r\n"
      "\t\"token\": \"ghp_x\"\r\n}\r\n";
  expectOk(parse(json), "jerome", "books", "main", "ghp_x");
}

TEST(GitSyncConfig, BomAnywhereButTheStartIsMalformed) {
  expectMalformed(std::string(" \xEF\xBB\xBF") + VALID);
  expectMalformed(std::string("\xEF\xBB") + VALID);
}

TEST(GitSyncConfig, KeysInAnyOrderAreOk) {
  expectOk(parse(R"({"token":"ghp_x","branch":"dev","repo":"books","owner":"jerome"})"), "jerome", "books", "dev",
           "ghp_x");
}

TEST(GitSyncConfig, UnknownKeysAreIgnoredWhateverTheirScalarValue) {
  const std::string json =
      R"({"comment":"my books","owner":"jerome","n":-12.5e3,"repo":"books","x":null,"t":true,"f":false,)"
      R"("Owner":"ignored","token":"ghp_x","long":")" +
      std::string(600, 'a') + "\"}";
  expectOk(parse(json), "jerome", "books", "main", "ghp_x");
}

TEST(GitSyncConfig, ReadsExactlyLenBytesWithNoTerminator) {
  std::string buffer = std::string(VALID) + "garbage";
  const auto config = std::make_unique<GitSyncConfig>();
  const GitSyncConfigResult result = parseGitSyncConfig(buffer.data(), strlen(VALID), *config);
  EXPECT_EQ(result.status, GitSyncConfigStatus::Ok);
  EXPECT_STREQ(config->token, "ghp_x");

  // A cut that drops the closing brace is malformed even though the buffer goes on.
  EXPECT_EQ(parseGitSyncConfig(buffer.data(), strlen(VALID) - 1, *config).status, GitSyncConfigStatus::Malformed);
}

TEST(GitSyncConfig, InputOverTheCapIsTooLargeAndAtTheCapIsOk) {
  const std::string valid(VALID);
  const std::string atCap = valid + std::string(GitSyncConfig::MAX_CONFIG_FILE_BYTES - valid.size(), ' ');
  ASSERT_EQ(atCap.size(), 1024u);
  expectOk(parse(atCap), "jerome", "books", "main", "ghp_x");
  expectRefused(atCap + " ", GitSyncConfigStatus::TooLarge);
}

TEST(GitSyncConfig, EveryVerdictHasADistinctNonEmptyDescription) {
  constexpr GitSyncConfigField FIELDS[] = {GitSyncConfigField::Owner, GitSyncConfigField::Repo,
                                           GitSyncConfigField::Branch, GitSyncConfigField::Token};
  std::vector<GitSyncConfigResult> results;
  for (auto status :
       {GitSyncConfigStatus::Ok, GitSyncConfigStatus::NotFound, GitSyncConfigStatus::ReadError,
        GitSyncConfigStatus::TooLarge, GitSyncConfigStatus::Malformed, GitSyncConfigStatus::OutOfMemory}) {
    results.push_back({status, GitSyncConfigField::None});
  }
  for (auto field : FIELDS) {
    results.push_back({GitSyncConfigStatus::MissingField, field});
    results.push_back({GitSyncConfigStatus::InvalidField, field});
  }
  std::set<std::string> seen;
  for (const auto& result : results) {
    const char* text = describeGitSyncConfigResult(result);
    ASSERT_NE(text, nullptr);
    EXPECT_GT(strlen(text), 0u);
    EXPECT_TRUE(seen.insert(text).second) << text;
  }
}

TEST(GitSyncConfig, InvalidFieldDescriptionNamesTheField) {
  EXPECT_NE(std::string(describeGitSyncConfigResult({GitSyncConfigStatus::InvalidField, GitSyncConfigField::Token}))
                .find("token"),
            std::string::npos);
  EXPECT_NE(std::string(describeGitSyncConfigResult({GitSyncConfigStatus::MissingField, GitSyncConfigField::Repo}))
                .find("repo"),
            std::string::npos);
}

TEST(GitSyncConfig, DescriptionNeverHoldsTheToken) {
  const std::string secret = "ghp_SENTINEL";
  // Invalid token, another field invalid, and a malformed file holding the token.
  for (const std::string& json :
       {with("token", secret + " "), with("token", secret).replace(10, 6, "a/b"), with("token", secret) + "x"}) {
    auto config = std::make_unique<GitSyncConfig>();
    const char* text = describeGitSyncConfigResult(parseGitSyncConfig(json.data(), json.size(), *config));
    EXPECT_EQ(std::string(text).find("SENTINEL"), std::string::npos) << text;
  }
}

TEST(GitSyncConfig, UnexpectedVerdictStillHasADescription) {
  const char* text = describeGitSyncConfigResult({static_cast<GitSyncConfigStatus>(200), GitSyncConfigField::None});
  ASSERT_NE(text, nullptr);
  EXPECT_GT(strlen(text), 0u);
}
