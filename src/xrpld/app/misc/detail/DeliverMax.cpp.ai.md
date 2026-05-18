# `DeliverMax.cpp` — API Field Alias for Payment Amount

This file implements a single utility function, `insertDeliverMax()`, that manages the transition of the `Amount` field in `Payment` transaction JSON from its legacy name to its semantically clearer successor, `DeliverMax`. The rename was introduced to make clear that the field represents the *maximum* amount a sender is willing to deliver — crucial for cross-currency and partial payments where actual delivery may differ — rather than an unambiguous delivered quantity.

## What the Function Does

`insertDeliverMax(Json::Value& tx_json, TxType txnType, unsigned int apiVersion)` operates on the outbound JSON representation of a transaction before it is returned to an API caller. Its logic is governed by two independent guards:

1. **Transaction type check** (`txnType == ttPAYMENT`): The `Amount` field exists on several transaction types (e.g., `OfferCreate`, `EscrowCreate`), where it carries entirely different semantics. The rename to `DeliverMax` is only meaningful for payments, so all other types are ignored.

2. **API version gate** (`apiVersion > 1`): For backward compatibility with API v1 clients, the function copies `Amount` into `DeliverMax` but leaves the original `Amount` field intact, so old clients continue to work. For API v2 and later, `Amount` is then removed — callers are required to read `DeliverMax` exclusively. This is a clean break enforced at the serialization boundary rather than in the protocol layer itself.

## Design Rationale

The decision to handle this at the JSON-output layer — rather than renaming the field in the `STTx` object or the ledger's binary serialization — is architecturally deliberate. The canonical on-ledger representation and wire format remain unchanged (`Amount` is the protocol-level field name). The rename is purely a presentation concern for API consumers, so it belongs in the RPC serialization path. This avoids any consensus-layer or storage format changes while still delivering a cleaner API surface.

The function is placed in `xrpl::RPC`, the namespace for RPC-layer utilities, even though the header lives in `app/misc/` — reflecting the mixed layering common to XRPL's RPC helpers.

## Call Sites

`insertDeliverMax()` is called from at least five separate RPC handlers:

- `LedgerToJson.cpp` — when serializing ledger contents including transaction history
- `Tx.cpp` — the `tx` command, for individual transaction lookup
- `AccountTx.cpp` — account transaction history
- `TransactionEntry.cpp` — transaction lookup within a specific ledger
- `TxHistory.cpp` — legacy transaction history endpoint

Each call site retrieves `context.apiVersion` from the active RPC context and the transaction type from the parsed `STTx` object, then passes both directly into this function. The uniformity of these call sites is intentional: rather than duplicating the version-gate logic in every handler, this single function centralizes the field migration policy.

## Invariants and Edge Cases

The function is entirely silent on failure — if `tx_json` lacks an `Amount` member, or if the transaction type is not `ttPAYMENT`, it returns without modification and without logging. This is appropriate because the function is called on all transactions during serialization, and silence for non-payment types is correct behavior, not an error condition.

The `jss::Amount` and `jss::DeliverMax` field name constants come from the compile-time string table in `jss.h`, where `DeliverMax` is explicitly documented as an alias to `Amount`. This ensures field name strings are consistent across the entire codebase and eliminates raw string literals from the RPC layer.