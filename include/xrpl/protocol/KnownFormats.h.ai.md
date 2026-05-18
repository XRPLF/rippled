# `KnownFormats.h` — Protocol Format Registry Template

## Purpose and Role

`KnownFormats<KeyType, Derived>` is the central abstraction for managing XRPL's **protocol format registries** — the compile-time-registered catalogs that define which fields are valid, required, or optional for every transaction type, ledger object, and inner object on the network. It provides a type-safe, name-indexed, and type-indexed lookup structure that the serialization and validation layers consult constantly at runtime.

Three concrete registries inherit from it:
- `TxFormats : public KnownFormats<TxType, TxFormats>` — one `Item` per transaction type (`ttPayment`, `ttOfferCreate`, etc.)
- `LedgerFormats : public KnownFormats<LedgerEntryType, LedgerFormats>` — one `Item` per on-ledger object type
- `InnerObjectFormats : public KnownFormats<int, InnerObjectFormats>` — one `Item` per inner STObject structure

## The `Item` Inner Class

Each registered format is an `Item`, bundling three things: a human-readable name string, a `KeyType` discriminant that maps to the wire-protocol integer, and an `SOTemplate` that codifies the field schema. The `SOTemplate` in turn holds `SOElement` entries, each pairing an `SField` reference with a style tag (`soeREQUIRED`, `soeOPTIONAL`, or `soeDEFAULT`) and, for amount/issue fields, an MPT-support annotation.

The `Item` constructor enforces a `static_assert` that `KeyType` is either integral or an enum — a deliberate compile-time gate that prevents misuse of the template with arbitrary types. Since these keys are embedded in signed transactions and ledger objects (meaning they are part of the binary protocol), the constraint ensures the key is always something that maps directly to an integer wire value.

## Storage Design: Why `std::forward_list`

The central design decision is using `std::forward_list<Item>` as the owning container, with `flat_map` indices pointing into it. The code makes this explicit in a comment: the requirement is that each `Item`'s **memory address must be stable** after insertion. `std::vector` and `std::deque` can reallocate and invalidate pointers; `std::forward_list` is a node-based structure that never moves existing elements.

The two `boost::container::flat_map` instances — `names_` and `types_` — store raw `Item const*` pointers, so pointer stability is not optional. `flat_map` is chosen over `std::map` because these registries are built once at startup and then read-only; the sorted contiguous storage of `flat_map` gives better cache behaviour for the frequent lookups that happen during transaction processing.

`emplace_front` is used when adding items, which means items are stored in reverse-registration order in the list — but since the maps are the only lookup paths, iteration order over the list is irrelevant for correctness. The `begin()/end()` pair on the class itself, which exposes `forward_list` iteration, is explicitly annotated as being for testing purposes only.

## CRTP for Error Messaging

The second template parameter `Derived` follows the Curiously Recurring Template Pattern. Its sole runtime use is in the constructor, where `beast::type_name<Derived>()` captures the concrete subclass name into `name_`. This string is then embedded in error messages from `findTypeByName`, making diagnostics like `"TxFormats: Unknown format name 'BadName'"` possible without any virtual dispatch overhead.

## Lookup Asymmetry: Throw vs. nullptr

`findTypeByName()` throws `std::runtime_error` when given an unknown name, while `findByType()` returns `nullptr`. This is a deliberate asymmetry. Name lookups occur when parsing externally supplied strings — JSON RPC calls, config files, user input — where a missing name is a recoverable application-level error worth propagating as an exception. Type lookups occur in internal code paths where the caller can meaningfully handle a miss by checking `nullptr`, and where the null-check pattern is idiomatic C++.

The `findByName()` overload is `protected`, preventing direct callers outside the class hierarchy from bypassing the exception-throwing public interface.

## Duplicate Registration Guard

`add()` calls `findByType()` before inserting and invokes `LogicError()` if the key already exists. `LogicError` is a hard process termination — not an exception, but a fatal abort. This is appropriate here: a duplicate format registration is a programming error that cannot be recovered from, and it is caught at startup when the singleton constructors run, not at request-handling time.

## Relationship to `SOTemplate` and `SField`

`KnownFormats` sits directly above `SOTemplate` in the dependency chain. It does not interpret field semantics itself — it simply owns the templates and provides indexed access. The `SOTemplate` class merges unique fields (specific to one format) and common fields (shared across all formats in the registry, like transaction metadata fields) into a single flat element list with an index vector for O(1) field lookup by `SField` number. This split between unique and common fields in the `Item` constructor maps cleanly to the `TxFormats` and `LedgerFormats` APIs, which both expose `getCommonFields()` as a static method.