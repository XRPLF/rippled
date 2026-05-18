# `LedgerToJson.cpp` — Ledger Serialization for RPC Responses

## Purpose and Context

This file implements the conversion pipeline from an in-memory XRPL ledger to the `Json::Value` structures returned by the `ledger`, `ledger_data`, and related JSON/WebSocket RPC methods. It is the single place in the server where ledger headers, transaction sets, account state, and the transaction queue are marshalled into the wire format clients receive.

The file is small but carries significant complexity because it must handle a combinatorial space of output options: whether the caller wants a full dump or just hashes, whether the encoding should be binary (hex-serialized) or human-readable JSON, and which API version the client negotiated. All of those decisions are encapsulated in the `LedgerFill` value-type defined in the companion header.

## The `LedgerFill` Configuration Object

`LedgerFill` (declared in `LedgerToJson.h`) is a plain aggregate that bundles everything the serialization functions need. It carries a `ReadView const&` for the ledger, a bitmask of `Options` flags, an optional pre-fetched queue snapshot, and a nullable `RPC::Context` pointer. The `options` bitmask has seven independent bits:

| Flag | Effect |
|---|---|
| `dumpTxrp` | Include the transaction set |
| `dumpState` | Include all account-state entries |
| `expand` | Expand transactions/state to full JSON instead of hashes |
| `full` | Implies `expand` **and** forces both `dumpTxrp` and `dumpState` |
| `binary` | Encode expansions as raw hex instead of JSON |
| `ownerFunds` | Inject live `owner_funds` balance into offer-create entries |
| `dumpQueue` | Append the open-ledger transaction queue |

Three small file-private helpers—`isFull`, `isExpanded`, and `isBinary`—evaluate these bits. `isExpanded` deliberately implies `isFull`, encoding the hierarchy: binary or expanded means full per-object detail; collapsed means hash-only output.

The `RPC::Context` pointer is allowed to be `nullptr`. Several callsites (including diagnostic log paths in `LedgerHistory.cpp`) construct `LedgerFill` without an RPC context. Every access to `fill.context` is guarded, and the API version defaults to `RPC::apiMaximumSupportedVersion` when the context is absent.

## Ledger Header Serialization

`fillJson` (the header-only overload) handles the open-vs-closed distinction carefully. For an open ledger that does not request a full dump, the function writes only `parent_hash`, `ledger_index`, and `closed: false`, then returns early—omitting the hash and close-time fields that are undefined for an in-progress ledger. This early-return pattern is intentional: a client that asks for the open ledger should not receive stale or zero-valued close data.

The `ledger_index` field changes type across API versions: pre-v2 clients receive a string (for backward compatibility with code that parses it as text), while v2+ clients receive a JSON integer. The `close_time_human` and `close_time_iso` convenience fields are guarded by a comparison against `NetClock::time_point{}` (the epoch), avoiding emission of an "epoch" timestamp for ledgers whose close time has not been set.

`fillJsonBinary` is the binary-mode header path. A closed ledger is serialized via `addRaw(info, s)` and emitted as a hex string under `ledger_data`; an open ledger emits only `closed: false`.

## Transaction Serialization

`fillJsonTx` (the single-transaction overload) is the most complex function in the file and has three distinct output modes controlled by the binary flag and API version:

**Binary mode** (`bBinary == true`): The transaction is serialized to hex under `tx_blob`. For v2+ clients the hash is also included under `hash` and metadata under `meta_blob`; for v1 clients metadata goes under `meta`. This asymmetry exists because v2 made the hash a first-class field independent of blob decoding.

**API v2+ JSON mode**: The transaction is rendered with `JsonOptions::disable_API_prior_V2`, which suppresses legacy field aliases. It is nested under `tx_json` and receives its hash explicitly. The function calls `RPC::insertDeliverMax` to copy the `Amount` field to `DeliverMax` (and remove `Amount` for v2+ clients), reflecting the v2 API rename. For `ttPAYMENT` and `ttCHECK_CASH` transactions, `RPC::insertDeliveredAmount` injects `delivered_amount` into the metadata block—a post-processing step required because the on-ledger metadata does not always contain the actual delivered value. For `MPTokenIssuanceCreate` transactions, `RPC::insertMPTokenIssuanceID` injects the computed `mpt_issuance_id`.

**API v1 JSON mode**: The transaction is inlined directly at the top of `txJson` (no `tx_json` wrapper), metadata appears under `metaData` (capital D), and the same `delivered_amount` and MPT ID injections are applied. The different nesting reflects how the v1 `ledger` RPC historically embedded transactions.

The `ownerFunds` injection after all three branches applies specifically to `ttOFFER_CREATE`: it queries `accountFunds` live from the ledger to include the offer creator's current balance of the asset they are offering. This is useful for order-book consumers who want to filter out underfunded offers without issuing separate account lookups. Notably, self-funded offers (where the issuer is also the account) are excluded because cross-currency self-offers have no funding risk.

The array-level `fillJsonTx` overload iterates `fill.ledger.txs` and appends results. The entire loop body is wrapped in a `try`/`catch(std::exception const&)` with an error log—this mirrors a pattern found in the gRPC `doLedgerGrpc` handler for the same reason: a deserialization failure in one transaction should not abort serialization of the remaining transactions.

## State and Queue Serialization

`fillJsonState` iterates the ledger's SLE (Serialized Ledger Entry) set. In binary mode, each entry is emitted as an object with `hash` and `tx_blob`. In expanded mode, the full `getJson()` representation is appended. In collapsed mode, only the 256-bit key hash is appended. These three variants map to the same `expand`/`binary`/neither dichotomy used throughout the file.

`fillJsonQueue` serializes entries from the transaction queue snapshot stored in `fill.txQueue`. Each entry exposes fee-level metadata (`fee_level`, `fee`, `max_spend_drops`, `auth_change`), retry tracking (`retries_remaining`, `preflight_result`, `last_result`), and the transaction itself via the same `fillJsonTx` path. The v2 API flattens queue transaction fields directly into the queue entry object; v1 nests them under a `tx` key.

## Public Entry Points and `copyFrom`

`addJson` is the standard entry point: it writes a `ledger` key into the supplied JSON object, fills it, and then conditionally appends `queue_data` at the top level. `getJson` is a convenience wrapper that allocates a fresh `Json::Value` and delegates to the private `fillJson`.

`copyFrom` is a utility exposed from the header because it is used by `LedgerHandler::writeResult` to merge pre-validated result fields into the final response. Its fast path handles the common case where the destination is unset (direct assignment avoids member iteration). When the destination already exists, it iterates `from`'s members and copies them one by one. The `XRPL_ASSERT` verifying that `from` is an object-or-null guards against silent data corruption from accidentally merging a non-object value into an existing object—a form of defensive programming against misuse of the API rather than against malformed ledger data.