# `include/xrpl/server/Writer.h`

## Role in the System

`Writer` is a pure abstract interface that decouples HTTP response data *production* from data *transmission* in the XRPL server layer. It exists to enable streaming and asynchronous response generation: the server can begin draining a `Writer` over a socket without requiring the full response to be materialized in memory first.

The interface sits at the boundary between the network I/O layer (`BaseHTTPPeer`, `Session`) and application-level response handlers. A handler constructs a `Writer` implementation (or receives one in a `Handoff`), hands it to the session via `Session::write(std::shared_ptr<Writer>, bool keep_alive)`, and the session drives it to completion asynchronously.

## The Four-Method Contract

The four virtual methods implement a **pull-based streaming protocol** between the I/O peer and the response source:

- **`complete()`** — Signals that the writer has no remaining data to emit. The I/O loop checks this after each consume cycle to know when to stop.

- **`prepare(bytes, resume)`** — Asks the writer to make at least `bytes` bytes available in its internal buffer. If the data is already ready, it returns `true` immediately. If more data must be generated asynchronously (e.g., waiting on a database query or a chunked encoder), it stores the `resume` functor and returns `false`. The I/O coroutine interprets `false` as a suspension point and exits; the `resume` callback re-enters it when the writer is ready.

- **`data()`** — Returns the current ready bytes as a `std::vector<boost::asio::const_buffer>`. Because Boost.Asio `const_buffer` is a non-owning view, this avoids copying: the vector holds pointers into the writer's own storage.

- **`consume(bytes)`** — Advances the writer's internal read pointer by the number of bytes actually sent by the network layer. This mirrors the Boost.Asio dynamic buffer convention (`prepare`/`commit`/`consume`) adapted for the cross-layer ownership model here.

## How the I/O Peer Drives the Writer

`BaseHTTPPeer<Handler, Impl>::do_writer()` is the coroutine that consumes a `Writer`. It runs on a Boost.Asio `strand` inside a stackful coroutine (`yield_context`):

```
for (;;) {
    if (!writer->prepare(bufferSize, resume))
        return;          // suspend; resume() will re-enter this coroutine
    async_write(stream, writer->data(), transfer_at_least(1), do_yield[ec]);
    writer->consume(bytes_transferred);
    if (writer->complete())
        break;
}
```

The `resume` functor captured inside `do_writer()` is a lambda that calls `util::spawn(strand_, ...)` to re-post the entire `do_writer` coroutine back onto the strand. This means the writer can produce data from any thread or async callback; it just calls `resume()` and the I/O peer picks up exactly where it left off. There is no shared mutable state between the writer and the peer during the suspension window — the writer holds its data, and the peer holds a `shared_ptr<Writer>` keeping it alive.

## `SimpleWriter`: The Synchronous Case

`SimpleWriter` (`SimpleWriter.h`) is the only concrete implementation in this header directory. Its constructor eagerly serializes a complete Boost.Beast HTTP message into a `multi_buffer`. Because all data is available immediately, its `prepare()` unconditionally returns `true`, ignoring the `resume` functor entirely. `data()` fans the `multi_buffer` segments out into `const_buffer` views, and `consume()` delegates to `multi_buffer::consume()`. It is marked **deprecated** — presumably because eagerly serializing the full response to a `multi_buffer` defeats the incremental-production purpose of the `Writer` abstraction and is wasteful for large bodies.

## Integration via `Handoff`

`Handoff` (`Handoff.h`) carries a `std::shared_ptr<Writer>` as its `response` field. When the server's connection-acceptance logic decides to respond without upgrading to WebSocket, it populates `Handoff::response` and the `BaseHTTPPeer` calls `write(handoff.response, handoff.keep_alive)`, which spawns `do_writer`. This lets protocol-level handlers (e.g., the upgrade negotiation path) produce responses without knowing anything about the socket or I/O model.

## Design Rationale

The `resume` functor approach is notable: rather than imposing a particular async model (promises, futures, coroutines) on writer implementations, it passes a plain `std::function<void()>`. Any implementation — whether producer-side threaded, Asio-based, or even synchronous — can call `resume()` at any time to re-activate the draining loop. The trade-off is that the writer must correctly call `resume()` exactly once per suspension; misuse (calling it zero times, or twice) would either stall the connection or cause a coroutine re-entry race. This contract is implicit and unguarded by the interface, placing correctness responsibility entirely on implementors.