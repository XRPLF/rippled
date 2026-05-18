# `LedgerAccept.cpp` — Admin RPC Handler for Forced Ledger Closure

## Role and Purpose

`LedgerAccept.cpp` implements the `ledger_accept` admin RPC command, which forces the node to close and advance to the next ledger without waiting for peers or the normal consensus timer. This capability exists exclusively for standalone mode — the operating configuration used by developers writing integration tests, by the `jtx` test harness, and by anyone running a local node that is intentionally isolated from the live network. In that context there are no validator peers to coordinate with, so ledger progression has to be triggered on demand rather than driven by consensus rounds.

The file is one of two handlers in the `admin/server_control/` directory; its sibling `Stop.cpp` handles the `stop` command. Both are thin entry points that do very little work themselves and delegate to deeper application services.

## What `doLedgerAccept` Does

The handler's logic fits in under twenty lines. After receiving the `RPC::JsonContext` it first checks `context.app.config().standalone()`. If the node is **not** in standalone mode the function returns immediately with `{"error": "notStandAlone"}` — no locking, no side effects. This early rejection is the primary guard against accidental use on a live validator.

If standalone mode is confirmed, the handler acquires the application-wide master mutex via `std::unique_lock` before doing anything else:

```cpp
std::unique_lock const lock{context.app.getMasterMutex()};
context.netOps.acceptLedger();
jvResult[jss::ledger_current_index] = context.ledgerMaster.getCurrentLedgerIndex();
```

With the lock held it calls `context.netOps.acceptLedger()` and then reads back the new current ledger index to return in the response.

## Why the Master Mutex Matters Here

`Application::getMasterMutex()` returns a `std::recursive_mutex` that is the single serialisation point for all mutations to the open ledger and to the consensus engine state. The same lock is acquired inside `NetworkOPsImp::processHeartbeatTimer()` and inside `RCLConsensus` when it builds a new open ledger. By taking it before calling `acceptLedger()` the RPC handler ensures it is not racing with the heartbeat timer or any other ledger-state mutation that might be in flight. The RAII `std::unique_lock` guarantees release even if `acceptLedger()` throws.

## What `acceptLedger()` Actually Does

`NetworkOPsImp::acceptLedger()` (in `NetworkOPs.cpp`) contains an `XRPL_ASSERT` that the node is in standalone mode, then drives a synthetic consensus round:

```cpp
beginConsensus(m_ledgerMaster.getClosedLedger()->header().hash, {});
mConsensus.simulate(registry_.get().getTimeKeeper().closeTime(), consensusDelay);
return m_ledgerMaster.getCurrentLedger()->header().seq;
```

It bypasses the normal peer-vote protocol entirely by calling `mConsensus.simulate()` — a code path that exists solely to move the consensus state machine forward without real peer participation. The result is a fully validated new ledger sequence as if consensus had concluded normally, including fee adjustments and any pending transactions in the open ledger.

## Response and Usage

On success the JSON response contains a single field `ledger_current_index` holding the sequence number of the ledger that is now open for new transactions (one past the one just closed). Test frameworks use this value to synchronize assertions — they issue transactions, call `ledger_accept`, and then query state at the returned index with confidence the transactions have been processed.

The `doLedgerAccept` signature itself accepts no parameters from the caller's JSON; there is no input to validate beyond the mode check, which explains why the validation architecture metadata notes that all validation here is purely business-logic (mode enforcement) rather than input sanitization.