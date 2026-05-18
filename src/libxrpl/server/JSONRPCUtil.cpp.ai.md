# `src/libxrpl/server/JSONRPCUtil.cpp`

This file implements the two thin but critical utility functions that serialize raw HTTP responses for the XRPL JSON-RPC server. Every HTTP reply that leaves `ServerHandler` passes through `HTTPReply`, making this the single place where the wire format of the server's HTTP layer is defined.

## Role in the Larger System

The XRPL node exposes its RPC API over plain HTTP. `ServerHandler` in `src/xrpld/rpc/detail/ServerHandler.cpp` handles connection lifecycle and request parsing, but it delegates all response serialization here. The public contract is minimal: a single header at `include/xrpl/server/detail/JSONRPCUtil.h` exposes only `HTTPReply`, keeping `getHTTPHeaderTimestamp` as an internal helper.

## `getHTTPHeaderTimestamp`

This function produces a complete `Date:` header line in the HTTP/1.1 date format (`Date: Mon, 01 Jan 2024 00:00:00 +0000\r\n`). The implementation is straightforward POSIX: `time()` → `gmtime_r` (or `gmtime_s` on MSVC) → `strftime` into a 96-byte stack buffer. The cross-platform split uses a compile-time `#ifndef _MSC_VER` guard — the two functions have swapped argument order, which is a historical MSVC divergence, and the guard handles it cleanly without any runtime cost.

A `CHECKME` comment acknowledges that this call is not free and that memoizing the result — since the timestamp only needs second-level precision — might be worthwhile at high request rates. In practice it is called twice per response (once for the 401 challenge path and once for every other response), but no caching has been added.

## `HTTPReply`

```cpp
void HTTPReply(int nStatus, std::string const& content,
               Json::Output const& output, beast::Journal j);
```

`Json::Output` is defined in `include/xrpl/json/Output.h` as `std::function<void(boost::beast::string_view const&)>`. The callback-based design means HTTP headers and body are streamed to the underlying transport in small chunks without ever allocating a single buffer large enough to hold the entire response. This matters for large JSON payloads from ledger-dump commands.

The function handles two distinct code paths:

**Bare 401 challenge.** When `content.empty() && nStatus == 401`, `HTTPReply` emits an HTTP/1.0 `WWW-Authenticate: Basic` challenge with a hardcoded 296-byte HTML body. Two design wrinkles are called out in the source itself: (1) this branch uses `HTTP/1.0` while all other branches use `HTTP/1.1` — the comment marks this as potentially accidental, (2) the `Server:` header is built from `systemName() + "-json-rpc/v1"` with a literal `v1`, whereas the normal path uses `BuildInfo::getFullVersionString()`. The hardcoded `Content-Length: 296` comment warns that the constant must be updated if the HTML body changes — a classic maintenance trap.

**All other responses.** A `switch` on `nStatus` emits the correct HTTP status line for the subset of codes the server actually uses: 200, 202, 400, 401, 403, 404, 405, 429, 500, 501, and 503. The switch has no `default` case — a `// NOLINTNEXTLINE` suppresses the linter. An unrecognized status code silently produces a response with `Date:`, `Connection: Keep-Alive`, `Content-Length`, and `Content-Type` headers but no status line, which would be malformed. In practice this cannot happen because all `HTTPReply` call sites in `ServerHandler.cpp` hard-code one of the enumerated codes.

The `Content-Length` calculation is `content.size() + 2`. The `+2` accounts for the `\r\n` that `HTTPReply` unconditionally appends after the body. This means the length advertised to the client is always accurate without requiring the caller to pre-compute it.

The `Server:` header for normal replies is assembled from `systemName()` (returning `"xrpld"` as a compile-time static string) concatenated with `BuildInfo::getFullVersionString()`, yielding something like `xrpld-json-rpc/xrpld-2.3.0`. This matches the identifier used in WebSocket handshakes via `BaseWSPeer`.

## Call Sites and Error Handling

`ServerHandler::onRequest` calls `HTTPReply(403, ...)` when the port does not have the HTTP protocol enabled, when authorization fails, and when a request is role-forbidden. It calls `HTTPReply(503, ...)` when the server rejects the coroutine (typically during shutdown or overload). The request-processing loop calls `HTTPReply(400, ...)` for every parse and validation failure. The final call at line 983 of `ServerHandler.cpp` sends the actual RPC result with the HTTP status code derived from the JSON response.

Logging is handled by a single `JLOG(j.trace())` at the top of `HTTPReply`, recording the status code and the full content string. At trace level this produces verbose output; the journal guard ensures no string formatting occurs in production unless the trace sink is attached.