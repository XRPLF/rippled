# RPCSub.h — Outbound Push Subscription for JSON-RPC

`RPCSub.h` defines the abstract interface for the XRPL server's outbound subscription mechanism: when a remote HTTP/HTTPS endpoint wants to *receive* ledger events pushed to it rather than polling, an `RPCSub` object is the subscription handle that drives that delivery. This is the "webhook" side of the XRPL subscription system.

## Context: where RPCSub fits in the subscription hierarchy

The broader subscription infrastructure revolves around `InfoSub` (defined in `include/xrpl/server/InfoSub.h`), which is the base class for all client subscription handles. `InfoSub` tracks which accounts, ledgers, order books, and transaction streams a client has subscribed to, manages a resource-consumption `Consumer`, and declares the single pure-virtual `send(Json::Value const&, bool)` method that each concrete subclass must implement to deliver events. WebSocket connections have their own `InfoSub` subclass; `RPCSub` is the subclass for outbound HTTP/HTTPS delivery.

`RPCSub` adds just two interface methods on top of `InfoSub`: `setUsername()` and `setPassword()`. These allow credentials for the remote endpoint to be updated after the subscription is created, which is why they are virtual rather than constructor parameters.

## Factory and implementation hiding

The header exposes no concrete class. The actual implementation, `RPCSubImp`, lives entirely in `src/xrpld/rpc/detail/RPCSub.cpp` and is never visible to callers. The `make_RPCSub()` factory function constructs and returns a `std::shared_ptr<RPCSub>` backed by `RPCSubImp`. This design keeps the implementation details — the deque, the lock, the connection state — out of the ABI and out of any callers' translation units.

One unexplained parameter is noted with a `VFALCO` comment directly in the header: `boost::asio::io_context& io_context`. The reason becomes clear in the implementation: `RPCCall::fromNetwork()` takes an `io_context` to schedule the async HTTP POST, so the subscription must hold a reference to it. The comment reflects that this coupling felt architecturally awkward to the original author.

## How event delivery works in RPCSubImp

The concrete `RPCSubImp` implementation has a straightforward producer-consumer design built around `mLock` (the mutex inherited from `InfoSub`), `mDeque` (a `std::deque<std::pair<int, Json::Value>>`), and a `mSending` boolean flag.

When `send()` is called — typically from the server's event-publishing path — it acquires the lock, appends the event together with a monotonically increasing sequence number (`mSeq++`) to the deque, and then, **only if no send job is already running**, enqueues a `jtCLIENT_SUBSCRIBE` job on the `JobQueue`. This lazy-start pattern means no thread is ever spinning or blocked when the queue is idle; the worker only exists while there is work to do.

The worker (`sendThread`) runs a drain loop: it acquires the lock, pops the front of the deque, copies the event out, releases the lock, and *then* calls `RPCCall::fromNetwork()` outside the lock. This is a deliberate separation: holding the lock during an outbound HTTP call would serialize all event producers against a potentially slow network endpoint. The sequence number is attached to the JSON object as `"seq"` just before sending, giving the remote endpoint a way to detect gaps or reordering.

If `fromNetwork` throws (network error, connection refused), the exception is caught and logged, and the loop continues draining. Events are never retried — if delivery fails, the event is silently dropped. The loop terminates when the deque is empty, at which point `mSending` is set to `false` under the lock, allowing the next `send()` call to start a fresh job.

## Construction-time validation

`RPCSubImp`'s constructor calls `parseUrl()` on the supplied URL string and throws `std::runtime_error` immediately if it is malformed or uses a scheme other than `http` or `https`. This fail-fast policy means that callers of `make_RPCSub` must be prepared to catch an exception — the subscription is never in a partially-constructed unusable state.

The `ServiceRegistry` parameter is used only for two things at construction: obtaining a `beast::Journal` for structured logging under the `"RPCSub"` category, and retrieving the global `Logs&` reference that `RPCCall::fromNetwork` requires for its own internal logging.

## Relationship to InfoSub::Source

`InfoSub::Source` (the inner class of `InfoSub`) carries three `RPCSub`-specific virtual methods: `findRpcSub`, `addRpcSub`, and `tryRemoveRpcSub`. The comment in `InfoSub.h` is candid: `// VFALCO TODO Remove — This was added for one particular partner`. These methods allow the source (typically the `NetworkOPs` implementation) to maintain a URL-keyed registry of active outbound subscriptions, so duplicate subscriptions to the same URL can be detected and deduplicated. `RPCSub` is thus a somewhat isolated feature within the subscription system, serving a narrow use case that was apparently added for a specific integration need and carries technical-debt markers throughout.