# `PathFind.cpp` — RPC Handler for Persistent Pathfinding Subscriptions

## Role in the System

This file implements `doPathFind`, the RPC entry point for the XRPL `path_find` command. Unlike `ripple_path_find` (which answers synchronously), `path_find` is a **subscription-oriented** command that pushes repeated payment-path updates to a connected client as ledger state evolves. Its small size belies its architectural significance: it sits at the boundary between the stateless RPC dispatch layer and the stateful, asynchronous `PathRequestManager` subsystem.

## Why a Subscription Model

Payment paths on XRPL are affected by every ledger close — order books shift, trust lines change, liquidity moves. A static one-shot answer ages quickly. The `path_find` API reflects this by allowing a client to register a long-lived request; the server then re-evaluates paths after each ledger and pushes updated results through the same WebSocket connection. This is why the handler unconditionally rejects calls that arrive without an active `infoSub` subscription object (`rpcNO_EVENTS`): HTTP connections have no persistent channel to push updates through, so the feature only makes sense over WebSocket.

## The Three Subcommands

The handler is a dispatcher over three subcommands:

**`create`** initializes a new path-find request for the current subscriber. The handler first assigns `Resource::feeHeavyBurdenRPC` to `context.loadType`, reflecting the computational cost of path searches (they traverse order books and credit lines across the full ledger graph). It then calls `context.infoSub->clearRequest()` to discard any prior active request on this connection before delegating to `PathRequestManager::makePathRequest()`. This replace-not-accumulate design means a client always has at most one active path-find per WebSocket connection.

**`close`** tears down the current path-find request. The handler retrieves the `InfoSubRequest` pointer via `context.infoSub->getRequest()` and returns `rpcNO_PF_REQUEST` if none exists, then calls `doClose()` on the `PathRequest` object and clears the reference in `infoSub`. No resource charging occurs here — the cost was borne at creation.

**`status`** is a lightweight query that returns the most recently computed path result without triggering a new computation. It also checks for an active request and delegates to `PathRequest::doStatus()`.

An unrecognised subcommand string falls through all three checks and returns `rpcINVALID_PARAMS`.

## Validation Layering

The validations are intentionally ordered from cheapest to most expensive:

1. **Config gate** — `PATH_SEARCH_MAX == 0` rejects immediately if pathfinding is administratively disabled on this node, before any parameter parsing occurs.
2. **Parameter presence and type** — the `subcommand` field must exist and be a JSON string; otherwise `rpcINVALID_PARAMS`.
3. **Subscription presence** — `context.infoSub` is checked before any subscription method is called; its absence returns `rpcNO_EVENTS`.
4. **Active request presence** — for `close` and `status`, the existence of a live `InfoSubRequest` on the subscription object is verified; its absence returns `rpcNO_PF_REQUEST`.

## Key Types and Relationships

`PathRequest` inherits from `InfoSubRequest`, the abstract base in `InfoSub.h` that defines the `doClose()` and `doStatus()` interface. `InfoSub` holds exactly one `std::shared_ptr<InfoSubRequest>` at a time, managed via `setRequest()`, `getRequest()`, and `clearRequest()`. This single-slot design ensures the subscription can only track one active pathfinding session, keeping lifetime management straightforward.

`PathRequestManager` owns a `std::vector<PathRequest::wptr>` — weak pointers that are promoted during its periodic `updateAll()` pass. This lets the manager enumerate all living requests without extending their lifetimes: if the owning `InfoSub` is destroyed (WebSocket disconnected), the corresponding `PathRequest` expires automatically, and `updateAll()` silently skips the stale entry.

The `doPathFind` handler acquires the closed ledger via `context.ledgerMaster.getClosedLedger()` and passes it to `makePathRequest()`. Using the closed (validated) ledger rather than the current open ledger ensures path computations reflect a stable, consistent view of the network state.

## Architectural Notes

The heavy-burden charge on `create` but not on `close` or `status` is a deliberate rate-limiting decision. Path computation is expensive; reading back a cached result is cheap. Charging only at initialization prevents abuse without penalising normal use.

The `context.infoSub->setApiVersion(context.apiVersion)` call on every invocation (before the subcommand dispatch) propagates the client's negotiated API version into the subscription object. This matters for serialization: `PathRequest::doUpdate()` uses this version when formatting the pushed updates, so a client that negotiated v2 always receives v2-formatted path results regardless of when the update fires.