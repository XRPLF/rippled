# `ServerDefinitions.cpp` — Protocol Schema Introspection RPC Handler

This file implements the `server_definitions` RPC endpoint, which exposes the complete XRPL protocol schema as a JSON object. Its purpose is to let clients — libraries, tooling, explorers — discover the current ledger's serialization rules, type system, transaction formats, and flag definitions at runtime without needing hard-coded knowledge of the protocol. The entire output is assembled once at server startup, hashed for cache-validation, and then served as a static payload.

## Architecture: Lazy Singleton Behind a Hash Gate

The public entry point is `doServerDefinitions(RPC::JsonContext&)`. Inside, the `detail::ServerDefinitions` class is stored as a `static` local — a Meyers singleton, constructed exactly once under the C++11 guarantee of thread-safe static initialization. All subsequent calls reuse the already-built `Json::Value`.

The handler accepts an optional `hash` query parameter. If the client passes the hash it already holds, the server calls `hashMatches()` and returns only `{"hash": "..."}` when they match. This bandwidth optimization matters because the full definitions payload is large and changes only on software upgrades. A mismatch causes the full definitions to be returned and the client can update its cached copy.

## What the Constructor Builds

`ServerDefinitions()` populates nine logical sections into a single `defs_` JSON object:

**`TYPES`** is a map from human-readable type names (`AccountID`, `Hash256`, `UInt32`, `Blob`, etc.) to their integer `SerializedTypeID` codes. The raw source is `sTypeMap`, a macro-generated `std::map<std::string, int>` built from the `STYPE(STI_XXX, N)` macro table in `SField.h`. Each raw name has its `STI_` prefix stripped before being fed to `translate()`.

**`LEDGER_ENTRY_TYPES`** and **`TRANSACTION_TYPES`** are populated by iterating over the singleton instances of `LedgerFormats` and `TxFormats` respectively, extracting name/type-code pairs. Both seed an `Invalid = -1` sentinel.

**`FIELDS`** is an array of `[name, {nth, isVLEncoded, isSerialized, isSigningField, type}]` pairs. Six entries are hard-coded at the start — `Generic`, `Invalid`, `ObjectEndMarker`, `ArrayEndMarker`, `taker_gets_funded` (nth=258), and `taker_pays_funded` (nth=259) — because they either have no canonical registry entry or require explicit control over their serialization attributes. The remaining fields come from `SField::getKnownCodeToField()`. Fields with empty names are silently skipped.

Three field attributes are derived inline by numeric type ID:

- `isVLEncoded` is true only for types 7 (Blob), 8 (AccountID), and 19 (Vector256). These are the three variable-length types in the serialization format.
- `isSerialized` is false for fields whose type code is ≥ 10000 (the container pseudo-types `STI_TRANSACTION`, `STI_LEDGERENTRY`, `STI_VALIDATION`, `STI_METADATA`) and for fields literally named `hash` or `index`, which are computed values rather than stored fields.
- `isSigningField` delegates to `SField::shouldInclude(false)`.

**`TRANSACTION_RESULTS`** is populated from `transResults()`, mapping TER code names like `tesSUCCESS` and `tecDIR_FULL` to their integer codes.

**`TRANSACTION_FORMATS`** and **`LEDGER_ENTRY_FORMATS`** expose per-type field schemas including optionality. Each section has a `common` key listing the fields shared by all transactions (or all ledger entries), with per-type entries listing only the type-specific fields. Common fields are tracked in a `std::set<std::string>` and skipped during per-type iteration, so clients get a clean two-level inheritance model.

**`TRANSACTION_FLAGS`** and **`LEDGER_ENTRY_FLAGS`** are populated from `getAllTxFlags()` and `getAllLedgerFlags()`, both of which are Meyers singletons in `TxFlags.h` built from X-macro tables. The result is a map keyed by transaction/entry type name (plus a `universal` key for globally-applicable flags), each containing a flat name-to-bitmask map.

**`ACCOUNT_SET_FLAGS`** maps `AccountSet`-specific flag names (`asfDisallowXRP`, `asfGlobalFreeze`, etc.) to their integer identifiers via `getAsfFlagMap()`.

## The `translate()` Function

This private static method converts raw `STI_`-stripped names into the naming convention expected by clients. The translation rules encode semantic knowledge about the XRPL type system:

- `UINT` combined with a fixed bit-width (128, 160, 192, 256, 384, 512) becomes `Hash` — e.g., `UINT256` → `Hash256`. This reflects the fact that these fixed-width types are used exclusively as cryptographic digests in the protocol, not arithmetic integers.
- `UINT` without a recognized bit-width suffix becomes `UInt` — e.g., `UINT32` → `UInt32`.
- A fixed lookup table handles special cases: `VL` → `Blob`, `ACCOUNT` → `AccountID`, `OBJECT` → `STObject`, `ARRAY` → `STArray`, etc.
- All other names are converted from `SCREAMING_SNAKE_CASE` to `CamelCase` by splitting on underscores and title-casing each token.

The lambda-based helper structure (`replace`, `contains`) is idiomatic for a function that tests the same input multiple ways. The comment noting a future use of `string::contains` from C++23 indicates an intentional minimum-language-version constraint at time of writing.

## Hash Integrity

After all sections are built, the constructor serializes `defs_` via `Json::FastWriter` and computes `sha512Half` over the resulting string. The resulting `uint256` is stored as `defsHash_` and also embedded into `defs_[jss::hash]` as a string. This means the hash covers the entire definitions payload but not the hash field itself — the hash is appended after the fact. Clients can use this hash for cache invalidation: any protocol change that adds a new ledger entry type, SField, transaction type, or flag will produce a different definitions hash.

## Relationship to Sibling Handlers

This file lives alongside `ServerInfo.cpp`, `ServerState.cpp`, `Fee.cpp`, `Feature.cpp`, and `Manifest.cpp` in `src/xrpld/rpc/handlers/server_info/`. While the other handlers report runtime state (current fee, active amendments, server health), `ServerDefinitions` is unique in being purely static — it reports protocol structure, not runtime state, and it never changes during a server's lifetime.