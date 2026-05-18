# `DeliveredAmount.h` — Delivered Amount RPC Utilities

## Role in the System

This header declares the public interface for computing and injecting the `delivered_amount` field into transaction metadata JSON responses. It exists because the XRP Ledger has a historical correctness problem with payment transactions: the `Amount` field on a `Payment` (or `CheckCash`) transaction records the *intended* delivery, not the *actual* delivery. When the `tfPartialPayment` flag is set, the ledger may settle for less than the stated `Amount`. Without a separate, authoritative record of what was actually delivered, downstream consumers — exchanges, wallets, compliance systems — cannot determine the true transfer size from the `Amount` field alone.

The `DeliveredAmount` field in `TxMeta` was added to the protocol starting at ledger sequence **4594095** (January 24, 2014) to record the actual delivery. This module's job is to bridge the gap: given a transaction and its metadata, produce the correct `delivered_amount` to embed in the RPC response JSON.

## The Three-Tier Source Resolution

The implementation in `detail/DeliveredAmount.cpp` follows a three-tier fallback to find the delivered amount:

1. **TxMeta field present**: If `TxMeta::getDeliveredAmount()` returns a value, that is authoritative. This is the normal case for all ledgers after 4594095.

2. **Ledger is confirmed post-deployment**: If the `DeliveredAmount` field is absent from metadata but the ledger index is ≥ 4594095, *or* the ledger closed after timestamp 446000000s (February 2014), then its absence is meaningful — it means the full `Amount` was delivered. The `Amount` field is returned.

3. **Pre-deployment ledger**: If neither condition holds, the delivered amount genuinely cannot be determined. The string `"unavailable"` is written into the JSON, a sentinel that intentionally cannot be parsed into a valid `STAmount`, preventing consumers from misinterpreting it.

This logic is gated by the `canHaveDeliveredAmount()` helper (internal to the `.cpp`), which ensures only `ttPAYMENT`, `ttCHECK_CASH`, and `ttACCOUNT_DELETE` transactions that completed with `tesSUCCESS` go through the resolution path at all.

## Overload Design

The header exposes three overloads of `insertDeliveredAmount` to serve two distinct call sites:

The **`ReadView const&` overload** is used by `LedgerToJson.cpp` during full-ledger serialization. Here the ledger view is already in hand, so the sequence number and close time are read directly from `ledger.header()`. No `LedgerMaster` lookup is needed.

The **`RPC::JsonContext const&` overloads** are used by the `tx` and `account_tx` RPC handlers. In this path the ledger sequence is derived from `TxMeta::getLgrSeq()`, and the close time must be fetched lazily from `context.ledgerMaster.getCloseTimeBySeq()`. The two variants differ only in whether the transaction is wrapped in an application-level `Transaction` object or exposed as a raw `STTx const`; the former simply delegates to the latter by calling `transaction->getSTransaction()`.

The lazy-evaluation design — both the `getLedgerIndex` and `getCloseTime` arguments are passed as callables in the template-based private implementation — avoids the `LedgerMaster` lookup entirely when `TxMeta` already carries the `DeliveredAmount` field, which is the common case for modern ledgers.

## `getDeliveredAmount` — The Non-Mutating Accessor

`getDeliveredAmount(RPC::Context const&, shared_ptr<STTx const>, TxMeta const&, LedgerIndex const&)` returns an `std::optional<STAmount>` for callers that need the value rather than side-effecting a JSON object. It takes the base `RPC::Context` (not `JsonContext`) because it does not need the request parameters — only `context.ledgerMaster` for the lazy close-time lookup. This overload is exposed for consumers like the `Simulate` RPC handler that needs to reason about the delivered amount without building the full JSON response inline.

## Relationship to Protocol

The header pulls in `xrpl/protocol/Protocol.h` for `LedgerIndex` and `xrpl/protocol/STAmount.h` for the return type of `getDeliveredAmount`. Everything else (`ReadView`, `Transaction`, `TxMeta`, `STTx`) is forward-declared, keeping compile-time coupling minimal. The actual inclusion of heavy ledger and app headers is deferred entirely to the `.cpp` implementation.