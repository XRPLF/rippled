# `LedgerEntry.cpp` — `ledger_entry` RPC Handler

## Role in the System

This file implements the `ledger_entry` RPC command, the primary mechanism for clients to read individual objects directly from the XRP Ledger state tree. Given a target ledger and a descriptor for one specific ledger state object (SLE), the handler computes the object's `uint256` index key, fetches it from the `ReadView`, and returns it either as JSON or as its raw serialized bytes. A parallel `doLedgerEntryGrpc` function handles the same lookup over gRPC, accepting a raw key rather than semantic parameters.

## Parser Table Architecture

The file's dominant design decision is the dispatch table. `doLedgerEntry` builds a static `std::array<LedgerEntry, N>` where each element holds three things: the JSON field name that signals which entry type is requested, a `FunctionType` parser callable, and the `LedgerEntryType` enum value used to type-check the result. Rather than maintaining this list manually, the table is populated via the `LEDGER_ENTRY` X-macro from `<xrpl/protocol/detail/ledger_entries.macro>`, which is the single authoritative enumeration of all ledger entry types. The macro expands to `{jss::rpcName, parse##name, tag}` entries, so adding a new ledger entry type to the macro file automatically ensures it appears in this handler.

Two extra aliases — `account_root` for `parseAccountRoot` and `ripple_state` for `parseRippleState` — are appended by hand after the macro block, preserving backward compatibility with historical field names that predate the standardized naming convention. A generic `index` entry is also appended, allowing direct lookup by hex key with an optional human-readable alias in API v3+.

## The `Expected<uint256, Json::Value>` Pattern

Every parser function is a `FunctionType`: `(Json::Value const&, Json::StaticString, unsigned apiVersion) → Expected<uint256, Json::Value>`. On success the parser returns the computed `uint256` key; on failure it returns an `Unexpected<Json::Value>` carrying a fully-formed error JSON object. `doLedgerEntry` checks the result and short-circuits immediately on error, propagating the error JSON directly as the RPC response. This avoids the exception overhead in the hot parsing path while keeping each parser self-contained and composable — the caller never inspects parser internals, only the `Expected` outcome.

## Dual-Form Input

Nearly every parser accepts its value in two forms: a raw hex string (direct key lookup) or a structured JSON object (semantic derivation via `keylet::*`). `parseObjectID` handles the hex case uniformly. The structured case requires type-specific logic — for example, `parseOffer` extracts `{account, seq}` and calls `keylet::offer(*id, *seq)`, while `parseRippleState` extracts a 2-element `accounts` array plus a `currency` string and calls `keylet::line(*id1, *id2, uCurrency)`. This dual-form design lets clients request entries by semantic description without knowing (or computing) the internal hash.

## Fixed-Location Singletons

Three ledger objects are singletons with well-known fixed keys — Amendments, Fee Settings, and Negative UNL. The `fixed(Keylet)` factory captures a `Keylet` by value and returns a lambda that calls `parseFixed`. `parseFixed` either validates a `true` boolean value (meaning "fetch this singleton") or falls through to hex-string parsing. The `parseAmendments`, `parseFeeSettings`, and `parseNegativeUNL` constants are built this way at static-initialization time. In API v3+, `parseIndex` also accepts plain strings (`"amendments"`, `"fee"`, `"nunl"`, `"hashes"`) to reach these singletons without knowing their key hashes.

## The Bridge Parser's Special Case

`parseBridge` is architecturally distinct from every other parser: it requires two sibling fields in the top-level params (`bridge` and `bridge_account`) rather than reading from a single nested sub-object. To handle this, `doLedgerEntry` has an explicit check — when `fieldName == jss::bridge`, it passes the entire `context.params` object to the parser rather than `context.params[fieldName]`. This is the only place in the dispatch loop where the parser receives more than its own sub-field's value.

## `parseDepositPreauth` — Credential Sorting Invariant

The deposit preauth parser enforces an exclusive-or constraint: exactly one of `authorized` or `authorized_credentials` must be present. The credential-array path invokes the helper `parseAuthorizeCredentials`, which validates array bounds (non-empty, not longer than `maxCredentialsArraySize`) and constructs an `STArray` of `sfCredential` objects. Before computing the keylet, the array is sorted by `credentials::makeSorted` — this is not cosmetic. The keylet for a credential-based deposit preauth is keyed on a canonical sorted representation, so supplying credentials in any order must resolve to the same ledger object. An empty sorted result is treated as a malformed input.

## `LedgerEntryHelpers` and Template-Based Parsing

All primitive extraction is delegated to the `LedgerEntryHelpers` namespace defined in `LedgerEntryHelpers.h`. It provides explicit specializations of `parse<T>()` for `AccountID`, `uint256`, `uint192`, `uint32_t`, and `Asset`, plus `parseHexBlob` for variable-length binary fields. The `required<T>` template combines a presence check (`isMember` + `isNull`) with a type parse, producing a consistent `Expected<T, Json::Value>` with a uniform error structure (`error`, `error_code: rpcINVALID_PARAMS`, `error_message`). The `requiredAccountID`, `requiredUInt32`, `requiredUInt256`, `requiredUInt192`, and `requiredAsset` wrappers then forward to `required<T>` with the appropriate error tag string. Because these are all inline functions in the header, the parse chain compiles down without any call overhead.

## Post-Fetch Type Validation

After a parser computes a key and `doLedgerEntry` fetches the SLE from the ledger, it cross-checks the SLE's actual `LedgerEntryType` against the `expectedType` stored in the dispatch table entry. If they differ (and `expectedType` is not `ltANY`), the handler returns `rpcUNEXPECTED_LEDGER_TYPE` rather than the node. This matters most for the raw-hex and `index` paths: a client could supply a valid hex key that happens to belong to a different object type. The check ensures that field-typed parsers (`ripple_state`, `offer`, etc.) only return objects of the expected type, preventing silent misinterpretation.

## API Version Branching

Version-specific behavior appears in two places. `parseIndex` only recognizes the human-readable singleton aliases (`"amendments"`, etc.) when `apiVersion > 2`. `doLedgerEntry` returns the legacy `"unknownOption"` error for API v1 when no recognized field is present, but returns a structured `make_param_error` in v2+. `Json::error` exceptions during parsing are re-thrown in v1 (preserving historical behavior) but caught and translated to `rpcINVALID_PARAMS` in v2+, preventing unstructured exception propagation to newer clients.

## gRPC Variant

`doLedgerEntryGrpc` performs a stripped-down version of the same operation — it takes a raw 32-byte key from the protobuf request, resolves the ledger via `RPC::ledgerFromRequest`, and returns the SLE serialized with a `Serializer`. There is no semantic parsing, no type checking, and no JSON/binary toggle: the gRPC path always returns bytes. Errors are mapped to gRPC status codes (`INVALID_ARGUMENT` for bad params, `NOT_FOUND` for ledger or entry not found).