# `include/xrpl/server/InfoSub.h`

## Role in the System

This header defines the core abstraction for client subscriptions in the XRPL server. When a client connects via WebSocket or JSON-RPC and issues a `subscribe` command, an `InfoSub` instance is created to represent that subscriber's ongoing session. The file defines three cooperating types: `InfoSubRequest`, `InfoSub`, and its inner `InfoSub::Source`. Together they implement a publish-subscribe interface between individual connected clients and the network event pipeline operated by `NetworkOPs`.

## `InfoSubRequest`: In-Flight Request Tracking

`InfoSubRequest` is a small abstract base class for requests that persist across multiple asynchronous callbacks — notably path-find operations, which stream progressive results until closed. It exposes `doClose()` to terminate the request and `doStatus()` to query its current state, both returning `Json::Value` for serialization directly to the client. Inheriting from `CountedObject<InfoSubRequest>` makes live instances visible in the node's diagnostic counters. The `shared_ptr<InfoSubRequest>` stored inside `InfoSub` via `setRequest()` ensures the in-flight computation is tied to the subscriber's lifetime.

## `InfoSub`: The Subscriber Object

`InfoSub` represents a single connected client endpoint. Its design centers on three responsibilities: maintaining a unique identity, tracking which feeds the client has subscribed to, and enforcing resource consumption limits.

**Identity via atomic sequence number.** The private `assign_id()` function increments a `static std::atomic<uint64_t>` and assigns the result to `mSeq` at construction time. This integer ID serves as the stable, non-owning identifier used in all "Internal" unsubscription calls during destruction, where passing a `shared_ptr` would be unsafe (the object is being torn down). The public `getSeq()` accessor exposes this to the `Source`.

**Subscription bookkeeping on the subscriber side.** `InfoSub` maintains three `hash_set<AccountID>` members: `realTimeSubscriptions_` (transactions as they are applied, pre-consensus), `normalSubscriptions_` (validated/confirmed transactions), and `accountHistorySubscriptions_` (historical replay feeds). The methods `insertSubAccountInfo`, `deleteSubAccountInfo`, `insertSubAccountHistory`, and `deleteSubAccountHistory` mutate these sets; the destructor uses them to call back into the source for automatic cleanup. All mutations are protected by the `protected` `std::mutex mLock`.

**Resource management.** Each `InfoSub` holds a `Resource::Consumer` — the rate-limiting token from the resource subsystem. Callers obtain it via `getConsumer()` and can charge load fees (`charge()`), check `disposition()` to decide whether to warn or disconnect, and call `disconnect()` if the client is misbehaving.

**API versioning.** The `apiVersion_` field stores the API version negotiated at connection time. It starts at 0 and `getApiVersion()` asserts (via `XRPL_ASSERT`) that it is greater than zero before returning, catching cases where the version was never initialized. This drives downstream logic in the `send()` dispatch path so serialization can adapt to different client expectations.

**The pure virtual `send()`.** The abstract `send(Json::Value const& jvObj, bool broadcast)` is the outbound delivery point. Subclasses implement it to push the JSON payload over their specific transport. `RPCSub` (defined in `src/xrpld/rpc/RPCSub.h`) handles HTTP callback delivery, while the WebSocket subclass handles framed WebSocket writes. The `broadcast` flag signals whether the message is being multicasted to many subscribers simultaneously, which may affect buffering behavior.

## `InfoSub::Source`: The Publisher Interface

`Source` is a pure abstract inner class that `InfoSub` depends on but doesn't implement. In practice, `NetworkOPs` is the sole implementation — it owns the authoritative maps that associate feed types (ledger, book, validation, peer status, consensus, transactions, manifests) to the set of current subscribers.

Every subscription method comes in two forms that reveal an important design split:

- **Normal-operation form**: accepts `ref ispListener` (i.e., `shared_ptr<InfoSub> const&`). This is called while the subscriber is alive, so it can be inserted into or removed from the source's internal maps by shared pointer.
- **"Internal" destruction form**: accepts `std::uint64_t uListener` (the raw `mSeq`). This is called exclusively from `InfoSub::~InfoSub()`, where passing a `shared_ptr` would be unsafe because the object's control block is being unwound. The destructor iterates its local subscription sets and calls these internal variants to remove itself from the `NetworkOPs` maps without risking a dangling reference.

This two-level unsubscription API is the key correctness invariant of the design. Normal `unsubXxx` methods clean up on both the `InfoSub` side and the `Source` side. The destructor only needs to clean up the `Source` side (the `InfoSub`'s own sets are about to be destroyed anyway), hence the `unsubXxxInternal` variants that bypass the subscriber's state.

**Account history subscriptions** add a third mode of operation: `subAccountHistory()` streams past transactions to a client and returns an error code. `unsubAccountHistory()` accepts a `historyOnly` flag — when `true`, the client stops receiving historical replay data but continues receiving new confirmed transactions. This graduated exit supports the common usage pattern where clients first replay account history then transition to a live feed.

**URL-based RPC subscriptions.** The `findRpcSub/addRpcSub/tryRemoveRpcSub` trio manages a secondary index keyed by URL string. The inline comment acknowledges this as a legacy feature ("added for one particular partner") that should eventually be removed. It enables server-push semantics over plain HTTP callbacks rather than WebSocket.

## Relationship to Sibling Files

`InfoSub` forms the bridge between the server transport layer (`Session`, `WSSession`) and the application event pipeline (`NetworkOPs`). `NetworkOPs.h` declares the concrete `Source` implementation. `RPCSub.h` shows the only concrete `InfoSub` subclass visible at the library boundary — it adds `setUsername`/`setPassword` for authenticated callback delivery. The `CountedObject` mixin connects both `InfoSubRequest` and `InfoSub` to the global instance-count diagnostics exposed via `CountedObjects::getCounts()`.