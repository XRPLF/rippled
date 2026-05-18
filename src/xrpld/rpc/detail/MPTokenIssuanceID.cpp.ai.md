# `src/xrpld/rpc/detail/MPTokenIssuanceID.cpp`

## Purpose

This file provides the RPC-layer utility responsible for surfacing the `mpt_issuance_id` field in transaction metadata JSON responses. When a `MPTokenIssuanceCreate` transaction succeeds, the XRP Ledger engine allocates a new `MPTokenIssuance` ledger object and records it in the transaction's metadata. The identifier of that object is a 192-bit `MPTID` — composed of the issuer's 32-bit account sequence (big-endian) concatenated with the issuer's 160-bit `AccountID` — but it does not appear verbatim in the transaction fields. This file extracts that ID from the transaction metadata and injects it into the JSON response as a convenience for API consumers.

## Design Pattern

The implementation follows the exact same three-function guard/extract/insert pattern used by the neighbouring `DeliveredAmount.cpp`: a predicate gates eligibility, a separate extractor digs into transaction metadata, and a public entry point composes both. This separation makes each concern independently testable and keeps the public API surface minimal.

## Function Breakdown

`canHaveMPTokenIssuanceID` is the gatekeeper. It enforces three conditions in sequence: the serialized transaction pointer must be non-null, its type must be `ttMPTOKEN_ISSUANCE_CREATE`, and the transaction engine result must satisfy `isTesSuccess`. The third condition matters because failed transactions may still produce metadata nodes — for example, they may have modified accounts for fee purposes — but no `MPTokenIssuance` object will have been created. Checking the TER here avoids scanning nodes unnecessarily and prevents false positives.

`getIDFromCreatedIssuance` walks the transaction metadata node set looking for an entry whose `sfLedgerEntryType` is `ltMPTOKEN_ISSUANCE` and whose field name (as returned by `getFName()`) is `sfCreatedNode`. Both checks are needed: `sfLedgerEntryType` confirms the object type, while `sfCreatedNode` confirms the operation — the engine records newly created ledger objects under `sfCreatedNode`, modified ones under `sfModifiedNode`. Once the right node is identified, the function reads `sfNewFields` (the post-creation state snapshot stored in metadata), casts it to `STObject`, and calls `makeMptID(sfSequence, sfIssuer)` to reconstruct the canonical identifier. The result is wrapped in `std::optional<MPTID>`, with `std::nullopt` returned if no matching node is found — a defensive choice since the caller already filters on success, but metadata structure should never be assumed complete.

`insertMPTokenIssuanceID` is the public entry point. It delegates first to `canHaveMPTokenIssuanceID`, then to `getIDFromCreatedIssuance`, and only writes `response[jss::mpt_issuance_id]` when both succeed. The response field is a stringified `MPTID`.

## Call Sites

The function is called from multiple response-building paths: `LedgerToJson.cpp` (when serializing ledger transactions to JSON under both `jss::meta` and `jss::metaData`), `NetworkOPs.cpp` (for streaming events), `Tx.cpp`, `AccountTx.cpp`, and `Simulate.cpp`. All call sites pass the raw `STTx` and a freshly constructed `TxMeta` so the extraction is always performed against authoritative, committed metadata.

## Relationship to `MPTID`

`MPTID` is defined in `include/xrpl/protocol/UintTypes.h` as `base_uint<192>`. `makeMptID` (declared in `Indexes.h`) packs the 32-bit sequence and 160-bit account into that 192-bit value. The ID is deterministic and globally unique because each account sequence number is consumed exactly once per account, and a given account can only issue an MPToken issuance once per sequence.