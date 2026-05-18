# `src/xrpld/rpc/Context.h` — RPC Call Context

## Role in the System

`Context.h` defines the dependency bundle that every RPC handler receives. Rather than threading individual subsystem references through every function signature, the XRPL RPC layer packages all shared execution state into a single aggregate struct and passes it as the handler's sole input for infrastructure concerns. This is the classic "context object" pattern, applied here at the boundary between the HTTP/gRPC transport layer and the per-command handler implementations.

## The Base `Context` Struct

`Context` holds everything a handler could need to interact with the running node:

- **`app`** — a reference to the top-level `Application` singleton, the root of the node's service graph (wallet, config, database, etc.).
- **`netOps`** — a `NetworkOPs` reference used to query network and consensus state, check operating mode, and submit transactions.
- **`ledgerMaster`** — exposes ledger history, the current open ledger, and validated ledger state.
- **`loadType`** — a `Resource::Charge` that the handler updates to signal how much resource cost the call should incur. It is populated by the caller before dispatch and read back after to assess usage fees, making it an *in-out* field despite being a plain reference.
- **`consumer`** — the `Resource::Consumer` record for the calling client, used by the resource management layer to track and enforce per-client rate limits.
- **`role`** — the `Role` enum value (`GUEST`, `USER`, `IDENTIFIED`, `ADMIN`, `PROXY`, or `FORBID`) resolved from port config and request credentials before the context is constructed. Handlers use it to gate admin-only operations.
- **`coro`** — an optional `shared_ptr` to a `JobQueue::Coro`. When set, this allows long-running handlers to yield execution back to the job queue cooperatively, avoiding thread starvation. Most handlers leave this null.
- **`infoSub`** — an optional `InfoSub::pointer` that, when set, represents an open WebSocket session. Subscription-oriented handlers (`subscribe`, `unsubscribe`) use it to register or deregister the session for event feeds.
- **`apiVersion`** — an unsigned integer encoding the client's requested API version. Handlers use this to shape response formats and error codes — for example, `conditionMet()` in `Handler.h` returns `rpcNO_NETWORK` on v1 but `rpcNOT_SYNCED` on later versions.
- **`j`** — a `beast::Journal` for structured logging. Stored by value (journals are cheap handles) rather than by pointer.

All members are non-owning references or lightweight handles. `Context` itself owns nothing and has no lifecycle implications — it is purely a view into resources owned elsewhere.

## Protocol-Specific Subtypes

### `JsonContext`

`JsonContext` extends `Context` for the JSON-RPC path (HTTP and WebSocket). It adds two fields:

- **`params`** — the parsed `Json::Value` carrying the request's parameter object.
- **`headers`** — a nested `Headers` struct holding `user` and `forwardedFor` as `std::string_view`. These are sourced from HTTP headers set by upstream proxies (e.g., a `secure_gateway` acting as an authenticating proxy). Using `string_view` here is intentional — the views refer into the longer-lived HTTP request buffer that exists for the duration of the call, avoiding a copy.

In `ServerHandler.cpp`, a `JsonContext` is aggregate-initialized with a brace-enclosed `Context` base followed by `params` and `headers`. The three-level initialization `{ {base...}, params, {user, fwdFor} }` mirrors the struct's inheritance layout exactly.

Every JSON-RPC handler signature is typed as `Status(JsonContext&, Json::Value&)` (see `Handler::Method` in `Handler.h`). The `JsonContext` is the primary dispatch vehicle; `Handler.h`'s `conditionMet<T>()` template accepts any `T` that exposes the `Context` fields, so it works transparently for both subtypes.

### `GRPCContext<RequestType>`

`GRPCContext` is a simple class template that extends `Context` with a single typed `params` field holding the decoded protobuf request object. The template parameter `RequestType` corresponds to the generated protobuf message type for each gRPC method (e.g., `org::xrpl::rpc::v1::GetLedgerRequest`). This keeps the gRPC handlers strongly typed while sharing all the infrastructure plumbing from the base `Context`.

## Design Observations

The split between `Context` and its two subtypes reflects a deliberate separation of transport concerns. The base struct captures everything that is protocol-neutral (node state, authorization, resource management), while the subtypes carry only what differs between JSON-over-HTTP and binary protobuf. This means infrastructure utilities like `conditionMet()` and `isUnlimited()` can be written against the base without needing to know about request parameters at all.

The choice to use references rather than pointers for `app`, `netOps`, and `ledgerMaster` is a deliberate invariant: a `Context` cannot be constructed with null subsystems. This is appropriate because context objects are always created in the hot path inside a running server — the subsystems are guaranteed live by the time any RPC call can arrive.