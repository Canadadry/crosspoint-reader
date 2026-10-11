---
title: "HTTP additions for GitHub sync: extra request header and cross-host redirect guard"
description: "HttpDownloader cannot send the Accept header the GitHub blob endpoint needs, and it resends Basic-auth credentials to whatever host a redirect points at."
status: needs-triage
---

## Problem Statement

The GitHub library sync (plan 1) downloads every book through
`GET /repos/{owner}/{repo}/git/blobs/{sha}` with `Accept: application/vnd.github.raw+json`, and
authenticates with the owner's fine-grained token sent as the password of preemptive Basic auth.
Two things in today's `HttpDownloader` stand in the way:

- It has no way to add a request header, so the blob endpoint would answer with JSON and base64
  instead of the raw file.
- On the compiled path (`runGetWolf`, built because every environment defines
  `FREEINK_NET_WOLFSSL=1`), each redirect hop builds a new client and re-adds the `Authorization`
  header for whatever URL the `Location` header names. A redirect to another host would hand the
  owner's token to that host.

The owner's TLS decision (no certificate verification) already accepts one risk for the token; it
must not gain a second, avoidable one through redirects.

## Solution

`HttpDownloader` gains one new streaming entry point that takes a small options struct:

- one optional extra request header (name and value), sent on every hop;
- an opt-in guard: when credentials are set, a redirect whose target is on a different origin
  (scheme, host or port) is refused before any request is sent to it, logged, and reported with a
  distinct error value.

The origin comparison is a small pure helper, host-tested with gtest. Existing entry points, their
signatures and their behaviour are unchanged, so OPDS, OTA, font downloads and KOReader behave and
compile exactly as before. Step 9 uses the new entry point to stream the trees body into
`GitTreeParser` and each blob body into the executor's `SyncChunkSink`, without buffering.

## User Stories

1. As the library owner, I want the device to ask GitHub for the raw blob, so that each downloaded book is the real file and its git blob SHA matches the tree.
2. As the library owner, I want my token never to be sent to a host other than the one the sync called, so that a redirect cannot leak it.
3. As the library owner, I want a refused redirect to stop that request with a clear log line, so that I can tell a redirect problem from a network failure.
4. As the library owner, I want a same-host redirect (for example GitHub's 301 for a renamed repository on `api.github.com`) to still work with my credentials, so that a harmless redirect does not break the sync.
5. As the library owner, I want a redirect from https to http on the same host to be refused while credentials are set, so that my token is never sent in clear text.
6. As the library owner, I want a redirect whose target cannot be parsed to be refused, so that the guard fails closed.
7. As the library owner, I want the refused redirect's log line not to print the target's path or query, so that signed URLs or other secrets in a `Location` header do not end up in the serial log.
8. As the sync engine (step 9), I want a streaming fetch that delivers body chunks as they arrive, so that a multi-MB EPUB goes straight to the temp file through `SyncChunkSink` with no RAM copy.
9. As the sync engine, I want the trees response streamed chunk by chunk, so that `GitTreeParser` can consume it without the JSON ever being held in RAM.
10. As the sync engine, I want only a final 200 response's body to reach my callback, so that an error page or a redirect body is never hashed or parsed as content.
11. As the sync engine, I want a distinct result for "redirect refused", so that I can stop the whole sync instead of failing each file in turn.
12. As the sync engine, I want the usual results (OK, HTTP error, callback stop, abort) on the same entry point, so that I can map them onto the executor's fetch result.
13. As the sync engine, I want returning false from my data callback to stop the transfer, so that the executor can refuse bytes past the declared size.
14. As an OPDS user, I want my catalogs and book downloads, including authenticated servers that redirect to a CDN, to behave exactly as today, so that this feature costs me nothing.
15. As a device user updating firmware, I want the OTA release check and firmware download to behave exactly as today, so that updates keep working.
16. As a device user downloading fonts, I want font downloads to behave exactly as today.
17. As a KOReader sync user, I want progress sync to behave exactly as today.
18. As a firmware developer, I want existing call sites to compile without edits, so that this change is reviewable as additive.
19. As a firmware developer, I want the origin comparison in a pure helper with host tests, so that its edge cases (case, default ports, userinfo, relative targets) are pinned without hardware.
20. As a firmware developer, I want the review to name the `#if` branch each change lands in, so that it is clear what the build actually compiles.
21. As a firmware developer, I want the dead `esp_http_client` branch to stay consistent and fail closed, so that a future build without wolfSSL cannot leak credentials through it.
22. As a firmware developer, I want the new options to cost no heap beyond what a header already costs, so that the 64 KB sync budget is not touched.

## Implementation Decisions

**Modules**

- **Origin helper (new, pure).** A small module in the firmware's utility area, with no Arduino,
  logging or heap dependency, so it builds on the host like the existing OPDS filename helper.
  Interface:
  - a parse function taking a URL as `std::string_view` and filling an origin value (scheme as
    http or https, host as a view into the URL, effective port), returning false when the URL is
    not an absolute http(s) URL it can parse;
  - `sameOrigin(a, b)`: true only when both parse and scheme, host and effective port are equal.
- **HttpDownloader (modified).** New public options struct, new enum value, new streaming entry
  point; internal plumbing passes the options from the entry point through the private `Sink` /
  run functions to both `#if` branches.

**API contract**

- New options struct, a plain aggregate with defaults that mean "current behaviour":
  - extra header name and value as `const char*`, both null by default; the header is sent only
    when both are non-null and non-empty. The pointers must outlive the call (the sync passes
    string literals);
  - a bool, default false, that turns on the cross-host guard.

  A struct rather than two loose `const char*` parameters, because it carries the guard flag too
  and leaves room for step 9 without another overload. It sits on the caller's stack (two
  pointers and a bool).
