# `HTTPClientSSLContext.h` — SSL Context Management for Outbound HTTPS Connections

`HTTPClientSSLContext` is a self-contained SSL lifecycle manager for outbound HTTP(S) client connections in the XRPL node. Its responsibility is twofold: configure a `boost::asio::ssl::context` with the correct certificate trust anchors at construction time, then orchestrate the correct sequencing of TLS verification setup across the two-phase connection lifecycle.

## Why This Class Exists

Boost.Asio SSL requires careful ordering of API calls around a TLS connection: some settings must be applied before the TCP connect, others must be applied after TCP connect but before the TLS handshake. Placing this logic in a single class prevents callers from accidentally applying these in the wrong order, and ensures that the same verification policy is consistently enforced across the two distinct consumers in the codebase.

## Certificate Trust Sources

The constructor resolves certificate trust anchors through a priority hierarchy:

1. If `sslVerifyFile` is non-empty, it loads a specific PEM CA bundle via `ssl_context_.load_verify_file()`.
2. Otherwise, it calls `registerSSLCerts()`, which on Linux/macOS calls Boost.Asio's `set_default_verify_paths` (using OS-standard locations), and on Windows reads from the CryptoAPI system store.
3. If `sslVerifyDir` is also non-empty, it additionally registers a directory of CA certificates via `ssl_context_.add_verify_path()`.

Errors during certificate loading throw `std::runtime_error` immediately, with one deliberate exception: if `registerSSLCerts` fails but a `sslVerifyDir` was also provided, the error is suppressed — the directory path is treated as an authoritative fallback. If neither fallback is available, the failure is fatal.

The `sslVerify` flag can suppress all peer verification entirely (used by `WorkSSL` when `Config::SSL_VERIFY` is false). This trades security for connectivity in development or internal-network scenarios.

## Two-Phase Verification Protocol

The class exposes `preConnectVerify()` and `postConnectVerify()`, designed to be called at specific points in the async connection lifecycle.

**`preConnectVerify(strm, host)`** must be called before `async_connect`. It does two things:

- Sets the TLS SNI (Server Name Indication) extension via `SSL_set_tlsext_host_name()`. SNI embeds the target hostname in the TLS ClientHello, which is required for virtual-hosting servers and CDNs that serve multiple certificates from a single IP. This must precede the handshake — there is no way to set it after the fact.
- If verification is disabled, immediately applies `verify_none` to the stream.

**`postConnectVerify(strm, host)`** is called after TCP connect succeeds but before the TLS handshake. When verification is enabled, it applies `verify_peer` mode and binds `rfc6125_verify` as the per-certificate callback.

This split is intentional. SNI must be in the ClientHello (before handshake), while the verify callback only has meaning once a stream is connected; configuring it earlier would have no effect and could be overwritten.

## Hostname Verification via RFC 6125

`rfc6125_verify()` is a static callback that wraps Boost.Asio's `host_name_verification` functor. It checks that the server's certificate matches the expected hostname according to RFC 6125 (covering wildcard and SAN matching). On failure it logs a warning via the `beast::Journal` before returning `false`, ensuring that failed verifications are traceable in node logs without crashing the process.

## Template Constraints

`preConnectVerify` and `postConnectVerify` are templated but constrained via `std::enable_if_t` to accept only `boost::asio::ssl::stream<tcp::socket>` or `boost::asio::ssl::stream<tcp::socket&>`. This covers both socket ownership models — an owned socket (`WorkSSL` holds a `socket_type` and wraps it by reference in `stream_type`) and a directly-owned stream — while preventing accidental use with incompatible ASIO types that would compile but behave incorrectly at runtime.

## Consumers and Lifecycle

There are two distinct usage patterns in the codebase:

**`WorkSSL`** (in `src/xrpld/app/misc/detail/WorkSSL.cpp`) creates a private `HTTPClientSSLContext` instance per connection object, constructed from `Config::SSL_VERIFY_DIR`, `Config::SSL_VERIFY_FILE`, and `Config::SSL_VERIFY`. It explicitly requests `tlsv12_client` rather than the default `sslv23`, locking connections to TLS 1.2. `preConnectVerify` is called in the `WorkSSL` constructor, and `postConnectVerify` is called in `onConnect()` before the async handshake is dispatched.

**`HTTPClient`** (in `src/libxrpl/net/HTTPClient.cpp`) uses a different pattern: a static `std::optional<HTTPClientSSLContext>` — a single shared context — initialized once via `HTTPClient::initializeSSLContext()` and reused across all connections made through `HTTPClientImp`. The `mSocket` (an `AutoSocket` wrapping an SSL stream) is constructed with a reference to this shared context's `boost::asio::ssl::context`, relying on Boost.Asio's thread-safety guarantees for read-only context access from multiple connections.

The distinction reflects the two use cases: `WorkSSL` is used for individual validator amendment/fee fetching tasks where per-connection configuration may vary, while `HTTPClient` handles general-purpose HTTPS fetches (e.g., validator list downloads) under a single application-wide TLS policy.