# `TransactionEntry.cpp` — Ledger-Scoped Transaction Lookup Handler

## Role in the System

This file implements `doTransactionEntry`, the handler for the `transaction_entry` JSON-RPC command. Its purpose is narrowly defined: retrieve a single transaction and its metadata from a **specific, caller-identified ledger**. This distinguishes it from its sibling `Tx.cpp`, which accepts a raw hash and searches across all available ledger history. `transaction_entry` demands that the caller name the ledger explicitly via `ledger_hash` or `ledger_index` — the trade-off being precision (you know exactly which ledger produced this result) at the cost of requiring the caller to already know where to look.

## Validation Chain

The function implements a layered validation sequence before any ledger data is returned. Each stage guards the next:

1. **Ledger resolution** — `RPC::lookupLedger` parses and validates `ledger_hash` / `ledger_index` from `context.params`, populates `lpLedger` (a `shared_ptr<ReadView const>`), and seeds `jvResult` with ledger metadata or an error. If `lpLedger` remains null, the function returns the error immediately.

2. **`tx_hash` presence** — `context.params.isMember(jss::tx_hash)` guards the field before any access. Missing the field yields `fieldNotFoundTransaction`.

3. **Current ledger rejection** — the `else if` branch tests whether `jvResult` already contains a `ledger_hash` key. If `lookupLedger` produced a result with no `ledger_hash` (meaning it resolved to the open/current ledger), the handler returns `notYetImplemented`. The inline comments acknowledge this as a known limitation with a historical `XXX` annotation — the API has never supported querying the in-progress ledger via this command.

4. **Hex parsing** — `uTransID.parseHex(...)` decodes the 64-character hex string into a `uint256`. Failure yields `malformedRequest` and returns immediately without hitting the ledger index.

5. **Transaction existence** — `lpLedger->txRead(uTransID)` returns a pair of `(STTx shared_ptr, STObject shared_ptr)` via C++17 structured bindings. A null `sttx` means the transaction is not in that ledger, yielding `transactionNotFound`.

This layered approach means errors are caught at the cheapest possible point: string checks before hash parsing, hash parsing before ledger I/O.

## Response Shape and API Versioning

The response diverges based on `context.apiVersion`:

**API v1** serializes the transaction with `JsonOptions::none`, producing the legacy format where `Amount` is used for Payment amounts. The metadata key is `metadata`.

**API v2+** changes three things. First, `sttx->getJson(JsonOptions::disable_API_prior_V2)` switches to the v2 serialization. Second, the response gains explicit `hash`, `ledger_hash`, `ledger_index`, `validated`, and `close_time_iso` fields at the top level. Third, the metadata key becomes `meta` (shorter). The `ledger_hash` field is only emitted for closed ledgers — `lpLedger->open()` is tested before calling `context.ledgerMaster.getHashBySeq()`. Close time is only present when the ledger is validated, obtained via `getCloseTimeBySeq`.

`RPC::insertDeliverMax` is called for both API versions. For Payment transactions it copies (and in v2+ removes) the `Amount` field to `DeliverMax`, a field renaming introduced in API v2 to clarify that the amount is the maximum deliverable, not necessarily delivered.

## Relationship to Sibling Files

In the same `handlers/transaction/` directory, `Tx.cpp` handles the broader `tx` command. That handler uses `TransactionMaster`, searches both the ledger database and the transaction queue, supports CTID lookups, and has substantially more logic for assembling the response. `TransactionEntry.cpp` is the leaner, ledger-pinned alternative: it works only against a `ReadView` (a read-only ledger snapshot), never touches the transaction queue, and has no relational database dependency. This makes it appropriate for clients that have already identified the containing ledger and want a deterministic, re-verifiable lookup.

## Design Notes

The `stobj` (transaction metadata `STObject`) from `txRead` may legitimately be null for transactions in open ledgers, since metadata is only generated at ledger close. The code conditionally serializes it only when non-null, so there is no `metadata`/`meta` key in the response for unfinalized transactions — which is consistent with the current-ledger rejection above.

The comment `// XXX Relying on trusted WSS client` on the `parseHex` call is an unresolved legacy note flagging that the hex parsing is lenient; there is no length or character-class check beyond what `parseHex` itself enforces. In practice `parseHex` will reject any non-hex or wrong-length string, so the concern has been functionally addressed even though the comment was never cleaned up.