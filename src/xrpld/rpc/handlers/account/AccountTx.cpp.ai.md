# `AccountTx.cpp` — `account_tx` RPC Handler

## Role and Context

This file implements `doAccountTx`, the entry point for the `account_tx` JSON-RPC command. Clients use this command to retrieve all transactions that affected a specific account, optionally filtered to a ledger range and with cursor-based pagination for large result sets. The handler lives in `src/xrpld/rpc/handlers/account/` alongside other per-account RPC handlers (`AccountInfo`, `AccountLines`, etc.) and follows the same structural pattern: parse input, validate, delegate to storage, serialize response.

The implementation is split into five cohesive functions rather than one monolith. This makes each concern independently testable and keeps the serialization logic (`populateJsonResponse`) clearly separated from the data-access logic (`doAccountTxHelp`).

## Data Model

The handler operates entirely through types defined in `RelationalDatabase` (declared in `include/xrpl/rdb/RelationalDatabase.h`). Key types:

- `LedgerSpecifier` is a `std::variant<LedgerRange, LedgerShortcut, LedgerSequence, LedgerHash>` that uniformly represents every way a caller can identify a ledger or range of ledgers.
- `AccountTxArgs` bundles the parsed request fields — account, optional ledger specifier, `binary` flag, `forward` flag, limit, and optional pagination marker.
- `AccountTxResult` bundles the query output — a `std::variant<AccountTxs, MetaTxsList>` holding either rich objects or raw bytes, plus the effective ledger range and the next pagination cursor.
- `AccountTxMarker` is a `{ledgerSeq, txnSeq}` pair that encodes the resumption point for paginated queries. Using ledger and transaction sequence numbers rather than raw SQL row offsets keeps the cursor stable across concurrent writes.

## Input Parsing Pipeline

### `parseLedgerArgs`

This function translates raw JSON `params` into a `LedgerSpecifier` (or an error). The ledger selection logic has a strict priority order: `ledger_index_min`/`max` range → `ledger_hash` → `ledger_index` → no specifier. The function enforces mutual exclusivity at API version 2 and above: supplying both a min/max range and a single-ledger identifier (`ledger_hash` or `ledger_index`) is an `rpcINVALID_PARAMS` error. In v1, this combination was silently tolerated; the v2 strictness is intentional to make the API unambiguous.

A subtle convention governs negative index values: `ledger_index_min < 0` silently defaults to `0` (earliest available), and `ledger_index_max < 0` silently defaults to `UINT32_MAX` (latest). This follows the long-established XRPL convention where `-1` in a numeric field means "unbounded" rather than causing an error. Both unsigned conversions are guarded by checking `asInt() >= 0` first.

### `getLedgerRange`

Given an optional `LedgerSpecifier`, this function resolves it to a concrete `LedgerRange` bounded by the node's currently validated ledger window (obtained via `ledgerMaster.getValidatedRange()`). If the node has no validated range at all, the response differs by API version: v1 returns the legacy `rpcLGR_IDXS_INVALID`, while v2+ returns `rpcNOT_SYNCED`, which more clearly tells the client the server isn't ready.

For range specifiers, v2+ applies strict bounds checking — if the requested min/max extends beyond what the node has validated, `rpcLGR_IDX_MALFORMED` is returned. In v1, the request was silently clamped to the available range instead. The v2 change prevents clients from unknowingly querying incomplete data.

For single-ledger specifiers (hash, sequence, shortcut), the function calls the shared `getLedger()` helper, then verifies the retrieved ledger falls within the validated window via `ledgerMaster.isValidated()`. If it is unvalidated or out of range, `rpcLGR_NOT_VALIDATED` is returned.

## Core Query Dispatch: `doAccountTxHelp`

This function sets the RPC resource load class to `feeMediumBurdenRPC` upfront, then resolves the ledger range and constructs `AccountTxPageOptions` (account, range, marker, limit, admin status). The actual database call is selected based on the `(binary, forward)` combination:

| `binary` | `forward` | Database method |
|----------|-----------|-----------------|
| false    | false     | `newestAccountTxPage()` |
| false    | true      | `oldestAccountTxPage()` |
| true     | false     | `newestAccountTxPageB()` |
| true     | true      | `oldestAccountTxPageB()` |

The `B`-suffixed variants return `MetaTxsList` — a vector of `(tx_blob, meta_blob, ledger_seq)` tuples — which avoids deserializing transaction objects when the client has requested binary output. This is a deliberate performance path: binary clients are often indexers that will parse the blobs themselves.

## Response Formatting: `populateJsonResponse`

The serialization step has the most API-version divergence. For JSON (non-binary) results:

- **API v1**: each transaction entry uses `tx` as the key and includes date in the transaction JSON.
- **API v2+**: uses `tx_json`, sets `JsonOptions::disable_API_prior_V2`, and promotes several fields (`hash`, `ledger_index`, `ledger_hash`, `close_time_iso`) to the top level of the transaction entry rather than embedding them inside the transaction object. The ledger hash is looked up by sequence via `ledgerMaster.getHashBySeq()`, and close time via `getCloseTimeBySeq()`.

For binary results the key for metadata changes from `meta` (v1) to `meta_blob` (v2+).

Beyond the basic serialization, `populateJsonResponse` applies four enrichment passes:

1. `RPC::insertDeliverMax()` adds the `DeliverMax` field to payment transactions, needed because the on-ledger `Amount` field can be confusing for partial payments.
2. `insertDeliveredAmount()` adds a `delivered_amount` field to the metadata of successful payment and check-cash transactions. If metadata has the original value it is used directly; if not, the transaction `Amount` field is used; if neither is available the field is set to `"unavailable"`. The last case exists for historical transactions predating the metadata field.
3. `RPC::insertNFTSyntheticInJson()` synthesizes NFT-related response fields derived from transaction metadata that were added after the original NFT amendments.
4. `RPC::insertMPTokenIssuanceID()` injects the computed `mpt_issuance_id` into the metadata of successful `MPTokenIssuanceCreate` transactions.

The `UNREACHABLE` macro on the missing-metadata branch (where `txnMeta` is null alongside a valid `txn`) signals that this state should never occur in a well-formed database — it's a developer-facing invariant assertion rather than a handled error path.

## Entry Point: `doAccountTx`

The public entry point guards on `config().useTxTables()` first — if the node is not maintaining a transaction index, it immediately returns `rpcNOT_ENABLED` before doing any work.

At API v2+, `binary` and `forward` are validated to be actual JSON booleans rather than strings or numbers. Prior to v2, the XRPL API accepted string truthy values in these fields due to loose JSON coercion in the original JSON library; the v2 cleanup enforces correctness. The marker is validated to have both a `ledger` and a `seq` field of integer type, with an explicit and descriptive error message if either is absent or non-numeric.

The overall flow — `doAccountTx` → `parseLedgerArgs` → `doAccountTxHelp` (which calls `getLedgerRange` internally) → `populateJsonResponse` — keeps each function's responsibility narrow and enables the separation of database logic from JSON serialization.