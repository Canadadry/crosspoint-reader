#pragma once
#include <cstdint>
#include <string_view>

struct UrlOrigin {
  bool https;
  std::string_view host;  // view into the parsed URL, case as written
  uint16_t port;          // explicit port, or 443/80 for https/http
};

// Fills `out` from an absolute http(s) URL. Fails closed: no scheme, another scheme, an empty
// host, userinfo, a bracketed IPv6 literal or a port that is not 1..65535 all return false.
bool parseUrlOrigin(std::string_view url, UrlOrigin& out);

// True only when both URLs parse and share scheme, host (case-insensitive) and effective port.
bool sameOrigin(std::string_view a, std::string_view b);
