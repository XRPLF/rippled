# `ServerHandler.h` — RPC and WebSocket Request Gateway

`ServerHandler` is the central hub that bridges the XRPL node's low-level TCP/TLS server with the RPC and WebSocket processing pipeline. It owns the `Server` object (the Boost.Beast–backed acceptor layer), enforces per-port connection limits, routes incoming connections to either the overlay peer protocol or the JSON-RPC subsystem, and dispatches all request work to the `JobQueue` as coroutines.

## Construction and the `ServerHandlerCreator` Token Pattern

`ServerHandler` cannot be constructed directly by arbitrary code. Its constructor requires a `ServerHandlerCreator` value — a private type with no public constructor. The only code that can create such a value is the `make_ServerHandler()` factory function, which is declared as a friend. This is a deliberate capability-key idiom: the constructor must be `public` so that `std::make_unique` can call it, yet the private token type makes it impossible for any caller outside the factory to satisfy the constructor's signature. This avoids the pitfalls of private constructors combined with `make_unique` (which would require additional `friend` declarations in standard library internals).

## `Setup` and Configuration

`ServerHandler::Setup` is a plain data structure populated from `rippled.cfg` by `setup_ServerHandler()`. It holds a vector of `Port` descriptors (each Port carries its name, bind address, protocols, TLS context, and optional user/password), a `client_t` sub-struct for when the process makes outgoing RPC calls to itself or another node (with optional TLS and credentials), and a `boost::asio::ip::tcp::endpoint` for the overlay peer listener. `makeContexts()` — called on the `Setup` after parsing — instantiates the SSL contexts for any port that advertises `https`, `wss`, or similar protocols.

The `operator<(Port, Port)` free function defined at the top of the file orders `Port` objects by name; this allows `std::reference_wrapper<Port const>` keys to work in `count_`, the `std::map` that tracks live connection counts per port.

## The Handler Concept and Session Lifecycle

`make_Server<Handler>()` (in `Server.h`) is a template factory that wraps any type satisfying a duck-typed handler concept inside a `ServerImpl`. `ServerHandler` fills that concept by exposing six callbacks:

- **`onAccept()`** — Called for every new TCP connection. Atomically increments the port's connection count and rejects connections once the configured limit is reached. The mutex protects `count_` because accepts can fire on multiple I/O threads.

- **`onHandoff()`** — The routing decision point. If the HTTP request carries a WebSocket upgrade header, the session is promoted via `session.websocketUpgrade()`, a `WSInfoSub` subscriber object is attached to `WSSession::appDefined`, and the handoff is marked `moved`. If the port carries the `peer` protocol and an SSL bundle is present, the connection is forwarded to `app_.getOverlay()`. A bare GET `/` on a WebSocket port is answered immediately with `statusResponse()`. Otherwise an empty `Handoff` is returned, which signals the server to proceed to `onRequest()`.

- **`onRequest()`** — Handles HTTP RPC. After checking that the port has the `http`/`https` protocol and that the Basic-Auth header matches the port's credentials, the session is *detached* (preventing the server from closing it while processing is in flight) and a coroutine of job type `jtCLIENT_RPC` is posted to the `JobQueue`. If the queue rejects the post (i.e., the node is shutting down), a 503 is sent immediately and the session is closed.

- **`onWSMessage()`** — Called for each WebSocket text frame. JSON parsing happens inline on the I/O thread (fast path); if the parse fails or the request exceeds `RPC::Tuning::maxRequestSize`, an error is sent back without touching the job queue. Valid messages are dispatched as `jtCLIENT_WEBSOCKET` coroutines; if the queue is full, the session is closed with a `going_away` close frame.

- **`onClose()`** — Decrements the per-port counter under lock, mirroring `onAccept()`.

- **`onStopped()`** — Called by the server once all connections have been closed. Sets `stopped_ = true` and notifies the `condition_variable` so that any thread blocked in `stop()` can proceed.

## Shutdown Synchronization

`stop()` calls `m_server->close()` — which triggers the asynchronous teardown of all listeners and sessions — then blocks on `condition_.wait()` until `onStopped()` fires. This two-phase shutdown guarantees that callers of `stop()` do not return prematurely while in-flight I/O is still referencing `ServerHandler` members. The `mutex_` / `condition_variable` pair is reused for both the connection-count tracking and the stop notification, keeping the synchronization surface small.

## `processRequest()`: HTTP RPC Engine

The private `processRequest()` function is the workhorse for HTTP JSON-RPC. It handles both single and batch requests: a top-level object with `method == "batch"` is treated as a batch, iterating over `params` as an array of individual calls and returning a JSON array of results. This design lets clients amortize round-trip latency for multiple ledger queries.

For each request in the loop, `processRequest()`:
1. Resolves the API version (with fallback to `apiVersionIfUnspecified`).
2. Determines the required `Role` for the method via `RPC::roleRequired()` and the caller's actual `Role` via `requestRole()` (which checks IP ranges, admin credentials, and secure-gateway headers).
3. Allocates a `Resource::Consumer` — unlimited for admin roles, metered for everything else. If the consumer is already disconnected due to load, a 503 (or per-item error in batch mode) is returned without executing the command.
4. Constructs an `RPC::JsonContext` and calls `RPC::doCommand()`.
5. Formats the response according to the `ripplerpc` protocol version: v2+ separates success/error at the envelope level; v1 always places results under `result`.
6. Masks sensitive fields (`passphrase`, `secret`, `seed`, `seed_hex`) in error responses before they are written back to the client.

The HTTP status code is also version-gated: `ripplerpc >= "3.0"` maps ledger error codes to appropriate HTTP statuses via `RPC::error_code_http_status()`; earlier versions always return 200.

## WebSocket Session Processing

`processSession(WSSession&, ...)` retrieves the `WSInfoSub` stored in `WSSession::appDefined`, checks the resource consumer's disconnect threshold, validates command presence, resolves the role, and calls `RPC::doCommand()`. The response format for WebSocket differs slightly from HTTP: `id`, `jsonrpc`, `ripplerpc`, and `api_version` fields from the request are echoed in the response, and a `type: response` field is always included. Sensitive keys in error responses are masked by the same `<masked>` replacement logic as the HTTP path.

## Metrics

Three metrics are registered against the `"rpc"` group of the `CollectorManager`: `rpc_requests_` (a counter incremented per request), `rpc_size_` (an event recording payload bytes), and `rpc_time_` (an event recording processing duration). Request duration is also logged at varying severity — `debug` for sub-second, `warn` for ≥1 s, `error` for ≥10 s — to make slow RPC calls observable without configuration.