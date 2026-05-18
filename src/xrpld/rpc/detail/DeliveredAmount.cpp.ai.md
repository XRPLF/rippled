# `DeliveredAmount.cpp` — Resolving the True Payment Delivery Amount for RPC

## Purpose and Context

On the XRP Ledger, a payment transaction carries an `Amount` field that specifies the *intended* or *maximum* delivery. For partial payments — where fees, exchange rates, or path constraints reduce what actually arrives — the `Amount` field can be misleading to clients. This file implements the logic that resolves the actual delivered quantity and injects a `delivered_amount` field into JSON RPC responses, giving consumers a reliable number to work with.

The file is consumed in three places: `LedgerToJson.cpp` (which serializes transactions embedded in full ledger responses), `Tx.cpp` (the `tx` RPC command), and `NetworkOPs.cpp` (real-time transaction subscription notifications). In every case, the caller builds a JSON metadata object and then calls one of the `insertDeliveredAmount` overloads to stamp the final answer into `meta["delivered_amount"]`.

## The Historical Boundary Problem

The `DeliveredAmount` metadata field was not always written into transaction metadata. It was introduced at ledger **4594095**, validated live on January 24, 2014. Before that ledger, partial payment transactions exist on-chain without an explicit `sfDeliveredAmount` record, creating an ambiguity: if the field is absent, does that mean the full `Amount` was delivered, or that the ledger predates the feature?

The core template function `getDeliveredAmount` handles this with a three-tier resolution:

1. **Modern metadata wins first**: If `transactionMeta.getDeliveredAmount()` returns a value, that is authoritative and is returned immediately. This covers all transactions from mid-2014 onward.

2. **Historical inference**: If the metadata field is absent but the transaction has an `sfAmount` field, the function checks whether the ledger is new enough to be trustworthy — `getLedgerIndex() >= 4594095 || getCloseTime() > NetClock::time_point{446000000s}`. Both conditions identify the post-DeliveredAmount era; the close time check (roughly February 2014) is a fallback in case ledger sequence alone is ambiguous. If either condition holds, the `Amount` field is returned directly, because a missing `DeliveredAmount` in a post-fix ledger means the full amount was delivered.

3. **Unknowable past**: Ledgers that predate the threshold and lack the metadata field return `std::nullopt`, which causes the JSON to be populated with the sentinel string `"unavailable"`.

The choice to use `"unavailable"` as a string rather than omitting the field entirely is deliberate. A downstream consumer can distinguish three states: field absent (not a payment transaction), field `"unavailable"` (was a payment but cannot be determined from this ledger), and field present with a numeric amount (definitively known).

## Lazy Evaluation via Template Callables

The innermost `getDeliveredAmount` is a file-local template parameterized on `GetLedgerIndex` and `GetCloseTime`. The comment at the top of the file explains the rationale: these values can be non-trivial to compute. In particular, `getCloseTimeBySeq()` on a `LedgerMaster` may require a database lookup by sequence number. Since the common case (modern ledger with an explicit `sfDeliveredAmount`) returns early before ever touching those values, making them lambdas avoids the cost entirely in the hot path. The ledger index and close time are only evaluated if and when the code reaches the historical fallback branch.

This design is the reason for the two internal overloads: one accepts a `ReadView` directly (where header info is already in memory as `info.seq` and `info.closeTime`) and one accepts an `RPC::Context` (which must delegate to `context.ledgerMaster.getCloseTimeBySeq()`). Both ultimately call the same template, just with different lambda bodies.

## Gate Function: `canHaveDeliveredAmount`

Before any amount resolution happens, `canHaveDeliveredAmount` acts as a type gate. Only three transaction types can produce a `delivered_amount`: `ttPAYMENT`, `ttCHECK_CASH`, and `ttACCOUNT_DELETE`. Additionally, the transaction result must be `tesSUCCESS` — a failed transaction by definition delivers nothing and should not have the field set at all. This guard is invoked at every public entry point before the more expensive resolution logic runs.

## Public API Surface

The header exposes three overloads of `insertDeliveredAmount` and one of `getDeliveredAmount`:

- `insertDeliveredAmount(meta, ReadView, STTx, TxMeta)` — used by `LedgerToJson` when iterating a complete closed ledger object. Ledger header data is directly accessible so close time and sequence are trivially available.
- `insertDeliveredAmount(meta, JsonContext, Transaction, TxMeta)` — thin wrapper that extracts the underlying `STTx` from a `Transaction` object and delegates to the third overload.
- `insertDeliveredAmount(meta, JsonContext, STTx, TxMeta)` — the live RPC path, where the ledger is not directly in scope and close time must be resolved through `LedgerMaster`.
- `getDeliveredAmount(Context, STTx, TxMeta, LedgerIndex)` — exposed separately for callers that need the `STAmount` value directly rather than inserting into JSON (used in the Simulate handler).

All overloads share the same resolution chain and produce the same result; the overload structure exists purely to accommodate the different calling contexts across the codebase.