# `WorkSSL.cpp` — TLS-Secured HTTP Client Worker

`WorkSSL.cpp` provides the SSL/TLS concrete implementation of the asynchronous HTTP client hierarchy used within `rippled` to fetch data from remote HTTPS endpoints — most notably for fetching validator lists, amendment data, and other signed content that the node retrieves from external web services.

## Role in the Work Hierarchy

The file implements a three-level CRTP inheritance chain: `Work` (pure virtual interface with `run()` and `cancel()`) → `WorkBase<Impl>` (template base holding the Boost.Asio plumbing — resolver, socket, request/response state, and strand) → `WorkSSL` (this file; SSL stream layered on top of the base TCP socket). The sibling `WorkPlain` performs the same job over unencrypted TCP, and its `onConnect` is trivially just a null-check followed by `onStart()`. `WorkSSL`'s `onConnect` must do substantially more: it must validate the SSL context after the TCP layer succeeds and then perform an async TLS handshake before anything is written.

Because `WorkBase` is a class template parameterised on `Impl`, it calls `impl().onConnect()` via the CRTP cast, dispatching to either `WorkSSL::onConnect` or `WorkPlain::onConnect` at compile time without a virtual call. The same pattern applies to `impl().stream()`: `WorkPlain::stream()` returns a raw `socket_type&`, while `WorkSSL::stream()` returns the `boost::asio::ssl::stream<socket_type&>` — the two are plug-compatible from `WorkBase`'s perspective because `async_write`/`async_read` accept any stream satisfying the Beast `AsyncStream` concept.

## Construction and Pre-connect Verification

The constructor initialises `context_` (an `HTTPClientSSLContext`) from three `Config` fields: `SSL_VERIFY_DIR`, `SSL_VERIFY_FILE`, and `SSL_VERIFY`. `HTTPClientSSLContext` registers system CA certificates (or loads a custom verify file), optionally appending a directory of additional trust anchors. The `stream_` is then constructed by wrapping the base class's `socket_` with the SSL context — notably the stream takes `socket_` *by reference*, so the same socket object owned by `WorkBase` is reused.

Before `WorkSSL` construction completes, `preConnectVerify()` is called synchronously. This call uses OpenSSL's `SSL_set_tlsext_host_name` to configure the Server Name Indication (SNI) extension, which must be set *before* the TCP connection is established so the correct TLS server certificate can be selected on the remote end. If `SSL_VERIFY` is disabled in config, `preConnectVerify` also sets `verify_none` on the stream. If SNI configuration fails, an `error_code` is returned and the constructor throws `std::runtime_error` via the `Throw<>` macro. This is the only place in the pipeline where a synchronous exception is appropriate: there is no async callback registered yet, so there is no other way to report a hard configuration failure back to the caller.

## The Async Pipeline Extension

After `WorkBase::run()` resolves the host and establishes the TCP connection, `WorkBase::onConnect` records the endpoint and delegates to `impl().onConnect(ec)`. For `WorkSSL` this is `WorkSSL::onConnect`:

```cpp
void WorkSSL::onConnect(error_code const& ec)
{
    auto err = ec ? ec : context_.postConnectVerify(stream_, host_);
    if (err) { fail(err); return; }
    stream_.async_handshake(..., &WorkSSL::onHandshake, ...);
}
```

`postConnectVerify` runs after the TCP layer is up but before any TLS bytes are sent. When peer verification is enabled (`SSL_VERIFY=true`), it calls `set_verify_peer` and then installs an RFC 6125 hostname verification callback (`HTTPClientSSLContext::rfc6125_verify`) that delegates to Boost.Asio's `host_name_verification`, logging a warning if the certificate's CN/SAN does not match `host_`. If peer verification is disabled, `postConnectVerify` is a no-op. Any error from either step causes an immediate `fail()` call, which delivers the error code to the registered callback and clears the callback pointer to prevent double-delivery.

Only if both SSL setup steps succeed does `WorkSSL` issue `stream_.async_handshake(client, ...)`, passing `shared_from_this()` through `bind_executor` to keep the object alive while the TLS negotiation is in flight.

`onHandshake` is minimal by design: check the error code, call `fail()` on failure, or call `onStart()` on success. `onStart()` is defined on `WorkBase` and initiates the HTTP GET by calling `async_write` on `impl().stream()` — which at this point is the fully-negotiated SSL stream.

## Error Handling and Lifetime

Two distinct error-reporting mechanisms are in play. Hard synchronous failures (bad SSL config at construction time) throw exceptions immediately. All async failures — TCP connect errors, `postConnectVerify` errors, handshake errors, write errors, and read errors — are reported through the `fail()` method, which invokes `cb_` exactly once and then nulls it. `WorkBase`'s destructor also fires `cb_` if it was never called (with `not_a_socket`), which covers cancellation paths. This ensures the caller always receives exactly one callback regardless of which async stage fails.

The `strand_` inherited from `WorkBase` serialises all async completions. Every `bind_executor` call in both `WorkBase` and `WorkSSL` wraps handlers in this strand, guaranteeing that `onConnect`, `onHandshake`, `onStart`, `onRequest`, and `onResponse` never execute concurrently even if the `io_context` runs on multiple threads.