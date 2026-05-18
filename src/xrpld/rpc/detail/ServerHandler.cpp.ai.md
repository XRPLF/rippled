# ServerHandler.cpp

`ServerHandler` is the central dispatcher that bridges the XRPL node's HTTP and WebSocket server infrastructure to its RPC execution layer. Every external client connection — whether a curl call, a WebSocket subscription, or a load balancer health probe — flows through this class before any RPC command is touched.

## Architectural Role

The file sits at the boundary between `xrpl::Server` (the low-level Boost.Beast HTTP/WebSocket server) and `RPC::doCommand` (the command dispatch table). `ServerHandler` implements the `Server`'s callback interface (`onAccept`, `onHandoff`, `onRequest`, `onWSMessage`, `onClose`, `onStopped`) and is responsible for authentication, rate-limiting, protocol routing, JSON parsing, role resolution, and metric collection — all before a single RPC handler sees the request.

## Construction and the `ServerHandlerCreator` Pattern

The constructor is deliberately public, because `std::make_unique` needs public access. But direct construction outside of `make_ServerHandler()` is prevented by requiring a `ServerHandlerCreator` argument — a private nested type only `make_ServerHandler` can name (declared a friend). This token idiom grants controlled construction through RAII factory while remaining compatible with `std::make_unique`.

During construction the handler registers three metrics with the `CollectorManager`: an RPC request counter, a response-size event, and a response-time event. These feed into whatever stats backend the node is configured to report to.

## Port Setup and Auto-Port Detection

`setup()` calls `m_server->ports()`, which actually binds the listening sockets, then iterates the resulting `endpoints_` map to back-fill any port configured as `0` (OS-assigned). The same pass identifies the first HTTP/HTTPS port as the `client` endpoint and the first `peer` port as the `overlay` endpoint. This is important for the local RPC client and for the overlay to know which address to advertise.

`parse_Ports()` reads the `[server]` config section, iterates named port subsections, converts each `ParsedPort` into a `Port` via `to_Port()`, and enforces constraints: exactly zero or one `peer` protocol is allowed in cluster mode; standalone mode strips all `peer` entries. The gRPC port (`SECTION_PORT_GRPC`) is intentionally skipped here — it is parsed by a separate `GRPCServer` class.

## Connection Lifecycle

### Accept Phase — `onAccept()`

Called on every new TCP connection before any HTTP is read. It atomically increments `count_[port]` under `mutex_` and rejects the connection immediately if the port's `limit` is exceeded. `onClose()` symmetrically decrements the counter, so the limit is enforced as a live ceiling rather than a watermark.

### Handoff Phase — `onHandoff()`

This is the first point where the actual HTTP request is examined. The method handles three distinct cases:

1. **WebSocket upgrade**: If the request carries an `Upgrade: websocket` header, `session.websocketUpgrade()` is called to obtain a `WSSession`. A `WSInfoSub` is created as the session's `appDefined` payload, its `Resource::Consumer` is initialized with a GUEST role for the purpose of connection accounting, and `ws->run()` is kicked off. The session is marked `moved`; the HTTP layer is finished with it.

2. **Peer overlay**: If the port carries the `peer` protocol and a raw TLS stream bundle was handed in, the entire connection is forwarded to `app_.getOverlay().onHandoff()`. This is how inbound peer-to-peer connections are routed to the overlay subsystem without going through the RPC path at all.

3. **Status request**: A plain HTTP `GET /` on a WebSocket-capable port receives a health-check response from `statusResponse()`. If `app_.serverOkay()` returns true the response is HTTP 200 with a brief HTML body; otherwise it is HTTP 500 with a reason string. Load balancers use this endpoint to decide whether to send traffic to the node.

If the request matches none of these cases an empty `Handoff` is returned, signalling to the server framework that the request should proceed to `onRequest()`.

## HTTP RPC Processing — `onRequest()`

