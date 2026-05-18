# `include/xrpl/server/detail/JSONRPCUtil.h`

This header is the sole public interface for the XRPL server's JSON-RPC HTTP reply mechanism. It lives in the `server/detail` namespace, marking it as an internal implementation detail of the server subsystem — not intended for consumption outside of the RPC pipeline.

## What It Declares

The header declares a single function:

```cpp
void HTTPReply(int nStatus, std::string const& strMsg,
               Json::Output const&, beast::Journal j);
```

This function is responsible for serializing a complete HTTP/1.x response — status line, standard headers, and body — and streaming it to the caller-provided output sink. It is the only path through which JSON-RPC responses leave the rippled HTTP server layer.

## The `Json::Output` Abstraction

The third parameter, `Json::Output`, is a `std::function<void(boost::beast::string_view const&)>` alias defined in `<xrpl/json/Output.h>`. Rather than writing to a socket or buffer directly, `HTTPReply` calls this callback incrementally for each chunk of response data. Callers construct an `Output` that targets whatever downstream sink they need — typically a `Session` write queue in `ServerHandler.cpp` via a `makeOutput(session)` factory.

This design cleanly separates response formatting from transport. The function doesn't know or care whether it's writing to a TLS stream, a plain TCP connection, or an in-memory string for testing. It only knows how to compose a valid HTTP response.

## Implementation Behavior

The implementation in `src/libxrpl/server/JSONRPCUtil.cpp` handles two distinct cases:

**Authentication challenge (401 with empty body):** When `nStatus == 401` and `strMsg` is empty, `HTTPReply` emits a full `WWW-Authenticate: Basic` challenge using HTTP/1.0 with a hardcoded HTML body. The source includes a prominent comment warning that the `Content-Length: 296` header is manually computed and must be updated if the body ever changes — a fragile pattern that was apparently never refactored.

**All other responses:** A `switch` statement maps status codes 200, 202, 400, 401, 403, 404, 405, 429, 500, 501, and 503 to their canonical HTTP/1.1 status lines. The response always includes:
- An RFC 7231-compliant `Date:` header generated via `getHTTPHeaderTimestamp()` (platform-abstracted via `gmtime_r`/`gmtime_s`)
- `Connection: Keep-Alive`
- `Content-Length` computed from `strMsg.size() + 2` (accounting for the trailing `\r\n` appended after the body)
- `Content-Type: application/json; charset=UTF-8`
- A `Server:` header embedding the rippled system name and full version string from `BuildInfo`

The `+2` in the `Content-Length` is subtle: `HTTPReply` unconditionally appends `"\r\n"` after the content body, so the declared length must account for those two bytes. Forgetting this would corrupt HTTP pipelining.

## Usage in the RPC Pipeline

`ServerHandler::onRequest()` and the internal `processRequest()` function in `ServerHandler.cpp` call `HTTPReply` at every decision point: protocol check failures (403), authorization failures (403), JSON parse errors (400), API version mismatches (400), resource throttling (503), method validation failures (400), and finally for the successful JSON response itself (200). In all cases the `Output` lambda is constructed from the active `Session` reference, funneling response data into the session's asynchronous write queue.

The `beast::Journal j` parameter enables structured trace-level logging of every reply: `JLOG(j.trace()) << "HTTP Reply " << nStatus << " " << content;`. This ties every outbound response to the rippled logging infrastructure without coupling the utility to any specific application context.

## Design Note

The `switch` statement in the implementation has a `// NOLINTNEXTLINE(bugprone-switch-missing-default-case)` suppression, acknowledging that unrecognized status codes silently produce a response with no status line. This is intentional — callers are expected to pass only the enumerated codes — but it means a programming error would yield a malformed response rather than a compile-time or runtime error.