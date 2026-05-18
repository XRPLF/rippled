# `LedgerClosed.cpp` — `ledger_closed` RPC Handler

This file implements `doLedgerClosed`, the handler for the `ledger_closed` RPC command. It answers the narrowly scoped question: *what is the most recently closed ledger?* — returning exactly two fields, `ledger_index` and `ledger_hash`, with no user-supplied input parameters.

## Ledger State Terminology

The XRPL node maintains three conceptually distinct ledger states, each accessible through a sibling handler in this directory:

- **Current** (`doLedgerCurrent`) — the open, in-progress ledger being assembled from incoming transactions. Not yet closed.
- **Closed** (`doLedgerClosed`, this file) — the ledger that has just completed the consensus process and been closed, but whose validation quorum has not necessarily been reached yet. Called the "finalized" or "accepted" ledger in `LedgerMaster.h`.
- **Validated** — the last ledger confirmed by a supermajority of trusted validators; the most authoritative state.

The closed ledger is therefore a step ahead of the open one but a step behind the validated one. Clients that need a stable, consensus-confirmed reference point without waiting for full validation use this endpoint — for example, to track ledger sequence progression or confirm a transaction was included in a specific closed ledger.

## Handler Registration and Precondition

In `Handler.cpp`, `doLedgerClosed` is registered as:

```cpp
{"ledger_closed", byRef(&doLedgerClosed), Role::USER, NEEDS_CLOSED_LEDGER},
```

The `NEEDS_CLOSED_LEDGER` precondition is significant: the RPC dispatch framework checks that a closed ledger actually exists *before* invoking the handler. This makes the `XRPL_ASSERT` inside `doLedgerClosed` a true invariant guard — it cannot be triggered by normal client requests, only by a programming error in the framework itself. If the assertion ever fires, it represents a broken invariant (the precondition was satisfied but `getClosedLedger()` still returned null) rather than an expected failure mode. The handler is also `Role::USER`, meaning no admin authentication is required.

## Implementation

The handler is minimal by design:

```cpp
Json::Value doLedgerClosed(RPC::JsonContext& context)
{
    auto ledger = context.ledgerMaster.getClosedLedger();
    XRPL_ASSERT(ledger, "xrpl::doLedgerClosed : non-null closed ledger");

    Json::Value jvResult;
    jvResult[jss::ledger_index] = ledger->header().seq;
    jvResult[jss::ledger_hash]  = to_string(ledger->header().hash);
    return jvResult;
}
```

`LedgerMaster::getClosedLedger()` returns a `std::shared_ptr<Ledger const>` from `mClosedLedger`, an internal cache of the last closed ledger. The `header()` accessor exposes the ledger's `seq` (a `uint32_t` sequence number written directly to JSON as an integer) and `hash` (a `uint256` converted to a lowercase hex string via `to_string`). The handler consumes no request parameters — the `context` is used only to reach `ledgerMaster`.

## Contrast with `doLedgerCurrent`

The peer handler `LedgerCurrent.cpp` is similarly trivial but exposes only `ledger_current_index` — no hash, because the open ledger's hash is undefined until it closes. `doLedgerClosed` returns both sequence and hash precisely because the closed ledger is immutable at query time. This asymmetry between the two handlers reflects a real protocol property: a closed ledger has a canonical identity, while the current one does not.

## No Input Validation Needed

Because this handler accepts no client-supplied fields, there is nothing to validate from the user's side. The `jss::` constants (`jss::ledger_index`, `jss::ledger_hash`) are compile-time string literals that serve as type-safe JSON field name keys, not runtime-validated inputs. The sole validation — the `XRPL_ASSERT` — guards an internal invariant, not external data.