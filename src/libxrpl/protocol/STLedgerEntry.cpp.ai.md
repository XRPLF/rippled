# `STLedgerEntry.cpp` — Typed Ledger Entry Implementation

`STLedgerEntry` is the concrete, typed representation of a single object in the XRPL ledger state. The ledger itself is a key-value store (backed by a `SHAMap`) where every entry is keyed by a `uint256` hash and has a declared type. This file implements the lifecycle of those entries: creation, deserialization, serialization-to-JSON, and the threading mechanism that links each modified entry to the transaction that last changed it.

## Inheritance and Identity

`STLedgerEntry` inherits from `STObject`, the general-purpose serialized field container. The subclass adds two private members: `key_` (the `uint256` SHAMap key, also called the "index") and `type_` (the `LedgerEntryType` enum value). Every function in this file either establishes, validates, or exposes those two pieces of typed identity on top of what `STObject` already provides.

The alias `using SLE = STLedgerEntry` is defined in the header, and `SLE::pointer` / `SLE::ref` aliases to `shared_ptr` variants are the standard way the rest of the codebase holds and passes ledger entries.

## Three Paths to Construction

The class has three constructors, each representing a distinct usage scenario.

**Creating a new entry** — `STLedgerEntry(Keylet const& k)` builds an empty, properly initialized object. A `Keylet` bundles a `LedgerEntryType` with its deterministically computed `uint256` key. The constructor looks up the format in the singleton `LedgerFormats` registry and throws `std::runtime_error` if the type is unknown, making it impossible to create an entry of an unrecognized type. It then calls `set(format->getSOTemplate())`, which populates the `STObject` with all fields declared in the format's `SOTemplate` at their default values and marks their optionality/requirement. Finally it writes `sfLedgerEntryType` into the object so the wire encoding is self-describing.

**Deserializing from wire** — `STLedgerEntry(SerialIter&, uint256 const& index)` first calls `STObject::set(sit)` to consume raw bytes and populate all fields, then calls the private `setSLEType()`. At deserialization time the type is embedded as the `sfLedgerEntryType` field inside the byte stream itself, so the constructor cannot know the type until after the fields are read. `setSLEType()` reads that field back out, looks it up in `LedgerFormats`, and calls `applyTemplate()` — the post-hoc counterpart to `set()`: rather than initializing an empty object, it validates and conforms an already-populated one to the required template, throwing if required fields are missing or the type is unrecognized.

**Wrapping an `STObject`** — `STLedgerEntry(STObject const& object, uint256 const& index)` handles the case where fields have already been parsed into a generic `STObject` and just need to be re-interpreted as a typed ledger entry. It delegates to `setSLEType()` for the same validation path as the deserialization constructor.

The split between `set()` (used when creating fresh) and `applyTemplate()` (used when validating deserialized data) reflects a real semantic difference: one is initialization, the other is conformance checking.

## Representation Methods

`getText()` produces a compact diagnostic string with the hex key and field contents via Boost.Format.

`getFullText()` is the heavier debug representation: it re-validates the format (throwing on corruption) to obtain the human-readable type name and includes it in the output alongside the key and all field values. The redundant format lookup in `getFullText()` is a deliberate defensive check — by the time this method is called, `type_` should always be valid, but the lookup enforces that invariant before emitting output.

`getJson()` is the most interesting representation method. It delegates to `STObject::getJson()`, then injects `jss::index` (the hex-encoded SHAMap key) since the key is stored in `key_` rather than in any serialized field. There is one special case: for `ltMPTOKEN_ISSUANCE` objects, `getJson()` also computes and injects `mpt_issuance_id` by calling `makeMptID(sfSequence, sfIssuer)`. This derived identifier is not stored as a field in the ledger object — it is recomputed on read. The design avoids redundancy in consensus-critical storage and ensures the derived ID is always consistent with the fields that define it.

## Transaction Threading

The ledger maintains an audit trail by threading each ledger entry through the transactions that modified it. The mechanism uses two fields, `sfPreviousTxnID` and `sfPreviousTxnLgrSeq`, which are updated every time a transaction touches an entry.

`isThreadedType(Rules const& rules)` gates which objects participate in threading. Not all object types have always carried those fields. Five types — `ltDIR_NODE`, `ltAMENDMENTS`, `ltFEE_SETTINGS`, `ltNEGATIVE_UNL`, and `ltAMM` — only gained `PreviousTxnID` support when the `fixPreviousTxnID` amendment activated. Before that amendment, `isThreadedType()` returns `false` for those types even if the field is declared in the template. This guard prevents premature threading on objects that historical validator code would not expect to carry those fields. The function performs a linear scan of a five-element `constexpr` array, an acceptable cost given this is called during transaction application, not in a hot inner loop.

`thread()` performs the actual update: it reads the current `sfPreviousTxnID`, checks whether the same transaction has already threaded this entry (returning `false` with an assertion if so, guarding against double-application), captures the old values into the output parameters, and writes the new transaction ID and ledger sequence. The output parameters allow callers to reconstruct the modification chain — each transaction records what the previous transaction was, forming a linked list through ledger history.

## Copy and Move Semantics

`copy()` and `move()` delegate to the `emplace()` helper from `STBase`. These methods support placement-new construction into caller-provided buffers, which is used by `detail::STVar` — a small-buffer variant that avoids heap allocation for serialized type objects. `STVar` is friended in the header, confirming this is an internal protocol-layer mechanism rather than a general-purpose interface.

## Validation Architecture

All three constructors fail loudly via `Throw<std::runtime_error>` if the type is unrecognized. There is no silent fallback to a generic representation: an `STLedgerEntry` with an unknown type cannot be constructed. This is correct for a consensus system where permitting unrecognized object types to propagate would create non-deterministic behavior across validators. Template conformance via `applyTemplate()` enforces the same guarantee for deserialized data, ensuring that on-ledger objects cannot have field sets that diverge from their declared format.