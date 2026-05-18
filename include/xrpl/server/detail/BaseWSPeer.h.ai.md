# `BaseWSPeer.h` — WebSocket Peer Base Template

## Role in the System

`BaseWSPeer` is the shared implementation core for all active WebSocket connections in the XRPL server. It sits in the `include/xrpl/server/detail/` layer — the "plumbing" that is never exposed to application code — and provides everything needed to manage a live WebSocket session: the upgrade handshake, chunked message writing, read-dispatch, liveness pinging, and orderly teardown. The two concrete subclasses that instantiate it, `PlainWSPeer` and `SSLWSPeer`, differ only in whether the underlying socket is bare TCP or wrapped in a TLS stream; all protocol logic lives here.

## Template Architecture (CRTP + Policy)

The class is parameterized on two types:

```cpp
template <class Handler, class Impl>
class BaseWSPeer : public BasePeer<Handler, Impl>, public WSSession
```

`Handler` is the application layer — it receives parsed messages via `handler_.onWSMessage()` and drives the session lifecycle. `Impl` is the concrete subclass (`PlainWSPeer<Handler>` or `SSLWSPeer<Handler>`), providing the `ws_` stream member that `BaseWSPeer` accesses through the CRTP helper `impl()`. Because `ws_` has different types in each subclass (`websocket::stream<tcp_stream>` vs. `websocket::stream<ssl_stream<tcp_stream>&>`), making it a template parameter avoids virtual dispatch on every I/O call while still sharing all protocol logic.

`BasePeer` contributes the `strand_`, `port_`, `handler_`, and `remote_address_` members, plus an `executor_work_guard` that keeps the Asio executor alive for the connection's lifetime. `WSSession` is the abstract interface visible to the rest of the server — it defines `run()`, `send()`, `close()`, and `complete()`, all of which `BaseWSPeer` implements.

## Strand-Based Concurrency

Every public entry point — `run()`, `send()`, `close()`, and `complete()` — begins with the same pattern:

```cpp
if (!strand_.running_in_this_thread())
    return post(strand_, std::bind(&BaseWSPeer::run, impl().shared_from_this()));
```

If called from outside the strand (e.g., from a handler thread that wants to push a response), the call is posted back to the strand and the caller returns immediately. Once on the strand, operations can read and write shared state (`wq_`, `do_close_`, `ping_active_`, etc.) without any mutex. This is idiomatic Asio strand use — serialization through scheduling rather than locking.

## Write Pipeline

Outbound messages implement the `WSMsg` abstract interface, whose key method is:

```cpp
virtual std::pair<boost::tribool, std::vector<boost::asio::const_buffer>>
prepare(std::size_t bytes, std::function<void(void)> resume) = 0;
```

`BaseWSPeer` drives this with a `std::list<std::shared_ptr<WSMsg>>` write queue (`wq_`). When `send()` adds the first entry, it immediately calls `on_write({})` to start the pump. Subsequent messages wait until the front of the queue is fully drained.

`on_write` calls `prepare(65536, ...)` — requesting at most 64 KB per call. The tribool return disambiguates three states: `indeterminate` means the data is not yet ready (the `resume` callback will restart the pump); `false` means more chunks follow (routes to `on_write` again); `true` means this is the final chunk (routes to `on_write_fin`). `on_write_fin` pops the queue head and either issues a WebSocket close (if `do_close_` is set) or starts writing the next queued message.

This chunked design lets large ledger responses stream out without ever copying the full payload into a single contiguous buffer.

## Backpressure and Client-Too-Slow Handling

`send()` enforces a hard queue depth limit from the port configuration:

```cpp
if (wq_.size() > port().ws_queue_limit)
{
    cr_.reason = "Policy error: client is too slow.";
    wq_.erase(std::next(wq_.begin()), wq_.end());
    close(cr_);
    return;
}
```

When a slow client allows the queue to grow beyond the limit, pending messages are dropped and the connection is closed with a policy error. Keeping the first queued message in place allows the current write to finish before the close handshake is issued, ensuring a clean WebSocket close frame reaches the client.

## Deferred Close

`close()` sets the `do_close_` flag rather than immediately issuing an async close:

```cpp
if (wq_.empty())
    impl().ws_.async_close(reason, ...);
else
    cr_ = reason;  // defer: on_write_fin will close after draining
```

If there are queued writes, the close is deferred until `on_write_fin` drains the queue. This guarantees that any already-queued responses (e.g., the final message before a clean disconnect) are delivered before the connection tears down.

## Read Side and Handler Handoff

`do_read` issues a single `async_read` that accumulates a complete WebSocket message into `rb_` (a `boost::beast::multi_buffer`). On success, `on_read` extracts the buffer sequence and calls `handler_.onWSMessage()`. Critically, the next `do_read` is not posted immediately — it is triggered only when the handler calls `complete()` on the session. This creates natural backpressure: the server will not accept another message from a client until it has finished processing the previous one.

## Liveness: Ping/Pong and Timer

`start_timer()` arms a `waitable_timer` with a 30-second timeout for remote clients or 3 seconds for loopback connections. When the timer fires in `on_timer()`, two scenarios can occur:

1. **First timeout, no ping outstanding**: A ping frame is sent with a cryptographically random 8-byte payload (using `crypto_prng()`). The comment acknowledges this is "probably overkill" but ensures the payload cannot be guessed and a spoofed pong cannot reset the timer. `close_on_timer_` is set and the timer restarts.

2. **Second timeout, ping sent but no matching pong received**: The connection is closed with a timed-out error code.

The `on_ping_pong` control callback (registered via Beast's `control_callback` mechanism) checks whether the received pong matches the sent payload and clears `close_on_timer_` if so. An important subtlety: the `control_callback_` is stored as a class member (`std::function`), not a temporary, because Beast holds only a non-owning reference to the callable object — storing it inline in `run()` would cause a use-after-free.

## Error Handling

All failure paths funnel through `fail()`, which is strand-assert guarded. It records only the first error (`ec_` check prevents double-failure), cancels the timer, and closes the raw TCP socket directly via `get_lowest_layer`. The `operation_aborted` error code is treated as benign — it signals that a pending async operation was cancelled intentionally (e.g., by `cancel_timer()`) rather than representing a real I/O failure, so it is silently ignored in several callbacks.

## Relationship to Concrete Peers

`PlainWSPeer` and `SSLWSPeer` each add exactly one member — the `ws_` stream — and a constructor that wires up the executor and timer. Neither adds any protocol logic. The entire behavioral surface is in `BaseWSPeer`, making this a textbook CRTP policy class that achieves static polymorphism without virtual dispatch in the hot I/O path.