# `xrpld/rpc/MPTokenIssuanceID.h`

This header is part of a small, focused module that solves a narrow but important problem: the `mpt_issuance_id` of a newly created MPToken issuance is not stored as an explicit field inside the ledger object or the transaction itself — it is derived at runtime from two fields (`Sequence` and `Issuer`) inside the `MPTokenIssuance` ledger entry created by the transaction. This header declares the three functions that together compute and inject that derived identifier into RPC JSON responses.

## Context: Why a Derived Identifier?

`MPTID` is a `base_uint<192>` (a 192-bit opaque identifier) computed by `makeMptID(sequence, account)` as defined in `Indexes.h`. Because the ID is deterministically reconstructable from data already present in the transaction metadata, the protocol does not redundantly store it. However, API consumers need it: callers submitting an `MPTokenIssuanceCreate` transaction need to know what identifier to use when subsequently managing or trading the issuance. This module bridges the gap by extracting the ID from the transaction's `CreatedNode` metadata and injecting it into the JSON response after the fact.

## The Three-Function Design

The module exposes three functions in `xrpl::RPC`, and their division of responsibility mirrors the pattern established by `DeliveredAmount.h` for the analogous `delivered_amount` enrichment.

`canHaveMPTokenIssuanceID()` is a pure eligibility predicate. It guards against unnecessary work and prevents accidental ID injection on irrelevant transactions. The implementation checks two conditions: the transaction type must be `ttMPTOKEN_ISSUANCE_CREATE`, and `transactionMeta.getResultTER()` must indicate `tesSUCCESS`. Both guards are necessary — a failed `MPTokenIssuanceCreate` does not actually create a ledger entry, so there is no `CreatedNode` to scan and no identifier to inject.

`getIDFromCreatedIssuance()` does the actual extraction. It walks the `TxMeta` node list looking for a node whose `LedgerEntryType` is `ltMPTOKEN_ISSUANCE` and whose field name is `sfCreatedNode`. When found, it reads `sfSequence` and `sfIssuer` out of the `sfNewFields` sub-object and calls `makeMptID()` to reconstruct the 192-bit identifier. The `std::optional<MPTID>` return type handles the case where no such node exists, rather than using an exception or sentinel value — consistent with modern XRPL C++ idioms throughout the codebase.

`insertMPTokenIssuanceID()` is the public-facing orchestration point. It calls `canHaveMPTokenIssuanceID()` first and returns early if the check fails, then delegates to `getIDFromCreatedIssuance()`. If both succeed, it writes `response[jss::mpt_issuance_id]` as a stringified `MPTID` directly into the `Json::Value` passed by reference. Taking the response as a non-const reference for in-place mutation is the same pattern used by `insertDeliveredAmount()` in `DeliveredAmount.h`.

## Call Sites

`insertMPTokenIssuanceID()` is invoked at every point in the codebase where transaction metadata is serialized to JSON for API consumers:

- `Tx.cpp` (the `tx` RPC command) calls it immediately after `insertDeliveredAmount()` and `insertNFTSyntheticInJson()`.
- `AccountTx.cpp` applies the same enrichment trio when building the transaction list for an account.
- `NetworkOPs.cpp` enriches the metadata when broadcasting transaction results to subscribers.
- `LedgerToJson.cpp` applies it when serializing full ledger contents.
- `Simulate.cpp` applies the same enrichment so simulated transaction responses are structurally identical to real submission responses.

This consistent pattern — always appearing alongside `insertDeliveredAmount()` and `insertNFTSyntheticInJson()` — reflects that all three are post-processing enrichments for fields that are derived rather than stored. The header formalizes the `MPTokenIssuanceID` enrichment as a first-class sibling in that enrichment layer.

## Design Notes

Separating `canHaveMPTokenIssuanceID()` and `getIDFromCreatedIssuance()` from `insertMPTokenIssuanceID()` is a deliberate testability affordance: callers can invoke the predicate and extractor independently in unit tests without needing a full `Json::Value` response object. The `STLedgerEntry::getJson()` path in the protocol layer also independently calls `makeMptID()` directly to inject `mpt_issuance_id` when serializing an `ltMPTOKEN_ISSUANCE` object, so the derived identifier appears consistently whether accessed through a transaction lookup or a direct ledger entry fetch.