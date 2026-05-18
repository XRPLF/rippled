# `BaseHTTPPeer.h` — CRTP Base for Active HTTP Connections

## Role in the System

`BaseHTTPPeer` is the heart of the XRP Ledger's HTTP server layer. It sits between the transport socket (plain TCP or TLS) and the application-level `Handler`, implementing the complete lifecycle of a single HTTP connection: accept, read, dispatch, write, keep-alive, and close. It lives in `include/xrpl/server/detail/` because it is an internal building block, not part of the public API surface.

Two concrete classes derive from it using the Curiously Recurring Template Pattern (CRTP): `PlainHTTPPeer<Handler>` for unencrypted connections and `SSLHTTPPeer<Handler>` for TLS. Both follow the same coroutine-driven read/write model; they differ only in the type of `stream_` they own and in their `do_close()` and `do_request()` implementations.

## Inheritance Design

`BaseHTTPPeer` inherits from two bases simultaneously.

From `io_list::work` it gets lifecycle membership in the server's I/O registry. When the server shuts down, `io_list::close()` iterates over every live `work` item and calls `close()` on it, which closes the socket. The `work` destructor decrements the registry counter; the last item to be destroyed signals a condition variable that `io_list::join()` waits on. This gives the server a clean, blocking shutdown path.

From `Session` it provides the public API that `Handler` implementations use: `journal()`, `port()`, `remoteAddress()`, `request()`, `write()`, `detach()`, `complete()`, and `close(bool)`. Three of these (`detach`, `complete`, and `close(bool)`) are marked `DEPRECATED` in the implementation, signalling a transitional design where the newer `onHandoff` callback returns an immediate response object rather than relying on the session's deferred write primitives.

## CRTP Access to `stream_`

The CRTP idiom solves a concrete problem: the stream type differs between plain and TLS peers, but all the I/O logic in the base class must reach the stream. The `impl()` helper performs an unchecked downcast to `Impl*`, giving the base access to `impl().stream_`. Every call to `boost::beast::get_lowest_layer(impl().stream_)` reaches the underlying `tcp_stream`'s timeout and close machinery, regardless of whether TLS is layered on top.

## Read Loop

`do_read()` is a Boost.Asio stackful coroutine spawned via `util::spawn`. The `util::spawn` wrapper ensures that unhandled exceptions propagate to `io_context::run()` rather than being silently swallowed, a regression that was introduced in Boost 1.84.

Inside `do_read()`, the timer is armed before the async read and cancelled after it. Beast's `async_read` both parses the HTTP message into `message_` and accumulates bytes in `read_buf_`. On success, `do_request()` is called — a pure virtual method that the derived class implements to invoke the `Handler`. If the client closed the connection, `do_close()` is called; a timeout calls `on_timer()` which routes through `fail()`.

The constructor accepts a `ConstBufferSequence` and pre-copies it into `read_buf_`. This seeds the read buffer with any bytes the `Door` (the acceptor) already captured during protocol detection — the bytes are not lost when ownership transfers to the peer.

## Dual Write Queue

The `write(void*, size_t)` path enqueues a heap-allocated copy of the data into `wq_`. The `buffer` inner struct owns a `unique_ptr<char[]>` so the caller's memory can be released immediately. The queue is protected by `mutex_`.

The write engine uses a deliberate two-queue rotation. `wq_` is the producer queue; `wq2_` is the in-flight queue. In `on_write()`, after a batch completes, `wq2_` is cleared, then atomically swapped with `wq_`. This means that while an async scatter-gather write is in progress over `wq2_`, producers can freely enqueue into `wq_` without contention. Only when a new write needs to start does the mutex matter — and even then, only for the moment needed to check whether the queues are empty and decide whether to post `on_write`.

The check `wq_.size() == 1 && wq2_.size() == 0` in `write()` detects the idle state: if this is the first item in `wq_` and `wq2_` is also empty, no write loop is running, so it must be kicked off. If either queue was already non-empty, an in-flight `on_write` will pick up the new data when it drains.

## Streaming Writer Path

The `write(shared_ptr<Writer>, bool)` overload handles large or lazily-generated responses. `do_writer()` runs as a second coroutine and calls `writer->prepare(bufferSize, resume)` in a loop. The `resume` callback, if invoked by the writer when data becomes available, re-enters `do_writer()` by spawning a new coroutine on the strand — allowing the writer to stall mid-response without blocking the strand itself. After the writer signals `complete()`, the connection either closes or re-enters `do_read()` depending on the `keep_alive` flag.

## Timeout Strategy

Timeouts are managed through Beast's built-in expiry mechanism on the lowest layer (`expires_after` / `expires_never`). Rather than maintaining a separate timer, the stream simply sets an expiry before every I/O operation and clears it after. A timed-out operation returns `boost::beast::error::timeout`, which is detected inline and routed to `on_timer()` → `fail()`.

Loopback connections receive a three-second timeout (`timeoutSecondsLocal`) versus thirty seconds for remote clients. This is an explicit optimization: in-process test environments like `Env` run over `127.0.0.1` and should not tolerate slow connections; cutting the timeout also makes stuck-test detection faster.

## Error Handling and Teardown

`fail()` is idempotent: it records only the first error in `ec_` and ignores subsequent calls, including `operation_aborted` (which fires when a pending async op is cancelled because the socket is being closed). This prevents double-close races. The final value of `ec_` is forwarded to `handler_.onClose()` in the destructor so the handler can distinguish clean closures from error-driven teardowns.

`close(bool graceful)` honours in-flight writes before closing. With `graceful = true` it sets `graceful_` and only proceeds to `do_close()` once `on_write()` finds both queues empty. With `graceful = false` it closes the socket immediately, which will cause any in-flight async operations to complete with `operation_aborted`, unwinding the call stack cleanly.

The strand-safety pattern is consistent throughout: any method callable from outside the strand checks `strand_.running_in_this_thread()` and posts to the strand if needed. This ensures all mutable state (`wq_`, `graceful_`, `complete_`, `message_`) is modified only from the strand, while the `mutex_` exclusively guards the cross-thread enqueue path into `wq_`.