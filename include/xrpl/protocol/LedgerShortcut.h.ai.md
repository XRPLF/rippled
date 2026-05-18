# `LedgerShortcut.h`

This header defines a small but semantically significant enumeration that captures the three canonical ledger states any XRPL consumer needs to distinguish. Rather than forcing callers to pass magic strings like `"current"`, `"closed"`, or `"validated"` — or to invent ad-hoc integer sentinels — `LedgerShortcut` gives the type system a precise vocabulary for expressing ledger selection intent without requiring a specific sequence number or hash.

## The Three Ledger States

The XRPL maintains three distinct ledger states at any point in time, and each has a distinct meaning:

- **`Current`** — the open, in-progress ledger still accumulating new transactions. It has not yet been closed or validated, so its contents may change.
- **`Closed`** — the most recently closed ledger, meaning no new transactions are being accepted into it, but consensus validation has not yet completed. It is stable in structure but not yet authoritative.
- **`Validated`** — the most recently validated ledger, the fully consensus-confirmed chain tip. This is the only state considered immutable and trustworthy for finality purposes.

This distinction matters operationally: querying the `Validated` ledger gives a guaranteed final answer, while `Current` or `Closed` may reflect intermediate state that could be rolled back during reorganization or consensus failure.

## Role in the Ledger Lookup Infrastructure

`LedgerShortcut` is the third leg of a three-way overload set in `RPCLedgerHelpers.h`. The `getLedger` family accepts a `uint256` hash, a `uint32_t` sequence number, or a `LedgerShortcut`, mirroring the three canonical ways an RPC caller can specify which ledger they want. In `RPCLedgerHelpers.cpp`, the `lookupLedger` parsing logic maps the JSON string fields `"current"`, `"closed"`, and `"validated"` onto the corresponding enum values before dispatching to the appropriate `getLedger` overload. The `AccountTx.cpp` RPC handler does the same when processing the `ledger_index` field.

The enum also appears as one arm of `RelationalDatabase::LedgerSpecifier`, a `std::variant<LedgeRange, LedgerShortcut, LedgerSequence, LedgerHash>`. This variant type unifies all the ways a query layer caller can say "which ledger(s)?" into a single parameter, letting implementations dispatch via `std::visit` rather than maintaining parallel overloaded method families. `LedgerShortcut` being a first-class participant in that variant means symbolic ledger names flow cleanly through the database query layer without needing special-case handling.

## Design Rationale

Using a scoped `enum class` rather than a plain `enum` or string constants prevents implicit integer conversions and namespace pollution, which would be hazards in a codebase that also works extensively with raw integer ledger sequence numbers. The three values map directly onto the three states the XRPL consensus model defines, so there is no over-engineering — no "best" or "latest" alias that would duplicate semantics and create confusion.

The file has no dependencies beyond `#pragma once` and the `xrpl` namespace, which is intentional: this is a pure vocabulary type. Any layer of the stack — RPC handlers, database helpers, gRPC adapters — can include it without pulling in heavyweight ledger or network machinery.