- New entry point: a streaming fetch taking the URL, the existing `DataCallback`, username,
  password and the options, returning `DownloadError`. It has no defaulted parameters and a
  distinct name from `fetchUrl`, so it cannot be picked by overload resolution from an existing
  call. It returns `DownloadError` rather than bool because step 9 needs to tell a refused
  redirect from other failures.
- `DownloadError` gains `REDIRECT_REFUSED`, appended after the existing values. No existing code
  switches on `DownloadError` (callers compare with `==`/`!=` `OK`), so no `-Wswitch` warning
  appears and no caller changes.
- Existing `fetchUrl` overloads and `downloadToFile` keep their signatures and pass default
  options internally: no extra header, guard off. `downloadToFile` gets no options: the executor
  writes its own temp file through the step 6 storage interface and only needs a stream.
- Only a 200 response's body reaches the callback, and success requires 200 with a complete body.
  This is today's `runGetWolf` behaviour (the data lambda ignores non-200 chunks and any status
  other than 200 after redirects is `HTTP_ERROR`); the PRD keeps it and the new entry point
  inherits it.
- The `DataCallback` is a `std::function`, as in the existing API. Step 9's adapter captures one
  pointer, which fits `std::function`'s small-object buffer, so wrapping a `SyncChunkSink` costs
  no heap.

**Redirect guard semantics**

- It applies only when the caller opted in **and** credentials are set, using the same test that
  decides whether `Authorization` is sent (username and password both non-empty). With the
  guard off or no credentials, redirects are followed as today.
- It compares the resolved target of each redirect with the origin of the **start URL**: scheme,
  host (case-insensitive) and effective port (an explicit 443 on https equals no port). The
  target is the URL after the existing `Location` resolution, so a relative `Location` is
  same-origin by construction.
- Same origin: the hop proceeds and credentials are sent again, as today (a GitHub repository
  rename answers 301 on `api.github.com`). The extra header is also re-sent on every hop.
- Different origin, including https to http on the same host: the redirect is refused before the
  next hop's client is created, so no connection or request is made to the target. The function
  logs with `LOG_ERR` the status and the target host only (no path, no query, never the
  credentials) and returns `REDIRECT_REFUSED`.
- Fail closed: a target that does not parse, has userinfo (`@` in the authority), a bracketed IPv6
  literal, an empty host or an invalid port counts as a different origin. Only a guarded,
  authenticated request can be refused by this, and the GitHub API never needs those forms.
- The redirect hop limit (5) is unchanged.

