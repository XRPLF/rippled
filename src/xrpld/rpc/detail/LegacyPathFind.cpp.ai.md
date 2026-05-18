# `LegacyPathFind.cpp` — Pathfinding Concurrency Guard

## Role and Purpose

`LegacyPathFind` is a small RAII guard that controls access to the synchronous `ripple_path_find` RPC operation. Path-finding in XRPL is computationally expensive — it performs graph traversal across the order-book to find liquidity paths between two currencies — so allowing unbounded concurrent execution would starve the job queue and degrade the whole server. This file implements a tiered admission-control scheme that keeps the server healthy under load while giving administrative clients an escape hatch.

## Where It Is Used

The guard appears in exactly two call sites. In `RipplePathFind.cpp`, the `ripple_path_find` RPC handler constructs it immediately after ledger lookup:

```cpp
RPC::LegacyPathFind const lpf(isUnlimited(context.role), context.app);
if (!lpf.isOk())
    return rpcError(rpcTOO_BUSY);
```

And in `TransactionSign.cpp`, it gates auto-pathfinding when a transaction sign request includes `build_path`:

```cpp
LegacyPathFind const lpf(isUnlimited(role), app);
if (!lpf.isOk())
    return rpcError(rpcTOO_BUSY);
```

In both cases the guard lives for the scope of the expensive operation, releasing its slot automatically when it goes out of scope.

## Admission Control Logic

The constructor runs a sequential chain of checks for non-admin requests. Any failure leaves `m_isOk` false and returns immediately with no side effects — the global counter is not touched:

**1. Admin bypass.** If `isAdmin` is true (derived from `isUnlimited(context.role)` in both call sites), the counter is incremented unconditionally and the constructor returns. Administrators are never throttled, regardless of how busy the server is.

**2. Job queue pressure.** `app.getJobQueue().getJobCountGE(jtCLIENT)` returns the total number of active client-class jobs. If this count exceeds `Tuning::maxPathfindJobCount` (50), the request is rejected. This prevents path-finding from competing with ordinary RPC work when the server is already saturated with client requests.

**3. Local fee load.** `app.getFeeTrack().isLoadedLocal()` returns true when the server's own CPU or memory load is high enough that the fee system has raised the local fee multiplier. Triggering either of the first two checks is sufficient for rejection — they are combined with `||`.

**4. Concurrent pathfind ceiling.** The last gate uses `Tuning::maxPathfindsInProgress` (2), enforced via a lock-free CAS loop on the static `std::atomic<int> inProgress`. The loop reads the current value, returns if it has already reached the ceiling, and otherwise attempts `compare_exchange_strong` to increment it. If another thread races to increment between the load and the CAS, the exchange fails and the loop retries rather than over-counting. This is the only spot in the constructor where a retry occurs; all earlier checks are single-shot tests.

## Why CAS Instead of a Mutex

The CAS loop is the right tool here for two reasons. First, the counter update is trivially cheap — a single integer increment — so a mutex would add more overhead than it saves. Second, the loop's retry path is bounded in practice: `maxPathfindsInProgress` is only 2, so at most one competing thread can cause a retry, and contention is extremely low. Using `std::memory_order_release` on success and `std::memory_order_relaxed` on failure is a deliberate choice — the release fence ensures that the increment is visible to other threads that subsequently load `inProgress` with acquire semantics, while the relaxed on failure avoids a needless memory barrier when nothing was changed.

## RAII Invariant

`m_isOk` is the only instance variable aside from the static counter. It defaults to `false` and is only set to `true` in paths where `inProgress` was actually incremented. The destructor checks this flag before decrementing, making it safe to construct a `LegacyPathFind` that fails admission — the destructor becomes a no-op in that case. This invariant means callers cannot accidentally release a slot they never acquired, even if the object is constructed and destroyed in unusual control flows.

## Tuning Constants

`maxPathfindsInProgress = 2` is deliberately low. Path-finding can be orders of magnitude more expensive than a typical RPC call, and two concurrent operations is enough to serve genuine administrative needs while keeping the system responsive. The broader `maxPathfindJobCount = 50` check provides an earlier, lighter-weight gate before the atomic is even consulted.