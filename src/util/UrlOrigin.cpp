#include "UrlOrigin.h"

namespace {

constexpr std::string_view SCHEME_SEPARATOR = "://";

char toLowerAscii(const char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

bool equalsIgnoreCase(const std::string_view a, const std::string_view b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (toLowerAscii(a[i]) != toLowerAscii(b[i])) return false;
  }
  return true;
}

bool parsePort(const std::string_view text, uint16_t& out) {
  if (text.empty()) return false;
  uint32_t port = 0;
  for (const char c : text) {
    if (c < '0' || c > '9') return false;
    port = port * 10 + static_cast<uint32_t>(c - '0');
    if (port > UINT16_MAX) return false;
  }
  if (port == 0) return false;
  out = static_cast<uint16_t>(port);
  return true;
}

}  // namespace

bool parseUrlOrigin(const std::string_view url, UrlOrigin& out) {
  const size_t schemeEnd = url.find(SCHEME_SEPARATOR);
  if (schemeEnd == std::string_view::npos) return false;
  const std::string_view scheme = url.substr(0, schemeEnd);
  if (equalsIgnoreCase(scheme, "https")) {
    out.https = true;
    out.port = 443;
  } else if (equalsIgnoreCase(scheme, "http")) {
    out.https = false;
    out.port = 80;
  } else {
    return false;
  }

  const std::string_view rest = url.substr(schemeEnd + SCHEME_SEPARATOR.size());
  const std::string_view authority = rest.substr(0, rest.find_first_of("/?#"));
  if (authority.find_first_of("@[]") != std::string_view::npos) return false;
  const size_t colon = authority.find(':');
  out.host = authority.substr(0, colon);
  if (out.host.empty()) return false;
  return colon == std::string_view::npos || parsePort(authority.substr(colon + 1), out.port);
}

bool sameOrigin(const std::string_view a, const std::string_view b) {
  UrlOrigin originA{};
  UrlOrigin originB{};
  if (!parseUrlOrigin(a, originA) || !parseUrlOrigin(b, originB)) return false;
  return originA.https == originB.https && originA.port == originB.port && equalsIgnoreCase(originA.host, originB.host);
}
