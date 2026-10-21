#include <gtest/gtest.h>

#include <string>
#include <string_view>

#include "UrlOrigin.h"

namespace {

void expectOrigin(std::string_view url, bool https, std::string_view host, uint16_t port) {
  SCOPED_TRACE(std::string(url));
  UrlOrigin origin{};
  ASSERT_TRUE(parseUrlOrigin(url, origin));
  EXPECT_EQ(origin.https, https);
  EXPECT_EQ(origin.host, host);
  EXPECT_EQ(origin.port, port);
}

struct OriginCase {
  const char* a;
  const char* b;
  bool same;
};

}  // namespace

TEST(UrlOriginTest, ParsesSchemeHostAndDefaultPort) {
  expectOrigin("https://api.github.com/repos/o/r", true, "api.github.com", 443);
}

TEST(UrlOriginTest, HttpSchemeDefaultsToPort80) { expectOrigin("http://example.com/x", false, "example.com", 80); }

TEST(UrlOriginTest, SchemeIsCaseInsensitive) {
  expectOrigin("HTTPS://api.github.com/", true, "api.github.com", 443);
  expectOrigin("Http://example.com", false, "example.com", 80);
}

TEST(UrlOriginTest, ExplicitPortIsReported) {
  expectOrigin("https://example.com:8443/x", true, "example.com", 8443);
  expectOrigin("http://example.com:443", false, "example.com", 443);
  expectOrigin("https://example.com:443/", true, "example.com", 443);
}

TEST(UrlOriginTest, AuthorityEndsAtQueryOrFragment) {
  expectOrigin("https://evil.com?x", true, "evil.com", 443);
  expectOrigin("https://evil.com#frag", true, "evil.com", 443);
  expectOrigin("https://evil.com:8443?x/y", true, "evil.com", 8443);
  expectOrigin("https://evil.com", true, "evil.com", 443);
}

TEST(UrlOriginTest, UnsupportedOrMalformedUrlsDoNotParse) {
  for (const char* url :
       {"", "api.github.com/x", "/relative/path", "ftp://example.com/", "https://", "https:///path", "https://:443/",
        "https://example.com:/", "https://example.com:abc/", "https://example.com:12a/", "https://example.com:0/",
        "https://example.com:65536/", "https://example.com:99999999999/", "https://example.com:-1/", "https://[::1]/",
        "https://[::1]:443/", "https://x@api.github.com/", "https://api.github.com@evil.com/",
        "https://user:pw@api.github.com/"}) {
    SCOPED_TRACE(url);
    UrlOrigin origin{};
    EXPECT_FALSE(parseUrlOrigin(url, origin));
  }
}

TEST(UrlOriginTest, SameOriginTable) {
  constexpr const char* TREES = "https://api.github.com/repos/o/r/git/trees/main?recursive=1";
  constexpr const char* BLOB = "https://api.github.com/repos/o/r/git/blobs/0123abcd";
  constexpr OriginCase CASES[] = {
      {TREES, TREES, true},
      {TREES, BLOB, true},
      {"https://example.com/a?x=1#f", "https://example.com/b?y=2#g", true},
      {"https://API.GitHub.com/x", "https://api.github.com/y", true},
      {"HTTPS://api.github.com/x", "https://api.github.com/y", true},
      {"https://example.com:443/x", "https://example.com/y", true},
      {"http://example.com:80/x", "http://example.com/y", true},
      {"https://example.com/", "https://other.com/", false},
      {"https://github.com/", "https://api.github.com/", false},
      {"https://example.com:8443/", "https://example.com/", false},
      {"http://example.com:443/", "https://example.com/", false},
      {"https://api.github.com/x", "http://api.github.com/x", false},
      {"https://api.github.com/", "https://x@api.github.com/", false},
      {"https://api.github.com/", "https://api.github.com@evil.com/", false},
      {"https://api.github.com/", "/relative", false},
      {"https://api.github.com/", "ftp://api.github.com/", false},
      {"https://api.github.com/", "https:///x", false},
      {"https://api.github.com/", "https://api.github.com:abc/", false},
      {"https://api.github.com/", "https://api.github.com:70000/", false},
      {"https://[::1]/", "https://[::1]/", false},
      {"", "", false},
      {BLOB, "https://raw.githubusercontent.com/o/r/main/book.epub", false},
      {BLOB, "https://objects.githubusercontent.com/github-production/abc", false},
  };
  for (const OriginCase& c : CASES) {
    SCOPED_TRACE(std::string(c.a) + " vs " + c.b);
    EXPECT_EQ(sameOrigin(c.a, c.b), c.same);
    EXPECT_EQ(sameOrigin(c.b, c.a), c.same);
  }
}
