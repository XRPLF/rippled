# `DeliverMax.h` — RPC Field Alias for Payment Transaction Amount

This header declares a single utility function that handles a deliberate, versioned rename of the `Amount` field in Payment transaction JSON output. It lives in `xrpl::RPC` because the translation is purely an API-layer concern: the on-ledger binary serialization continues to use `Amount` unchanged, but JSON responses presented to API consumers go through this normalization step.

## The Problem It Solves

The `Amount` field name is semantically ambiguous for Payment transactions. In the ledger protocol, `Amount` on a Payment represents the *maximum* the sender is willing to deliver — the actual delivered amount can be lower when partial payments or path-finding are involved. The name `DeliverMax` is a much clearer description of this semantics. Renaming a widely-used field in a live API requires a careful two-phase migration: first add the alias so clients can adapt, then remove the old name once the ecosystem has moved to the newer API version.

## `insertDeliverMax()`

```cpp
void insertDeliverMax(Json::Value& tx_json, TxType txnType, unsigned int apiVersion);
```

The implementation in `detail/DeliverMax.cpp` is compact and intentional:

1. **Guard on field presence**: only acts when `Amount` is actually present in the JSON object, avoiding crashes on partial or pre-serialized objects.
2. **Guard on transaction type**: only `ttPAYMENT` is affected. Every other transaction type that carries an `Amount` field (offers, escrows, etc.) passes through completely untouched.
3. **Version-conditional removal**: when `apiVersion > 1`, the `Amount` key is removed after copying, forcing clients on the newer API to exclusively use `DeliverMax`. Clients still on v1 receive both keys, preserving backward compatibility without any special-casing at the call sites.

## Call Sites

`insertDeliverMax` is called after every path that serializes a transaction to JSON for RPC output: `Tx.cpp`, `LedgerToJson.cpp`, `TransactionSign.cpp`, `AccountTx.cpp`, `TxHistory.cpp`, `TransactionEntry.cpp`, and `NetworkOPs.cpp`. Because the logic is isolated here rather than duplicated across those handlers, the aliasing behavior is guaranteed to be consistent regardless of which endpoint a client uses to retrieve a transaction.

The forward declaration of `Json::Value` (rather than a full include of the Json headers) keeps this header lightweight — it only pulls in `TxFormats.h` for the `TxType` enum, so including it in RPC handlers adds minimal compilation overhead.