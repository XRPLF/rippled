# `AccountInfo.cpp` — RPC Handler for `account_info`

## Role and Context

`AccountInfo.cpp` implements `doAccountInfo`, the server-side handler for the XRPL `account_info` JSON-RPC method. It sits inside `src/xrpld/rpc/handlers/account/`, alongside sibling handlers like `AccountLines`, `AccountOffers`, and `AccountObjects`. Together these form the account-centric slice of the RPC surface; `AccountInfo` is the lowest-level entry point — it reads the account root object directly, rather than enumerating linked objects.

The file also defines `injectSLE`, a small helper that serializes a `SLE` (Serialized Ledger Entry) to JSON, augmented with a Gravatar URL when an email hash is present.

## Input Parsing and Validation

`doAccountInfo` accepts the account identifier under either the canonical `account` key or the legacy `ident` alias. The `ident` alias predates the current API and is preserved purely for backward compatibility. Both paths enforce `isString()` before proceeding; a missing or wrong-typed value returns an `invalid_field_error` or `missing_field_error` immediately, before any ledger access occurs.

The account string is decoded via `parseBase58<AccountID>`, which rejects malformed addresses with `rpcACT_MALFORMED`. Ledger resolution (`RPC::lookupLedger`) runs before account parsing: if the caller requested a non-existent ledger hash or index, the function returns early with the ledger-error response and never touches account state.

## Flag Serialization

Rather than returning raw bitmask integers, the handler translates ledger-specific flags into a human-readable `account_flags` JSON object — a named boolean for each flag. Three static constexpr structures organise these:

- **`lsFlags`** — the nine core flags always included regardless of enabled amendments (e.g., `defaultRipple`, `depositAuth`, `disableMasterKey`).
- **`disallowIncomingFlags`** — four "disallow incoming" flags introduced with NFToken and related amendments, but included unconditionally in the response once the server is current enough to know the field names.
- **`allowTrustLineClawbackFlag`** and **`allowTrustLineLockingFlag`** — individually gated on `featureClawback` and `featureTokenEscrow` respectively. These only appear in the response when the requested ledger has those amendments active, preventing false `false` readings against older ledgers.

This design separates amendment-awareness (whether a field exists in the ledger) from the response shape: unconstrained flags are always serialized so clients can depend on a stable key set, while genuinely amendment-dependent flags are withheld until meaningful.

## Pseudo-Account Detection

`getPseudoAccountFields()` returns a lazily-initialized static list of `SField` pointers whose definitions carry the `sMD_PseudoAccount` metadata flag. Pseudo-accounts are special on-ledger accounts created by protocol features such as `SingleAssetVault` or `LendingProtocol`; they cannot submit transactions, carry no reserve requirement, and have a designated owner field linking them to their controlling object.

The handler iterates these fields and, if any is present on the account root, sets `result["pseudo_account"]["type"]` to the trimmed field name (stripping a trailing `"ID"` suffix for readability). The invariant that only one such field can be set is enforced elsewhere (by `InvariantCheck`), so the loop breaks after the first match.

## Signer Lists and API Version Shim

When `signer_lists: true` is requested, the handler reads `keylet::signers(accountID)` and places the result in a single-element JSON array — pre-allocated as an array on the assumption that future protocol versions might allow multiple signer lists per account.

Where the result is placed depends on `context.apiVersion`:

- **API v1**: the `signer_lists` array is nested inside `account_data`, matching the original (mis)documented behaviour.
- **API v2+**: it moves to the top-level response object as documented on xrpl.org.

API v2 also enforces strict boolean typing on the `signer_lists` parameter itself. In v1, any truthy string was silently accepted; from v2 onward, a non-boolean value returns `rpcINVALID_PARAMS`. This is a deliberate breaking change introduced with the versioned API.

## Transaction Queue Data

When `queue: true` is requested, the handler calls `TxQ::getAccountTxs(accountID)` and serializes each pending `TxDetails` entry. This path is gated with an explicit check that the target ledger is open (`ledger->open()`): the transaction queue is a property of the current open ledger only, and asking for queue state against a closed or validated ledger is rejected with `rpcINVALID_PARAMS`. This is not merely a semantic convenience — the queue is cleared at ledger close, so its contents are meaningless relative to any historical ledger.

For each queued transaction the handler records its `seq` or `ticket` number, fee level, last-valid ledger sequence, absolute fee in drops, and `max_spend_drops` (fee plus potential spend). It also flags whether any queued transaction is an `auth_change` — a "blocker" in TxQ terminology — meaning it alters the account's signing authority, which can invalidate downstream transactions in the same queue.

At the summary level, the response collects `sequence_count`, `ticket_count`, `lowest_sequence`, `highest_sequence`, `lowest_ticket`, `highest_ticket`, `auth_change_queued`, and `max_spend_drops_total`. These summary fields are emitted only when their associated counts are non-zero, keeping the response compact when the queue is simple.

## `injectSLE` — Gravatar and Validity Marker

`injectSLE` serializes the SLE via `getJson(JsonOptions::none)` and then conditionally appends a `urlgravatar` field constructed from the lowercase hex of `sfEmailHash`. The Gravatar URL uses HTTP rather than HTTPS — a known technical debt called out with a `VFALCO TODO` comment. If the SLE is not an `ltACCOUNT_ROOT`, it sets `"Invalid": true` as a defensive signal to callers; in practice `doAccountInfo` only passes account-root SLEs, so this branch is a safety net rather than an expected path.

## Error Handling Summary

| Condition | Error |
|---|---|
| `account` / `ident` wrong type | `rpcINVALID_FIELD` |
| Neither field present | `rpcMISSING_FIELD` |
| Unknown ledger | ledger-level error from `lookupLedger` |
| Malformed Base58 address | `rpcACT_MALFORMED` |
| `queue: true` on non-open ledger | `rpcINVALID_PARAMS` |
| `signer_lists` non-boolean (v2+) | `rpcINVALID_PARAMS` |
| Account not found in ledger | `rpcACT_NOT_FOUND` |