# RPCSub.cpp — Outbound HTTP(S) Push Subscriptions

## Role in the System

`RPCSub.cpp` implements the XRPL server's ability to *push* subscription events to a remote HTTP or HTTPS endpoint. Where the normal subscription model delivers events over open WebSocket connections back to a connected client, this mechanism works in the opposite direction: the server initiates an outbound HTTP POST carrying the event payload as a JSON-RPC call. A comment in `InfoSub.h` notes it was "added for one particular partner" as a push-to-URL integration — which explains why the feature has a somewhat isolated, specialized character in an otherwise general-purpose RPC layer.

## Class Structure and Visibility

The implementation uses a deliberate PIMPL-like pattern. The header `RPCSub.h` exposes only the abstract base class `RPCSub` (itself extending `InfoSub`) and the `make_RPCSub()` factory function. The concrete implementation, `RPCSubImp`, lives entirely within this `.cpp` file and is never visible outside the translation unit. Callers hold `std::shared_ptr<RPCSub>` and never see any of the internal deque, lock, or connection state. This keeps the private internals out of the ABI entirely.

The inheritance chain is `RPCSubImp` → `RPCSub` → `InfoSub`. `InfoSub` is the framework base for all subscription objects — it manages subscription sets for accounts, ledgers, order books, and similar feeds, and provides the protected `mLock` mutex. `RPCSub` adds only the `setUsername()` and `setPassword()` virtual interface for credential management. `RPCSubImp` provides the concrete `send()` implementation that is the entire reason this class exists.

## URL Validation at Construction Time

The constructor eagerly validates and decomposes the target URL rather than deferring any of this work. `parseUrl()` is called immediately; a failure throws `std::runtime_error("Failed to parse url.")`. The scheme is then manually checked: `https` sets `mSSL = true`, anything other than `http` or `https` throws `std::runtime_error("Only http and https is supported.")`. If no port is present in the URL, it defaults to 443 for HTTPS or 80 for HTTP. This fail-fast approach means the `RPCSubImp` object either comes out of construction fully ready to use or doesn't exist at all — there is no partially-initialized state.

## Asynchronous Dispatch Without a Dedicated Thread

The design of `send()` and `sendThread()` is the most interesting aspect of the implementation. Rather than maintaining a persistent background thread per subscription, it uses the server's shared `JobQueue` on demand.

`send()` acquires `mLock`, pushes a `(sequence_number, json_value)` pair onto `mDeque`, and then checks the `mSending` flag. If no send job is currently active, it submits a `jtCLIENT_SUBSCRIBE` job to the `JobQueue` which will call `sendThread()` on a worker thread. If `mSending` is already true, the item is simply queued and the existing job will pick it up — no second job is spawned. This single-worker-per-subscription invariant avoids ordering problems that would arise if two concurrent jobs raced to drain the same deque.

`sendThread()` runs a `do...while` drain loop. Each iteration takes the lock only long enough to pop one event from the front of the deque (or detect that it is empty and clear `mSending`). The actual HTTP call to `RPCCall::fromNetwork()` is made *outside* the lock. This is deliberate and important: `fromNetwork()` performs a synchronous network operation on the io_context, which could block for an arbitrarily long time. Holding the lock across that call would prevent `send()` from enqueuing new events for the entire duration of the round-trip.

Each event receives a monotonically increasing `seq` field injected by `sendThread()` before the call. The remote receiver can use this to detect dropped or reordered deliveries.

## Concurrency Subtlety with Credentials

`mIp`, `mPort`, `mPath`, and `mSSL` are set once in the constructor and never mutated, making them safe to read from `sendThread()` without the lock. However, `mUsername` and `mPassword` can be updated at any time via `setUsername()` and `setPassword()`, both of which correctly acquire `mLock` before mutating those fields. The read of `mUsername` and `mPassword` in `sendThread()` happens outside the lock, creating a potential data race under the C++ memory model. In practice, string assignment is unlikely to cause observable corruption, and credential updates are an uncommon operation, but this is a real inconsistency in the locking discipline that the existing `XXX` comment nearby hints at.

## Relationship to RPCCall::fromNetwork

`RPCCall::fromNetwork()` takes a `boost::asio::io_context&` to schedule the outbound HTTP request. This is why `RPCSubImp` stores and passes through the io_context — a coupling that the header author found architecturally uncomfortable enough to annotate with `// VFALCO Why is the io_context needed?`. The answer lies in how `fromNetwork` works: it creates a Boost.Asio async HTTP session that must be associated with a running io_context. The subscription does not own the io_context; it merely borrows a reference to the one managed by the server's network layer.

The call to `fromNetwork()` passes `"event"` as the method name and `true` for the `quiet` flag, suppressing verbose logging on the call site. No response callback is registered, so the send is effectively fire-and-forget: if the remote endpoint rejects the call or returns an error, the exception is caught and logged, but no retry or backpressure mechanism exists.