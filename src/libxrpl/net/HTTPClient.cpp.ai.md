# HTTPClient.cpp

`HTTPClient.cpp` provides the XRP Ledger's outbound HTTP/HTTPS client — a self-contained asynchronous implementation used by the rippled process to fetch web content from external hosts. Its primary consumers include the validator list fetcher and any other subsystem that needs to retrieve data over HTTP(S) during node operation.

## Architecture: Public Interface vs. Hidden Implementation

The file uses a deliberate split between a thin public façade and a concrete private implementation class. `HTTPClient` in the header exposes only static factory methods; `HTTPClientImp` (defined entirely within the `.cpp` file) holds all mutable state and performs the actual work. This prevents callers from holding long-lived `HTTPClient` pointers and forces every request to be self-contained from creation through completion.

`HTTPClientImp` doubly inherits from `std::enable_shared_from_this<HTTPClientImp>` and `HTTPClient`. The `shared_from_this()` capability is essential here: every async handler posted to Boost.Asio captures a `shared_ptr` to the `HTTPClientImp` instance, keeping the object alive across the entire async chain regardless of whether the original caller retains any reference. Each static `HTTPClient::get()` or `HTTPClient::request()` call creates a new `HTTPClientImp` on the heap via `make_shared`, fires the first async operation, and returns immediately. The object then sustains itself through captured `shared_ptr`s in Boost.Asio's handler queues.

## Global SSL Context

A module-scoped `std::optional<HTTPClientSSLContext>` named `httpClientSSLContext` serves as a process-global singleton. `HTTPClient::initializeSSLContext()` constructs it with certificate verification paths and a `bool sslVerify` flag; `HTTPClient::cleanupSSLContext()` destroys it. The `HTTPClientImp` constructor dereferences this optional immediately via `httpClientSSLContext->context()`, which provides the `boost::asio::ssl::context` used to initialize the `AutoSocket`. If `initializeSSLContext()` has not been called, this dereference throws `std::bad_optional_access` — an implicit precondition that the process must satisfy before any request is issued. In practice, the server calls `initializeSSLContext()` at startup and the context lives for the entire process lifetime; `cleanupSSLContext()` is only exercised in test teardown.

## The Async Pipeline

Each HTTP(S) request executes as a sequential chain of Boost.Asio async operations:

1. **`httpsNext()`** — arms the deadline timer and fires `async_resolve` on the first hostname in the sites deque.
2. **`handleResolve()`** — on successful resolution, calls `httpClientSSLContext->preConnectVerify()` to set the TLS SNI hostname before connecting, then fires `async_connect`.
3. **`handleConnect()`** — on successful connection, calls `postConnectVerify()` to install the RFC 6125 hostname verifier (using Boost.Asio's `host_name_verification`). If `mSSL` is true, fires `async_handshake`; otherwise calls `handleRequest` directly, bypassing TLS entirely.
4. **`handleRequest()`** — invokes the `mBuild` callable to write an HTTP request into `mRequest`, then fires `async_write`.
5. **`handleWrite()`** — fires `async_read_until` delimited on `"\r\n\r\n"` to capture the response headers into a 32 KB–capped `mHeader` buffer.
6. **`handleHeader()`** — parses the headers with three static `boost::regex` patterns: one for the status code, one for `Content-Length`, and one for any body bytes that arrived in the same read. If the declared `Content-Length` exceeds `maxResponseSize_`, the request is aborted with `errc::value_too_large`. If the full body is already in the header buffer, completion is invoked immediately; otherwise `async_read` retrieves the remainder.
7. **`handleData()`** — combines body bytes with any pre-read bytes and invokes the completion callback. `boost::asio::error::eof` is treated as success, since HTTP/1.0 servers signal end-of-body by closing the connection.

## Timeout and Shutdown Semantics

The `mDeadline` timer (`boost::asio::basic_waitable_timer<std::chrono::steady_clock>`) is armed at the start of `httpsNext()` for the configured duration. If it fires before all async I/O completes, `handleDeadline()` sets `mShutdown` to `errc::bad_address`, cancels the resolver, and calls `mSocket.async_shutdown()` to tear down the connection. A single `boost::system::error_code mShutdown` field acts as a persistent error latch: once set by any handler, all subsequent handlers check it first and short-circuit to `invokeComplete()`. This avoids the complexity of cancelling in-flight operations individually and ensures only the first error is reported.

## `AutoSocket`: Transparent SSL/Plain Dispatch

`AutoSocket` is a thin adapter that wraps a `boost::asio::ssl::stream<tcp::socket>`. Every `async_write`, `async_read`, `async_read_until`, and `async_shutdown` call dispatches at runtime to either the SSL stream or its inner plain TCP layer depending on `mSecure`. In `HTTPClientImp`, the socket is always constructed with the SSL context (since `httpClientSSLContext->context()` is always passed), but TLS is only engaged when `mSSL` is `true` — non-SSL requests skip `async_handshake` entirely and write directly over the TCP layer. This design avoids duplicating the entire async pipeline for plain vs. SSL modes.

## Fallback Retry Across Sites

The `mDeqSites` deque allows the caller to supply a prioritized list of hostnames. After each `invokeComplete()` call, if the completion callback returns `true` (indicating the caller wants to continue) and the deque still has entries, `httpsNext()` is called again with the next hostname. This provides built-in failover across mirrors or CDN endpoints without requiring the caller to implement retry logic. The deque is consumed front-to-back: `invokeComplete()` pops the current entry before invoking the callback.

## Request Generalization

The `request()` method accepts a `std::function<void(boost::asio::streambuf&, std::string const&)>` callable (`mBuild`) that writes any HTTP method into the request buffer. `get()` is a convenience wrapper that binds `makeGet()` as that callable, producing a minimal HTTP/1.0 GET with `Connection: close`. This separation means POST, custom headers, or other methods can be issued through the same async pipeline by providing a different build function, without any changes to the connection or I/O layers.

## Response Size Enforcement

`maxResponseSize_` is enforced at two points. First, in `handleHeader()`, if `Content-Length` is present and exceeds the cap, the request aborts before reading any body. Second, if `Content-Length` is absent, `maxResponseSize_` is used as the exact number of bytes to read — the client will consume up to that many bytes and no more. This bounds both memory allocation and exposure to runaway servers.