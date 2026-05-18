# `include/xrpl/net/HTTPClient.h`

## Role in the System

`HTTPClient` is the XRPL node's outbound HTTP/HTTPS client, used whenever the server needs to fetch data from an external URL — notably for fetching the Unique Node List (UNL) and for issuing RPC webhook callbacks. It presents a narrow, purely static public interface over a fully asynchronous Boost.Asio pipeline, hiding all connection state inside a private implementation type.

## Design: Static Factory over a Hidden `shared_ptr` Object

The header declares only static methods; there is no public way to construct or own an `HTTPClient` instance. The real work happens in `HTTPClientImp`, defined entirely inside `HTTPClient.cpp`. Each call to `get()` or `request()` calls `std::make_shared<HTTPClientImp>(...)` and immediately invokes its request pipeline. The `shared_ptr` keeps the object alive for the duration of the async chain through idiomatic `shared_from_this()` captures — callers never hold a reference, so there is no lifetime management burden on the call site.

This pattern — factory function, opaque internal class, `shared_from_this` for self-lifetime — is the standard Asio idiom for fire-and-forget async operations. The alternative of exposing the internal state through the header would make every caller responsible for keeping the connection object alive across dozens of async continuations, which is error-prone.

## Global SSL Context

A single `std::optional<HTTPClientSSLContext>` lives as a module-level static in `HTTPClient.cpp`. `initializeSSLContext()` must be called once at startup before any requests are made; it constructs the `HTTPClientSSLContext`, which configures a `boost::asio::ssl::context` with optional CA directories or files. Each `HTTPClientImp` receives a reference to this context at construction time.

`cleanupSSLContext()` resets the optional, releasing the OpenSSL resources. The inline documentation is explicit that this must not be called while requests are in flight, and notes it is only called from tests — in production the context is effectively process-scoped. The `maxClientHeaderBytes` constant (32 KB) caps the `mHeader` streambuf to prevent runaway header reads.

## Async Pipeline

`HTTPClientImp` drives an ordered handler chain:

1. **`httpsNext()`** — arms the deadline timer, then initiates DNS resolution via `mResolver.async_resolve`.
2. **`handleResolve()`** — on success, calls `HTTPClientSSLContext::preConnectVerify()` to set the TLS SNI hostname, then issues `boost::asio::async_connect`.
3. **`handleConnect()`** — calls `postConnectVerify()` to configure peer-certificate verification callbacks (RFC 6125 hostname matching), then either begins a TLS handshake (`async_handshake`) for SSL connections or jumps directly to `handleRequest()` for plaintext.
4. **`handleRequest()`** — invokes the `mBuild` function, which populates the `mRequest` streambuf with the HTTP message, then calls `async_write`.
5. **`handleWrite()`** — reads until `\r\n\r\n` to capture the entire HTTP response header into `mHeader`.
6. **`handleHeader()`** — parses the status line and optional `Content-Length` header using `boost::regex`, enforces the `maxResponseSize_` limit, and either completes immediately (zero-length body) or issues `async_read` for the remaining body bytes.
7. **`handleData()`** — assembles the full body and calls `invokeComplete`.

The `mShutdown` error code acts as a one-way latch: once set by a timeout, resolve failure, connect failure, or verification error, every subsequent handler short-circuits to `invokeComplete` without touching the network. This prevents the common async bug of partially executing a pipeline after a prior stage has already signalled failure.

## Site Failover via `std::deque`

The `get()` overload accepting `std::deque<std::string> deqSites` exposes a built-in retry/fallback mechanism. After each attempt — success or failure — `invokeComplete()` pops the front of the deque and inspects the bool returned by the completion callback. If the callback returns `true` and sites remain in the deque, `httpsNext()` is called again with the next hostname. This allows callers to supply a ranked list of mirrors; the completion callback decides per-attempt whether to keep trying.

The single-site `get()` and `request()` overloads simply wrap their hostname in a one-element deque before delegating to the same mechanism.

## `AutoSocket` and SSL Transparency

`AutoSocket` wraps a `boost::asio::ssl::stream<tcp::socket>` and routes every async operation — `async_read`, `async_write`, `async_handshake`, `async_shutdown` — to either the SSL stream or its inner plain TCP layer depending on a `mSecure` flag. `HTTPClientImp` uses only the SSL path for outbound requests (`async_handshake` is always called with `client` as the handshake type, which forces SSL). The non-SSL branch in `handleConnect` exists for plaintext HTTP when the caller passes `bSSL = false`, bypassing the handshake step while still using `AutoSocket`'s unified read/write API.

## `request()` vs `get()`

`request()` is the primitive: callers supply a `build` lambda that writes directly into a `boost::asio::streambuf`. `get()` delegates to `request()` by binding `HTTPClientImp::makeGet()` as the builder, which emits an HTTP/1.0 `GET` with `Connection: close`. Using HTTP/1.0 is intentional — each request tears down the connection after a single exchange, which suits one-shot external fetches and avoids persistent-connection management complexity. The `RPCCall.cpp` usage demonstrates `request()` directly to send POST bodies for webhook delivery.

## Response Size Safety

`responseMax` is the caller's hard limit on body bytes. In `handleHeader()`, the `Content-Length` value is parsed and compared against `maxResponseSize_`; if `Content-Length` exceeds the limit, the connection is aborted with `value_too_large`. If `Content-Length` is absent, `responseMax` is used directly as the read buffer size. This two-stage guard prevents a malicious or misbehaving server from causing unbounded memory allocation regardless of whether it announces a size.