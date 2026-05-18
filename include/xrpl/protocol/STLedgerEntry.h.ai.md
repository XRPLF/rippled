# `STLedgerEntry.h` — The Typed Ledger Object

## Role in the System

`STLedgerEntry` (universally aliased as `SLE` throughout the codebase) is the C++ representation of a single object residing in the XRPL state ledger. The ledger state is an associative map — a `SHAMap` — where every object has a 256-bit key and a structured binary payload. `STLedgerEntry` wraps both: it carries the `uint256 key_` that identifies its position in that map and a `LedgerEntryType type_` that names what the object actually is (an account root, an offer, an escrow, a trust line, etc.).

The class inherits from `STObject`, which provides the generic serialized-type field container, and from `CountedObject<STLedgerEntry>`, which hooks the instance into a lock-free global counter for diagnostic memory accounting. Inheriting from `STBase` (via `STObject`) gives it a uniform serialization interface shared with transaction fields and inner objects.

## The Keylet Contract

A `Keylet` is a struct that bundles a `uint256` key together with a `LedgerEntryType`. The primary constructor `STLedgerEntry(Keylet const& k)` enforces a critical invariant: it looks up the type in the `LedgerFormats` singleton. If the type is not registered, it throws immediately rather than producing an object in an unknown state. It then calls `set(format->getSOTemplate())` to populate the `STObject` field list with the correct template for that type, and writes `sfLedgerEntryType` into the object itself so the type is embedded in serialized form. This design means you cannot accidentally create an SLE whose type field disagrees with its in-memory `type_` member.

The convenience constructor `STLedgerEntry(LedgerEntryType, uint256)` simply delegates to the `Keylet` path, providing a less type-safe but ergonomically shorter spelling used in contexts where the type is already known statically.

## Deserialization and `setSLEType()`

When an SLE is read from the ledger — either via a `SerialIter` (raw bytes from the SHAMap) or by wrapping an already-parsed `STObject` — the type is not known at construction entry. Both of those constructors initialize `type_` to the sentinel `ltANY` and then call the private `setSLEType()`. This method reads `sfLedgerEntryType` from the just-deserialized field data, finds the matching format, sets `type_` to the real value, and calls `applyTemplate()` to enforce the SOTemplate. `applyTemplate()` can throw, making deserialization of malformed or unrecognized ledger objects a hard failure rather than silent corruption.

The rvalue `SerialIter&&` overload exists purely for convenience — it forwards to the lvalue overload via a `NOLINT` annotation acknowledging that the rvalue is not truly moved, since `SerialIter` is consumed by position rather than by move semantics.

## Transaction Threading

XRPL ledger objects support a *transaction threading* mechanism: each object records the ID and ledger sequence of the most recent transaction that modified it (`sfPreviousTxnID` / `sfPreviousTxnLgrSeq`). This creates a traceable chain of modifications per ledger object, independent of the transaction history tree.

`isThreadedType(Rules const& rules)` gates this mechanism. Several object types — `ltDIR_NODE`, `ltAMENDMENTS`, `ltFEE_SETTINGS`, `ltNEGATIVE_UNL`, and `ltAMM` — only gained `PreviousTxnID` fields with the `fixPreviousTxnID` amendment. Before that amendment activates, `isThreadedType()` returns false for those types even if the field technically exists in the template, preventing premature use of the threading feature on objects that historically lacked it.

`thread(txID, ledgerSeq, prevTxID, prevLedgerID)` performs the actual update: it captures the current `sfPreviousTxnID` into the caller's out-parameter `prevTxID`, writes the new transaction ID and ledger sequence, and returns `true`. If the object is already threaded to the same `txID`, it returns `false` — an idempotency guard that prevents double-application if a transaction is replayed.

## JSON Representation

`getJson()` delegates to `STObject::getJson()` and then injects the `index` field (the SHAMap key in hex). There is one special case: for `ltMPTOKEN_ISSUANCE` objects, it also computes and injects `mpt_issuance_id` using `makeMptID(sfSequence, sfIssuer)` — a derived identifier that API consumers need but that is not stored as a field inside the object itself.

## Copy/Move and `STVar` Integration

The private `copy()` and `move()` overrides satisfy the `STBase` interface required by `detail::STVar`, the polymorphic value type that `STObject` uses internally to store heterogeneous fields in a `std::vector` with small-buffer optimization. These methods delegate to the `emplace()` helper to construct the object in an externally provided buffer, enabling the owning `STVar` to avoid heap allocation for small objects. `friend class detail::STVar` is declared to enable this.

## Key Design Observations

The `final` keyword on `STLedgerEntry` signals that the class is not meant to be further subclassed despite its polymorphic base. Ledger entry type diversity is handled entirely through the `SOTemplate` / `LedgerFormats` registration system at runtime, not through C++ subclass hierarchies. This keeps the type system flat while still enforcing per-type field schemas.

The `using SLE = STLedgerEntry` alias at the bottom of the header is pervasive throughout the rippled codebase — nearly all code that reads or writes ledger state refers to `SLE` rather than the full name. The shared-pointer typedefs (`SLE::pointer`, `SLE::ref`, `SLE::const_pointer`, `SLE::const_ref`) standardize ownership conventions across ledger-accessing code, where entries are almost always heap-allocated and passed by `shared_ptr`.