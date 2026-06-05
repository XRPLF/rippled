# WebSockets

Async WebSocket support for client RPC and real-time subscriptions. Both plain and SSL. Built on Boost.Beast + Boost.Asio.

## Key Invariants

- All async operations run on a per-session Boost.Asio strand for thread safety
- Outgoing message queue (`wq_`) has a per-session limit (`port().ws_queue_limit`); exceeding it closes the connection with policy error "client is too slow"
- `complete()` must be called after processing each message to resume reading; forgetting it stalls the session
- `WSInfoSub` holds a weak pointer to `WSSession`; dead sessions are automatically skipped during event delivery
- Message size limit: `RPC::Tuning::maxRequestSize`; oversized messages get a `jsonInvalid` error response

## Connection Flow

1. `Door` accepts TCP -> `Detector` probes for SSL vs plain -> creates `SSLHTTPPeer` or `PlainHTTPPeer`
2. HTTP request with WebSocket upgrade -> `ServerHandler::onHandoff` -> `session.websocketUpgrade()` -> creates `PlainWSPeer` or `SSLWSPeer`
3. `BaseWSPeer::run()` -> set permessage-deflate options -> `async_accept` handshake -> `on_ws_handshake` -> `do_read` loop
4. `on_read` -> `ServerHandler::onWSMessage` -> validate JSON -> post to job queue -> `processSession` -> send response -> `complete()`

## Common Bug Patterns

- `on_read` consumes the buffer AFTER calling `onWSMessage`; if the handler accesses the buffer asynchronously after `onWSMessage` returns, it reads garbage
- `close()` with pending messages defers the actual close; calling `send()` after `close()` but before actual close queues more messages
- Missing `complete()` call after sending response = session never reads again = appears hung
- Job queue shutdown: if `postCoro` returns nullptr, session must close with `going_away`; dropping this silently leaks sessions

## Review Checklist

- Verify strand execution for all socket operations (read, write, close, timer)
- New subscription streams: ensure `WSInfoSub::send` handles the new event type
- Flow control: test with slow clients to verify queue limit enforcement

## Key Patterns

### complete() Resumes Read Loop
```cpp
// REQUIRED after sending response — missing this = session hangs forever
void BaseWSPeer::complete()
{
    if (!strand_.running_in_this_thread())
        return post(strand_, std::bind(
            &BaseWSPeer::complete, impl().shared_from_this()));
    do_read();  // resume reading next message
}
```

### Queue Limit Enforcement
```cpp
// REQUIRED: close slow clients to prevent memory exhaustion
if (wq_.size() >= port().ws_queue_limit)
{
    close(boost::beast::websocket::close_code::policy_error);
    return;  // do NOT queue unboundedly
}
```

## Key Files

- `include/xrpl/server/detail/BaseWSPeer.h` - session lifecycle and message queue
- `include/xrpl/server/detail/Door.h` - connection acceptance
- `src/xrpld/rpc/detail/ServerHandler.cpp` - `onWSMessage` and `onHandoff`
- `src/xrpld/rpc/detail/WSInfoSub.h` - subscription delivery
