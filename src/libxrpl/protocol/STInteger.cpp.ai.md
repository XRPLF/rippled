# `STInteger.cpp` — Field-Aware Integer Serialization Specializations

`STInteger.cpp` provides the explicit template specializations for the `STInteger<T>` class template, which is the XRPL serialized type for all integer fields in the protocol. The file exists specifically because the generic template — fully defined in `STInteger.h` — cannot express per-instantiation behavior for deserialization, type identification, and human-readable output. Every method that needs to differ between `uint8_t`, `uint16_t`, `uint32_t`, `uint64_t`, and `int32_t` lives here.

## Why Template Specializations in a Separate File

The header provides inline implementations for `add()`, `isDefault()`, `isEquivalent()`, `operator=`, and the copy/move plumbing — all of which behave identically regardless of the underlying integer type. But four virtual methods require type-specific logic: the `SerialIter` constructor (which must call `get8()`, `get16()`, `get32()`, or `get64()`), `getSType()` (which must return the matching `STI_UINT8` / `STI_UINT16` / etc. constant), `getText()`, and `getJson()`. Putting those in a `.cpp` file prevents duplicate symbol problems and avoids implicitly instantiating all specializations in every translation unit that includes the header.

## Field-Identity-Aware Formatting

The architecturally significant pattern here is that `getText()` and `getJson()` are not naive integer-to-string converters. They inspect `getFName()` — the field's compile-time identity — and produce semantic output for well-known protocol fields:

**`STUInt8` / `sfTransactionResult`**: The 8-bit transaction result code is converted to a `TER` via `TER::fromInt()` and passed to `transResultInfo()`, which resolves it to a human-readable description (`getText()`) or a short token string like `"tesSUCCESS"` (`getJson()`). This field appears in transaction metadata, so these two output formats serve different consumers: human debugging versus API clients parsing JSON. If the code is unrecognized — which is expected to be impossible under correct operation — an error is logged and the raw integer falls through. The `LCOV_EXCL_START/STOP` markers around those branches acknowledge they are untestable by design.

**`STUInt16` / `sfLedgerEntryType` and `sfTransactionType`**: The 16-bit type codes are converted to their respective enum types via `safe_cast<LedgerEntryType>()` and `safe_cast<TxType>()`, then looked up in the singleton registries `LedgerFormats::getInstance()` and `TxFormats::getInstance()`. Using `safe_cast<>` here rather than a C-style cast is defensive: it ensures the conversion is intentional and auditable, even though both fields hold raw integers on the wire. The result is that JSON output for a ledger entry shows `"Offer"` rather than `7`, and for a transaction shows `"Payment"` rather than `0`.

**`STUInt32` / `sfPermissionValue`**: Permission values are delegated to `Permission::getInstance().getPermissionName()`, which first attempts granular permission lookup, then falls back to transaction-type-based permission name resolution. This makes `sfPermissionValue` fields readable in API output while keeping the on-wire representation compact.

## `STUInt64::getJson()` — Hex vs. Decimal, Always a String

The 64-bit specialization is the most nuanced. JSON's `number` type is an IEEE 754 double, which cannot represent arbitrary `uint64_t` values without precision loss. `getJson()` therefore always returns a `Json::Value` constructed from a `std::string`, never from a raw numeric type. The conversion is done with `std::to_chars` — locale-independent, no-allocation, and guaranteed to produce the exact decimal or hexadecimal representation.

The choice between base 10 and base 16 is driven by `SField::sMD_BaseTen`, a metadata flag stored on the `SField` instance. Fields explicitly annotated with `sMD_BaseTen` (such as sequence-like counters) render in decimal; all others render in hex, which is the natural representation for opaque 64-bit identifiers like quality values or rate denominators. This field-level metadata eliminates ad-hoc conditionals: the formatting decision was made when the field was registered, not at output time.

`STUInt64::getText()` is notably simpler — it just calls `std::to_string()` — because the text representation is used in log output and diagnostic contexts where decimal is universally preferred regardless of field identity.

## Deserialization Path

Each specialization's `SerialIter` constructor simply delegates to the value constructor with the appropriately-sized read: `sit.get8()`, `sit.get16()`, `sit.get32()`, or `sit.get64()`. Note that `STInt32` (signed) reads via `sit.get32()` — the same method as `STUInt32` — and relies on the implicit bit-pattern reinterpretation that occurs when the unsigned result is stored in a signed integer. This is standard behavior in the XRPL serialization model, which treats the wire as type-agnostic bytes.

## Relationship to `STParsedJSON.cpp`

This file and `STParsedJSON.cpp` form a symmetric pair for the human-readable fields. `STParsedJSON.cpp` parses string values like `"tesSUCCESS"` or `"Payment"` back into their integer representations during JSON ingestion. `STInteger.cpp` converts stored integers back to those strings during JSON emission. The round-trip is exact for all registered types; unrecognized values degrade gracefully to raw integers rather than failing.