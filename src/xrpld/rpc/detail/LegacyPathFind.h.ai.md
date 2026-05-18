# `LegacyPathFind.h` — Concurrency Guard for Synchronous Path-Finding

## Role in the System

`LegacyPathFind` is a lightweight RAII guard that controls access to the synchronous, single-shot path-finding code path in XRPL's RPC subsystem. The `ripple_path_find` RPC command is itself marked deprecated, and within it there are two execution branches: an asynchronous coroutine path (used when no ledger is specified) and a synchronous blocking path (used when the caller explicitly names a ledger). `LegacyPathFind` is the concurrency limiter for the latter — the synchronous "legacy" execution. The guard is constructed at the entry point of the synchronous branch and, if `isOk()` returns false, the handler immediately returns `rpcTOO_BUSY` to the caller.

## Design: Lock-Free Slot Acquisition

The class holds a single `static std::atomic<int> inProgress` counter shared across all instances. The constructor either claims a slot in this counter or fails — there is no blocking wait. This is an intentional design choice for an RPC handler: latency matters more than fairness. A caller that cannot get a slot right away is turned away immediately rather than queued, preventing unbounded resource accumulation.

For non-admin callers, the constructor performs two pre-checks before attempting to claim a slot:

1. **Job queue saturation**: if the `jtCLIENT` job count in the `JobQueue` exceeds `Tuning::maxPathfindJobCount` (50), the server is already under pressure handling client work, and path-find is declined.
2. **Local load**: if `getFeeTrack().isLoadedLocal()` is true, the server is throttling itself due to CPU or memory pressure; path-find is again declined.

Only when both checks pass does the constructor enter a CAS loop to atomically increment `inProgress` up to the hard cap of `Tuning::maxPathfindsInProgress` (2). The `compare_exchange_strong` with `memory_order_release` on success and `memory_order_relaxed` on failure is the idiomatic pattern: the release ensures the increment is visible before any path-finding work begins, while the relaxed load on failure avoids unnecessary synchronisation overhead when retrying.

## Admin Bypass

If `isAdmin` is true, the constructor unconditionally increments `inProgress` and sets `m_isOk = true`, skipping every load check and the CAS concurrency cap. This reflects a deliberate policy: administrative connections are trusted to perform heavier operations that would be refused to public clients. The cap of 2 concurrent path-finds is a resource protection for public-facing traffic only.

## RAII Cleanup

The destructor decrements `inProgress` only if `m_isOk` is true — that is, only if the constructor actually claimed a slot. This means failed acquisitions never alter the counter, and there is no double-decrement risk even if the object is destroyed in an unusual path. The asymmetry between the admin increment (unconditional `++`) and the non-admin CAS is safe here because both paths set `m_isOk` before returning.

## Usage Context

In `doRipplePathFind` (`RipplePathFind.cpp`), `LegacyPathFind` is stack-allocated as a `const` object immediately before calling `PathRequestManager::doLegacyPathRequest`:

```cpp
RPC::LegacyPathFind const lpf(isUnlimited(context.role), context.app);
if (!lpf.isOk())
    return rpcError(rpcTOO_BUSY);
```

Its lifetime is tied to the synchronous call. The moment `doLegacyPathRequest` returns and the handler exits scope, `lpf`'s destructor releases the slot, freeing capacity for the next caller. The class has no other public state — `isOk()` is the only observable result of construction.

## Summary

`LegacyPathFind` is a minimal but carefully reasoned concurrency gate. The `static atomic<int>` shared counter is the key: it makes the maximum of 2 simultaneous legacy path-finds a process-wide invariant without requiring a mutex. The admin bypass, the pre-flight load checks, and the CAS loop together form a three-layer defense against resource exhaustion on a deprecated but still callable interface.