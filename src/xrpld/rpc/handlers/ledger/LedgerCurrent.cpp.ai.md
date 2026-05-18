# `LedgerCurrent.cpp` — RPC Handler for the Open Ledger Index

## Role in the System

This file implements `doLedgerCurrent`, the server-side handler for the `ledger_current` JSON-RPC command. Its entire job is to report the sequence number of the **open (in-progress) ledger** — the ledger that is currently accepting transactions and has not yet been closed and validated.

It lives alongside a small family of related handlers in `src/xrpld/rpc/handlers/ledger/`: `LedgerClosed.cpp`, `Ledger.cpp`, `LedgerData.cpp`, `LedgerDiff.cpp`, `LedgerEntry.cpp`, and `LedgerHeader.cpp`. Each handler answers a distinct query about ledger state; `LedgerCurrent` answers the narrowest possible one.

## What It Does and Why It's Simple

```cpp
Json::Value
doLedgerCurrent(RPC::JsonContext& context)
{
    Json::Value jvResult;
    jvResult[jss::ledger_current_index] = context.ledgerMaster.getCurrentLedgerIndex();
    return jvResult;
}
```

The handler makes a single call to `LedgerMaster::getCurrentLedgerIndex()` and packages the result under the `ledger_current_index` JSON key. There is no input parsing, no validation logic, and no error handling — and that sparseness is intentional.

The open ledger has no finalized hash yet. Transactions are still being applied to it; its state root is not committed. Because there is nothing to identify the ledger other than its sequence number, the response carries only `ledger_current_index`. Compare this to `doLedgerClosed`, which returns both `ledger_index` and `ledger_hash` — the closed ledger has a deterministic, immutable hash that clients can use for consistency checks. The current ledger cannot offer that guarantee.

This design means clients polling `ledger_current` get a lightweight, always-available answer: "the network is currently building ledger N." No lock on ledger state is needed beyond what `LedgerMaster` already manages internally.

## Dependency on `RPC::JsonContext`

All RPC handlers in this codebase receive an `RPC::JsonContext&` rather than raw parameters. The context bundles all server-side dependencies — including a reference to `LedgerMaster` — so handlers never need to reach into the application singleton directly. This keeps handlers easily testable and clearly scoped to what they actually touch.

`LedgerMaster` is the authoritative source of ledger lifecycle state: which ledger is open, which is closed, and which is validated. `getCurrentLedgerIndex()` reads the current open ledger's `LedgerIndex` (a `uint32_t` sequence number) directly, without acquiring the ledger object itself.

## Validation Architecture

There are no RPC input parameters to validate — `ledger_current` takes no arguments — so the handler has no validation logic at all. The RPC framework's template-based dispatch (`RPC::JsonHandler<LedgerCurrent>::process`) performs request-level checks before invoking the handler, so by the time `doLedgerCurrent` runs, the context is guaranteed well-formed. The `jss::ledger_current_index` accessor is a compile-time string constant from the protocol's `jss` namespace, not a dynamic lookup, so there is no risk of key misspelling.

## Summary

`LedgerCurrent.cpp` is an intentionally minimal handler. Its simplicity reflects a real constraint in the ledger model: an open ledger has an identity (its sequence number) but not yet a fingerprint (its hash). Returning only the index is the correct and complete response, not an omission.