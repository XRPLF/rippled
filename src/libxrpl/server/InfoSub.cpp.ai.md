# `InfoSub.cpp` — Client Subscription State Manager

`InfoSub` represents a single connected client's view of the XRPL event stream. Its job is to act as the durable record of everything that client has subscribed to — ledger events, transactions, validator manifests, consensus rounds, individual accounts — and to ensure every one of those subscriptions is cleanly torn down when the connection closes. The `.cpp` file is compact because most architectural weight lives in the header; the implementation is primarily about lifecycle management and thread-safe mutation of subscription sets.

## The Subscription Identity Problem

Every `InfoSub` instance receives an immutable, process-unique 64-bit sequence number via `assign_id()`, stored as `mSeq`. The implementation uses a function-local `std::atomic<uint64_t>` that increments on each construction — simple, lock-free, and correct under concurrent connection setup. This number is the client's identity across all server-side subscription tables. When the server (the `Source`) needs to remove a subscriber it addresses it by this integer, never by pointer. That design isolates the server tables from the lifetime of the `InfoSub` object itself, which matters critically in the destructor.

## The Destructor's Two-Phase Unsubscription

The destructor is the most important method in the file, and its comment is the key to understanding it. For flat event streams — transactions, ledger, manifests, server status, validations, peer status, consensus — each `unsub*` call takes only the `mSeq` integer. These calls remove the entry from the server's lookup table and nothing else.

Account subscriptions are different. The `hash_set<AccountID>` members `realTimeSubscriptions_` and `normalSubscriptions_` are a mirror of the per-account subscription maps held by the `Source`. Under normal operation, when user code calls `unsubAccount`, the server removes the `InfoSub` from its maps *and* calls back to `deleteSubAccountInfo` to remove the account from `InfoSub`'s own sets. During destruction that callback would modify a container inside an already-dying object — harmless in practice, but architecturally wrong and potentially unsafe if subclass destructors have already run.

The solution is `unsubAccountInternal`, an overload on `Source` that takes the raw `mSeq` and the set by value. It only touches the server's data structures. The `InfoSub` destructor passes its own sets directly, skipping the callback entirely. The empty checks before calling `unsubAccountInternal` are a minor optimization: if no accounts were ever subscribed, the virtual dispatch and set iteration are avoided. Account history subscriptions use the same pattern via `unsubAccountHistoryInternal`, iterating one account at a time because each history subscription can be in a partially-completed state.

## Account Subscription Flavors

The class maintains three separate `hash_set<AccountID>` collections, each with a distinct semantic:

`realTimeSubscriptions_` — accounts whose transactions are delivered as they enter the network, before ledger close confirmation. The original comments note the "rt" naming was a historical artifact for "real time" meaning "unconfirmed."

`normalSubscriptions_` — confirmed transactions only, delivered after ledger close.

`accountHistorySubscriptions_` — a more exotic subscription type where the client also receives past transactions replayed from history. `insertSubAccountHistory` intentionally returns `bool` (whether the account was newly inserted) so callers can avoid redundant historical replay if a client double-subscribes.

All three mutation methods (`insertSubAccountInfo`, `deleteSubAccountInfo`, `insertSubAccountHistory`, `deleteSubAccountHistory`) take `mLock`, a `protected` `std::mutex`. Marking the lock `protected` rather than `private` lets concrete subclasses like `WSInfoSub` or `RPCSub` extend its protection scope if they need to make compound operations atomic.

## The `Source` Interface and Inversion of Control

`InfoSub::Source` is a pure abstract inner class defining the entire subscription API that the server must implement. This is a deliberate inversion: `InfoSub` holds a reference to its source, but the source (in practice, `NetworkOPs`) knows nothing about the concrete `InfoSub` subclass — it sees only `InfoSub::ref` (`shared_ptr<InfoSub> const&`). This decouples protocol logic from transport. The two known concrete subclasses in the codebase are `WSInfoSub`, which streams JSON over a WebSocket session held by weak pointer, and `RPCSub`, which serializes and delivers events to a remote HTTP callback URL via the job queue. Neither affects how the `Source` tracks subscriptions.

## Resource Tracking and API Versioning

`m_consumer` holds a `Resource::Consumer` handle that integrates with the server's load-shedding and rate-limiting framework. A client that floods the server with requests accrues charges; the consumer tracks a credit balance and signals when warnings or disconnection are warranted. The two-constructor design — one with and one without a `Consumer` — reflects that not all connection types participate in resource accounting (e.g., internal pseudo-clients).

`apiVersion_` starts at zero and `getApiVersion()` asserts it is positive before returning. This is a deliberate "fail fast" guard: if server-side setup code forgets to call `setApiVersion`, any downstream use of the version field will abort with a clear message rather than silently applying version-zero behavior. The `noexcept` on `getApiVersion` is notable — `XRPL_ASSERT` is expected to terminate the process rather than throw, so the `noexcept` contract holds even on assertion failure.

## Relation to `InfoSubRequest`

`InfoSub` optionally holds a `shared_ptr<InfoSubRequest>`, a separate abstract interface for "path find" style stateful requests that need to be notified of close or status queries. This is a narrow escape hatch for long-lived RPC requests that outlive a single handler invocation. `clearRequest()` / `setRequest()` / `getRequest()` form a simple optional-value pattern using `shared_ptr` null as the absent state.