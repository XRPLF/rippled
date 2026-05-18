# `LedgerFormats.cpp` — Ledger Entry Format Registry

`LedgerFormats.cpp` is the sole registration point that connects every on-ledger object type to its protocol-level validation schema. Its three functions — `getCommonFields()`, the constructor, and `getInstance()` — collectively build the singleton registry that the rest of the codebase queries to serialize, deserialize, and validate any ledger entry.

## Role in the Protocol Stack

Every object stored on the XRP Ledger (account roots, offers, trust lines, escrows, payment channels, AMM pools, etc.) has a canonical field layout. When an `STObject` is deserialized from raw bytes, it looks up the matching `SOTemplate` in this registry via `LedgerFormats::getInstance().findByType(type)` and validates that every required field is present and every field has the correct type. Without this registry, there is no enforcement boundary between a well-formed ledger entry and arbitrary binary data.

`LedgerFormats` inherits from `KnownFormats<LedgerEntryType, LedgerFormats>`, a CRTP-adjacent base template that manages two `boost::container::flat_map` indexes (by name and by numeric type) over a `std::forward_list<Item>`. The list is node-based by design: once an `Item` is emplaced, its address never changes, so the flat-map indexes can hold stable raw pointers. Each `Item` owns an `SOTemplate` (the combination of type-unique fields and common fields) plus the entry's string name and integer type tag.

## Macro-Driven Registration

The constructor is intentionally terse: it defines a local `LEDGER_ENTRY` macro and then `#include`s `<xrpl/protocol/detail/ledger_entries.macro>`. That file expands one `LEDGER_ENTRY(tag, value, name, rpcName, fields)` invocation per entry type, and the macro expands each invocation into an `add(jss::name, tag, UNWRAP fields, getCommonFields())` call.

This X-macro pattern serves as a single source of truth. The same `ledger_entries.macro` file is also included in `LedgerFormats.h` (with a different `LEDGER_ENTRY` definition) to generate the `LedgerEntryType` enum values. A new ledger entry type is therefore added in exactly one place — the macro file — and both the numeric identifier and the validation schema are derived from that single declaration.

The `UNWRAP(...)` helper macro is needed because the fields argument is written as a doubly-parenthesised braced list, e.g. `({sfAccount, soeREQUIRED}, ...)`. The outer parentheses prevent the C preprocessor from treating commas inside the initializer list as macro argument separators; `UNWRAP` strips them so the result is a valid `std::initializer_list`-compatible expression for `std::vector<SOElement>`. The constructor uses `#pragma push_macro`/`pop_macro` around both `LEDGER_ENTRY` and `UNWRAP` to protect against pre-existing definitions in the translation unit — defensive macro hygiene that avoids hard-to-diagnose build failures.

`LEDGER_ENTRY_DUPLICATE` handles one naming collision: `DepositPreauth` exists as both a transaction type and a ledger entry type. Because `jss.h` uses a `JSS()` macro to declare string constants, two `JSS(DepositPreauth)` expansions in the same translation unit would produce a duplicate symbol. `LEDGER_ENTRY_DUPLICATE` expands to the same `LEDGER_ENTRY` call but suppresses the `JSS` emission.

## Common Fields

`getCommonFields()` returns a local static `std::vector<SOElement>` containing three fields shared by every ledger entry:

- `sfLedgerIndex` (`soeOPTIONAL`) — the object's position in the ledger; may be absent in some contexts.
- `sfLedgerEntryType` (`soeREQUIRED`) — the numeric discriminator used for lookup; must always be present.
- `sfFlags` (`soeREQUIRED`) — the object's flag bitmask; must always be present, even if zero.

Separating these into `getCommonFields()` rather than repeating them in every `LEDGER_ENTRY` invocation means a change to universal fields touches one place and propagates to all ~30 registered types automatically. Using a function-local static also avoids static initialization order problems — the vector is constructed on first call, which happens inside the `LedgerFormats` constructor itself.

## Singleton Initialization and Duplicate Detection

`getInstance()` returns a function-local static `LedgerFormats const instance`. C++11 guarantees this is initialized exactly once, thread-safely, on first access. Because the constructor calls `KnownFormats::add()` for every entry, and `add()` calls `findByType()` before inserting, a duplicate numeric type ID in `ledger_entries.macro` triggers `LogicError` at program startup rather than silently producing an incorrect registry. This is a meaningful invariant: since type IDs are part of the serialization protocol and must never collide, a crash-on-startup is the correct failure mode.

## Protocol Stability Concerns

`LedgerFormats.h` documents explicitly that `LedgerEntryType` numeric values are on-ledger protocol data — changing or reusing them causes a hard fork. The macro file lists entries in ascending type-ID order and deliberately leaves gaps (e.g., `0x0084`–`0x0087` are reserved for future Vault-related objects). Legacy types (`ltNICKNAME = 0x006e`, `ltCONTRACT = 0x0063`, `ltGENERATOR_MAP = 0x0067`) are retained in the header with `[[deprecated]]` annotations rather than removed, precisely to prevent accidental reuse of those numeric slots.