`onRequest()` runs on the I/O thread. It performs two fast checks: that the port's protocol set includes `http` or `https`, and that `authorized()` accepts the credentials. Both failures return HTTP 403. `authorized()` parses HTTP Basic Auth by extracting the base64 payload, decoding it with `xrpl::base64_decode`, splitting at `:`, and comparing against the port's configured `user`/`password`. When no credentials are configured the function short-circuits to `true`, making auth opt-in per port.

If both checks pass, `session.detach()` promotes the session to a `shared_ptr` and `m_jobQueue.postCoro(jtCLIENT_RPC, ...)` schedules `processSession(Session)` as a coroutine job. Detaching is the key step: it moves ownership off the I/O thread so the coroutine can yield inside `RPC::doCommand` without stalling accept handling for other connections. If the job queue rejects the post (typically during shutdown), a 503 is returned and the session is closed immediately.

## WebSocket RPC Processing — `onWSMessage()`

Incoming WebSocket frames are assembled into a `Json::Value`. Oversized or unparseable messages receive an inline `jsonInvalid` error response without touching the job queue. Valid messages are dispatched as `jtCLIENT_WEBSOCKET` coroutines. The `WSInfoSub` (stored as `session->appDefined`) carries the consumer and user identity; `processSession(WSSession)` checks `consumer.disconnect()` first, returning `rpcSLOW_DOWN` if the endpoint has been throttled.

## The `processRequest()` Pipeline

This is the most involved function in the file — roughly 400 lines covering both single and batch HTTP RPC calls.

**Batch mode** is triggered when `method == "batch"` with a `params` array. The outer loop iterates each sub-request independently, accumulating results into a JSON array. Errors in individual sub-requests do not abort the batch; they are appended with an inline `error` object using JSON-RPC 2.0-style numeric codes (`method_not_found = -32601`, `server_overloaded = -32604`, etc.).

For each request (batch element or singleton), the pipeline proceeds:
1. **API version resolution** — `RPC::getAPIVersionNumber` extracts the requested API version from `params[0]` or, for batch, from the top-level object. Invalid versions generate either an HTTP 400 or a per-element error.
2. **Role determination** — `RPC::roleRequired` maps the method name to its minimum required role, then `requestRole` evaluates the client's actual role based on IP network membership and port configuration (admin nets, secure_gateway nets, etc.).
3. **Resource consumer acquisition** — Unlimited roles get `newUnlimitedEndpoint`; all others get `newInboundEndpoint`. If `consumer.disconnect()` is true the request is rejected as overloaded.
4. **Header trust revocation** — If the role is neither `IDENTIFIED` nor `PROXY`, the `forwardedFor` and `user` string views are zeroed out. This ensures that headers like `X-Forwarded-For` and `X-User` cannot be trusted when they arrive from a non-gateway source.
5. **Command dispatch** — `RPC::doCommand(context, result)` is called inside a try/catch. Exceptions produce `rpcINTERNAL` errors (marked `LCOV_EXCL` since they indicate bugs rather than normal failure paths).
6. **Response formatting** — The format diverges on `ripplerpc` version: 2.0+ uses a structured `{error: {code, message}}` shape; earlier versions nest results under `result` and echo back the request on errors. Starting at version 3.0, the HTTP status code is derived from the RPC error code via `RPC::error_code_http_status`; older versions always return HTTP 200.
7. **Credential masking** — When echoing the request back in an error response, fields `passphrase`, `secret`, `seed`, and `seed_hex` are replaced with `"<masked>"` to prevent credentials from leaking into error logs.

## Duration Logging

`logDuration()` is a templated helper that applies tiered severity: debug below 1 second, warning at 1–10 seconds, and error above 10 seconds. The thresholds reflect the expectation that well-formed RPC calls should complete in well under a second; anything past 1 second indicates contention or a ledger under heavy load.

## Shutdown Coordination

`stop()` calls `m_server->close()` then blocks on `condition_.wait()` until `stopped_` is set. `onStopped()` is the server's final callback after all connections have drained, at which point it sets `stopped_` and notifies the waiting thread. The destructor sets `m_server = nullptr` to release the `unique_ptr<Server>`, which triggers the server's own destruction. This ensures clean teardown ordering: server closes first, handler waits, handler is then destroyed.