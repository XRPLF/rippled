# `WSInfoSub.h` — WebSocket Subscription Handle

`WSInfoSub` is the concrete WebSocket implementation of the abstract `InfoSub` subscription handle. Its purpose is narrow and well-defined: it bridges the XRPL notification system (which publishes ledger events, transactions, and server status updates to subscribed clients) to a live WebSocket session managed by the network layer. Every WebSocket connection that goes through `ServerHandler` gets exactly one `WSInfoSub` attached to it as its `appDefined` payload.

## Role in the Subscription Architecture

`InfoSub` defines the abstract contract: track which accounts, books, and event streams a client is subscribed to, and expose a `send()` method that delivers a `Json::Value` to that client. All subscription registration and event dispatch in the RPC subsystem goes through `InfoSub::pointer` handles, so the publisher never needs to know whether its audience is connected via WebSocket, HTTP long-poll, or any other transport. `WSInfoSub` fulfills this contract specifically for the WebSocket case.

The `InfoSub::Source` interface (passed to the constructor) is the bidirectional gateway to the subscription registry—typically `NetworkOPs`. When `WSInfoSub` is constructed it registers with this source; when it is destroyed the base class destructor unregisters all subscriptions on behalf of the now-closed connection.

## Construction and Trusted-Proxy Identity Extraction

The constructor accepts a `std::shared_ptr<WSSession>` but stores it as a `std::weak_ptr`. This is a deliberate ownership decision: the `WSSession` controls its own lifetime (it is owned by the server I/O layer), while the `WSInfoSub` is owned by `session->appDefined` and by any active subscription registrations held in the source. A strong back-reference would create a cycle that neither side could break cleanly.

During construction, the code performs a trust check before reading any proxy-provided headers:

```cpp
if (ipAllowed(
        beast::IPAddressConversion::from_asio(ws->remote_endpoint()).address(),
        ws->port().secure_gateway_nets_v4,
        ws->port().secure_gateway_nets_v6))
```

Only if the remote address falls within the port's configured `secure_gateway` networks are the `X-User` header and `X-Forwarded-For` header trusted. This is the correct defense against header spoofing: a direct client claiming to be an admin user through a forged `X-User` header would be ignored unless the connection came through a known trusted reverse proxy. The resulting `user_` and `fwdfor_` strings are then used immediately after construction in `ServerHandler::onUpgrade()` to establish the `Resource::Consumer` with the appropriate role and rate-limit bucket.

## `send()` Implementation

```cpp
void send(Json::Value const& jv, bool) override {
    auto sp = ws_.lock();
    if (!sp)
        return;
    boost::beast::multi_buffer sb;
    Json::stream(jv, [&](void const* data, std::size_t n) {
        sb.commit(boost::asio::buffer_copy(sb.prepare(n), boost::asio::buffer(data, n)));
    });
    auto m = std::make_shared<StreambufWSMsg<decltype(sb)>>(std::move(sb));
    sp->send(m);
}
```

The `ws_.lock()` guard handles the race between session teardown and pending notification delivery. If the underlying `WSSession` has already been destroyed (connection closed, server shutdown), the lock returns a null pointer and the send is silently dropped. There is no error propagation because there is no caller waiting for the result—events are fire-and-forget from the publisher's perspective.

Serialization uses `Json::stream()` rather than `Json::FastWriter` or `Json::Value::toStyledString()`, writing directly into chunks of a `boost::beast::multi_buffer` via the callback. This avoids materializing the full JSON as an intermediate `std::string` before copying it into the I/O buffer, which is a meaningful allocation saving for high-frequency subscription events like ledger closes or transaction streams.

The serialized buffer is wrapped in a `StreambufWSMsg<multi_buffer>` and handed to `WSSession::send()`. The `StreambufWSMsg` template (defined in `WSSession.h`) implements the chunked `prepare()` protocol that the async WebSocket write loop uses to drain the buffer incrementally.

The unnamed `bool` parameter (`broadcast`) is accepted but ignored. Its semantic in the base class is to signal whether the send is a broadcast (sent to many subscribers simultaneously) versus a targeted reply. The WebSocket transport layer does not differentiate—every `send()` is an individual async write queued on the session—so the flag carries no meaning here.

## Integration Point in `ServerHandler`

`ServerHandler::onUpgrade()` creates the `WSInfoSub`, calls `requestInboundEndpoint()` passing `is->user()` and `is->forwarded_for()` to establish rate-limit accounting, and then stores the shared pointer as `ws->appDefined`. Later, every incoming WebSocket frame goes through `ServerHandler::processSession(WSSession)`, which immediately casts `session->appDefined` back to a `WSInfoSub` to check `getConsumer().disconnect()`—i.e., whether the endpoint has exceeded its resource budget and should be disconnected. The `user()` and `forwarded_for()` accessors return `std::string_view` over the stored `std::string` members, keeping the interface zero-copy while the object is alive.