**Which redirect policy the sync uses (decided from the plan's wording)**

- The sync uses the cross-host guard, not a "no redirects at all" mode. The plan's Tree snapshot
  rule names the risk as resending the token to *another host*, step 8 names the guard as "no
  cross-host redirect when credentials are set", and section 5's fallback says "keep the
  no-cross-host-redirect guard". A same-host redirect is harmless for the token and is how GitHub
  answers a renamed repository. Step 0.0 recorded that both GitHub calls answer 200 with no
  redirect, so the guard is a safety net, not a path the sync expects to take. No "refuse all
  redirects" mode is added.

**Why opt-in rather than on for every authenticated call**

- The step says existing callers are unchanged. OPDS passes the server's username and password,
  and OPDS download endpoints redirect (the comment above the redirect loop in the
  `esp_http_client` branch says so), possibly to a CDN on another host. Turning the guard on for
  every authenticated call would change OPDS behaviour, which the step rules out. Hardening OPDS
  is listed under Out of Scope.

**Where each change lands (`#if` branches)**

- `#if defined(FREEINK_NET_WOLFSSL)` → `runGetWolf`, the branch every environment compiles:
  - the extra header is added with `SecureHttpClient::addHeader` right after the `Authorization`
    header, for every hop;
  - the guard runs in the redirect branch, after `SecureHttpClient::resolveUrl` produced the next
    URL and before `continue` starts the next hop.

  `setUserAgent` is untouched, so GitHub still receives the User-Agent it requires.
- `#if !defined(FREEINK_NET_WOLFSSL)` → `runGet` (esp_http_client), kept as it is today:
  - the extra header is set with `esp_http_client_set_header`;
  - the guard is stricter there: when it is on and credentials are set, any redirect is refused
    with `REDIRECT_REFUSED` before `esp_http_client_set_redirection`. Reading the resolved target
    would need `esp_http_client_get_url` and a URL-sized buffer in code that no environment
    compiles, so the branch fails closed instead. It never sends credentials to an unchecked
    host.
- The dispatcher `runGetSecure` passes the options to whichever branch is compiled.

**Memory**

- No new persistent allocation. The extra header costs one small `std::string` inside
  `SecureHttpClient`'s header list per hop, like the existing `Authorization` header, and is
  freed with the client at the end of the hop. The origin helper works on `std::string_view`
  with no allocation and no buffer larger than a few words on the stack.

**Logging**

- No log line prints the password or the `Authorization` value. The existing `LOG_DBG` of each
  hop's URL is unchanged; GitHub API URLs carry no secret, since the token travels in the header.

## Implementation Chunks

1. **Origin helper and its gtest suite.** Pure code, host-tested, registered in the test
   CMake list.
2. **HttpDownloader wiring.** Options struct, `REDIRECT_REFUSED`, new streaming entry point,
   plumbing through `Sink` and both `#if` branches, guard calls into the helper. Verified by
   `pio run` and the reviews below.

## Testing Decisions

- A good test checks only external behaviour: given two URLs, does the helper say same origin
  or not, and what host and port does it report. It never checks how parsing is done.
- **Origin helper, gtest suite** (new, alongside the existing ones), table-driven, covering:
  - identical URLs, and differing paths, queries or fragments on the same origin: same;
  - host case differences (`API.GitHub.com` vs `api.github.com`), and scheme case
    (`HTTPS://`): same;
  - an explicit default port vs none (`:443` on https, `:80` on http): same;
  - a different host, subdomain or port: different;
  - https vs http on the same host: different;
  - userinfo in the authority (`https://x@api.github.com/`, `https://api.github.com@evil.com/`):
    different;
  - authority ending at `?` or `#` (`https://evil.com?x`): parsed host is `evil.com`;
  - unparsable or unsupported targets (no scheme, `ftp://`, empty host, non-numeric or
    out-of-range port, bracketed IPv6, empty string): parse fails and the result is "different";
  - the real GitHub shapes: the trees URL vs a blob URL (same) vs a
    `raw.githubusercontent.com` or `objects.githubusercontent.com` URL (different).
- Prior art: the OPDS filename suite, which compiles a source file from the firmware's utility
  area against gtest, and the GitSync suites for table style.
- **HttpDownloader itself is not unit-tested**: it depends on `SecureHttpClient`, Wi-Fi and
  `HalStorage`, none of which link on the host. It is covered by:
  - `pio run` (default environment) with no new warnings;
  - a review that names, for each change, the `#if` branch it lands in (as listed under
    Implementation Decisions);
  - a review that the call sites in OPDS (catalog fetch and book download), OTA (release check
    and firmware stream) and font downloads are untouched by the diff and compile. KOReader sync
    does not call `HttpDownloader` (it drives `SecureHttpClient` directly), so it is unaffected
    by construction; the review states this.
- Owner check, for the step 12 list: the first real sync's blob downloads verify against their
  tree SHAs, which proves the `Accept` header reached GitHub (a JSON/base64 body would fail
  verification).

## Out of Scope

- Turning the cross-host guard on for OPDS or other authenticated callers, or stripping
  credentials on a cross-host hop instead of refusing it. Both change existing behaviour; they are
  a separate hardening change.
- Blocking https-to-http downgrades for unauthenticated requests (today `runGetWolf` follows them).
- A "refuse every redirect" mode, a cancel flag on the new entry point (the data callback can
  already stop the transfer, and the plan cancels between files), and response-header or status
  accessors for callers (for example a "bad token" message on 401).
- Removing the dead `esp_http_client` branch.
- TLS certificate verification (rejected by the owner, plan section 2).
- Anything in steps 7, 9, 10 and 11: the config loader, the sync glue, the activity and the menu
  entry.

## Further Notes

- The guard compares with the start URL's origin, not the previous hop's. A chain that stays on
  one origin is equivalent; a chain that leaves and comes back is refused at the first exit.
- `SecureHttpClient::resolveUrl` treats a scheme-relative `Location` (`//host/x`) as an absolute
  path on the current host. That keeps such a hop on the start origin, so the guard has nothing
  to refuse and no credentials leak; it is noted here, not fixed (SDK code).
- **H-7-1 (hypothesis about step 7):** the config loader exposes owner, repo, branch and token in
  a form that converts to `std::string` (or is one), so step 9 can pass the token as the password
  argument with a fixed non-empty username such as `x`, as in step 0.0's curl calls.
- **H-9-1 (hypothesis about step 9):** step 9's fetch callback calls the new streaming entry point
  with the guard on, the raw `Accept` header for blobs and `application/vnd.github+json` (or no
  extra header) for the trees call, and wraps `SyncChunkSink::write` in the data callback so a
  false from the sink stops the transfer.
- **H-9-2 (hypothesis about step 9):** step 9 treats `REDIRECT_REFUSED` as fatal for the whole run
  (it stops the sync with an error, per the plan's Tree snapshot rule) rather than as a per-file
  `FetchFailed` that would repeat for every file.
- No `guideline` decisions were needed: the opt-in choice follows from "existing callers are
  unchanged" and the OPDS redirect comment, and the redirect policy follows from the plan's
  wording quoted above.
