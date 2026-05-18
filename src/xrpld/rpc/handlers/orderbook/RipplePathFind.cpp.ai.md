# `RipplePathFind.cpp` — Deprecated One-Shot Pathfinding RPC Handler

This file implements `doRipplePathFind`, the handler for the `ripple_path_find` JSON-RPC method. The comment at line 12 is blunt: *"This interface is deprecated."* The successor is `path_find` (implemented in `PathFind.cpp` in the same directory), which uses a WebSocket subscription model to push continuous updates. `ripple_path_find` remains for backwards compatibility — it is a single synchronous-looking RPC call that finds one or more payment paths between two parties and returns the result in the same HTTP response.

## Two Completely Different Execution Paths

The function branches sharply on whether the caller supplied a specific ledger (`ledger`, `ledger_index`, or `ledger_hash` in the request params). This is not just an implementation detail — it reflects two entirely different semantics clients can request.

### Default Path (No Ledger Specified)

When no ledger is pinned, the handler trusts the network's latest validated state and dispatches to the live path-finding engine via `PathRequestManager::makeLegacyPathRequest()`. This path is only available in networked (non-standalone) mode. Before going further, it checks that the validated ledger is not too stale: if `getValidatedLedgerAge()` exceeds `RPC::Tuning::maxValidatedLedgerAge`, the call is rejected with `rpcNO_NETWORK` (API v1) or `rpcNOT_SYNCED` (API v2+), a version-gated error distinction introduced to give cleaner semantics to newer clients.

The execution in this branch is asynchronous but appears synchronous to the caller. The `doRipplePathFind` function runs inside a `JobQueue::Coro` — a cooperative coroutine on the server's job queue thread pool. Here is the thread choreography documented at length in the source itself:

1. `makeLegacyPathRequest()` enqueues a path-finding job and receives a `PathRequest::pointer` back. It also captures a *completion lambda* that will fire when path-finding finishes.
2. If the job was accepted (`request` is non-null), the coroutine calls `context.coro->yield()` — suspending itself and releasing its thread back to the job queue.
3. The path-finding job runs on (potentially) a different job queue thread. Upon completion it invokes the completion lambda.
4. The lambda calls `coroCopy->post()` to re-enqueue this coroutine for execution. If `post()` fails (i.e., the job queue is shutting down and rejecting new work), it falls back to `coroCopy->resume()` — running the coroutine to completion on the *current* thread rather than letting the application hang on shutdown.
5. The coroutine resumes after the `yield()` call and calls `request->doStatus()` to retrieve the completed path-finding result.

The `coroCopy` variable in the lambda is a deliberate design choice: capturing `context.coro` by value as a `shared_ptr` keeps the coroutine object alive for the duration of the lambda, preventing the storage from being destroyed between `resume()` returning and the lambda returning. This is a subtle lifetime hazard that the code explicitly calls out.

### Ledger-Specified Path (Synchronous, Historical)

When the caller pins a specific ledger, the handler is fully synchronous and runs directly without involving coroutines. It calls `RPC::lookupLedger()` to resolve the ledger and then constructs an `RPC::LegacyPathFind` guard before invoking `PathRequestManager::doLegacyPathRequest()`.

`LegacyPathFind` is a RAII concurrency throttle defined in `LegacyPathFind.h/.cpp`. Its constructor checks whether the path-finding request should be admitted:

- **Admin callers** (`isUnlimited(context.role)`) bypass all limits and are always admitted.
- **Non-admin callers** face two checks: the job queue's `jtCLIENT` job count must be below `Tuning::maxPathfindJobCount`, and the server must not be locally load-shedding. If both pass, an atomic compare-and-exchange loop increments a static `inProgress` counter, rejecting the request if it would exceed `Tuning::maxPathfindsInProgress`.

If `LegacyPathFind::isOk()` returns false, the handler returns `rpcTOO_BUSY` immediately. The RAII destructor decrements the counter when the guard goes out of scope, so the concurrency slot is always released regardless of how `doLegacyPathRequest()` exits.

The result of `doLegacyPathRequest()` is merged with any fields already placed in `jvResult` by `lookupLedger()` (typically ledger metadata). The merge uses `std::move` to avoid copying the string keys.

## Resource Marking and Gate Checks

The very first gate — before any branching — checks `config().PATH_SEARCH_MAX == 0`. This allows operators to administratively disable all path-finding at the server level, returning `rpcNOT_SUPPORTED` cleanly. The `loadType` is immediately set to `Resource::feeHeavyBurdenRPC`, ensuring the caller is charged for a heavyweight operation regardless of which path is taken. Path-finding is one of the most compute-intensive operations in the ledger RPC surface.

## Relationship to `PathFind.cpp`

`PathFind.cpp` in the same directory handles `path_find`, the non-deprecated WebSocket-based API. It shares the same `PathRequestManager` backend and the same `PATH_SEARCH_MAX` guard, but routes through `makePathRequest()` (subscription-based) rather than `makeLegacyPathRequest()`. The legacy name throughout `PathRequestManager` — `makeLegacyPathRequest`, `doLegacyPathRequest` — traces back to this file being the older of the two. The `PathRequest` class itself has two constructors reflecting this split: one takes an `InfoSub` subscriber for push updates, and one takes a completion callback for the one-shot legacy model.

## Shutdown Safety

The detailed shutdown commentary embedded in the source (dated May 2017) highlights a real hazard: during application teardown the job queue stops accepting new work, which can strand a coroutine waiting for a `post()` that will never happen. The `resume()` fallback in the completion lambda is the explicit defense against this scenario, allowing the coroutine to drain on the path-finding thread rather than deadlocking. This pattern recurs elsewhere in the codebase wherever coroutines interact with the job queue during shutdown.