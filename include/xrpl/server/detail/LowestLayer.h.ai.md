# `include/xrpl/server/detail/LowestLayer.h`

## Purpose and Context

This 23-line header exists solely as a Boost version compatibility shim. The XRPL server's networking layer stacks Boost.Beast streams — a plain or TLS socket wrapped in an HTTP or WebSocket layer — and peer teardown logic regularly needs to reach the raw `tcp::socket` buried at the bottom of that stack. Beast provides a mechanism to do exactly that, but the API changed in a breaking way at Boost 1.70, requiring different code paths and different headers depending on which version is installed.

Rather than scattering `#if BOOST_VERSION` guards throughout every call site, this file centralises the divergence into one wrapper function, giving the rest of the server a uniform, version-agnostic surface.

## The API Divergence Being Bridged

Prior to Boost 1.70, each layered stream type exposed the bottom of its stack via a member function: `t.lowest_layer()`. The calling code also needed to supply the template parameter explicitly. Starting in Boost 1.70, Beast introduced `boost::beast::get_lowest_layer(t)` as a free function with automatic type deduction, and moved the supporting traits from `<boost/beast/core/type_traits.hpp>` into the new `<boost/beast/core/stream_traits.hpp>` header. The old member-function form was deprecated and eventually removed, so simply calling one or the other at compile time is not possible without a version check.

## Design of `xrpl::get_lowest_layer`

```cpp
template <class T>
decltype(auto)
get_lowest_layer(T& t) noexcept
{
#if BOOST_VERSION >= 107000
    return boost::beast::get_lowest_layer(t);
#else
    return t.lowest_layer();
#endif
}
```

The function is a transparent forwarding wrapper. `decltype(auto)` is chosen deliberately over a plain `auto` return because the underlying calls return references to the lowest-layer object rather than copies — stripping the reference with `auto` would silently copy a socket, which would be both wrong and expensive. `noexcept` is correct for both branches: neither member access nor the Beast free function can throw.

The preprocessor branch is the smallest possible divergence point: one `#include` and one expression differ between the two paths. Every other aspect — template parameter, function signature, `noexcept`, `decltype(auto)` — is shared.

## Usage in the Server

`BasePeer<Handler, Impl>::close()` is the primary consumer. When a peer is asked to close, it calls `xrpl::get_lowest_layer(impl().ws_).socket().close(ec)` to peel through the WebSocket (and possibly TLS) layers and close the raw socket directly. The error code from `close()` is intentionally discarded — by the time `close()` is reached the goal is orderly resource release, and socket-level errors at that point are not actionable.

`BaseWSPeer` uses the same call pattern from its failure path, again reaching the TCP socket to force a close when the WebSocket handshake or I/O has failed. Notably, `BaseHTTPPeer` calls `boost::beast::get_lowest_layer` directly rather than through this wrapper — that file predates or does not need the compatibility layer, or was updated independently when the minimum Boost version changed.

## Architectural Note

This file is a narrow, well-contained answer to a real problem: third-party library API churn. By isolating the version check here rather than in every peer class, any future change to the minimum supported Boost version — or a further API revision in Beast — requires a single-point update rather than a codebase-wide search-and-replace. It also keeps the peer implementations readable: `xrpl::get_lowest_layer(stream)` reads as intent, not as a version negotiation.