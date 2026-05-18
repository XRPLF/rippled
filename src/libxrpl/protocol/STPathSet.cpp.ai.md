# `STPathSet.cpp` — Payment Path Serialization and Management

## Role in the System

Cross-currency payments on the XRP Ledger require a pathfinding step: a client or server discovers one or more routes through the network of offers and liquidity providers that can convert the source asset into the destination asset. These candidate routes are encoded in a `Paths` field of a `Payment` transaction as an `STPathSet` — a serialized type that carries an ordered collection of alternative payment paths. `STPathSet.cpp` implements the binary serialization, deserialization, hashing, cycle-detection, and JSON rendering for this type and the two nested types it depends on: `STPathElement` (a single node or hop descriptor) and `STPath` (an ordered sequence of such nodes representing one candidate route).

## Type Hierarchy

`STPathElement` stores the minimal description of a single hop: it holds a bitmask `mType` indicating which of its three optional components — an `AccountID`, an asset (`PathAsset`), and an issuer `AccountID` — are actually present, plus a pre-computed hash for fast equality. An element with an account set is a *rippling node* (payment flows through that account's trust lines); an element without an account is an *offer node* (a class of order book offers). The `is_offer_` flag captures this distinction at construction time.

`PathAsset` holds a `std::variant<Currency, MPTID>`, reflecting the XRP Ledger's extension to support Multi-Purpose Tokens (MPT) alongside traditional IOU currencies. A path element can carry exactly one: setting both `typeCurrency` and `typeMPT` bits is a protocol invariant violation enforced by `XRPL_ASSERT` during deserialization.

`STPath` is a plain `std::vector<STPathElement>` wrapper with a vector-like interface plus `hasSeen()` for cycle detection. `STPathSet` is a `std::vector<STPath>` wrapped inside `STBase`, making it a first-class serialized type (`STI_PATHSET`) that can appear in transaction fields.

## Wire Format

The binary encoding packs paths end-to-end with structural markers:

- For each hop in a path, a one-byte type field (`iType`) is emitted first, followed by the present components. The type byte is a bitmask: `typeAccount = 0x01`, `typeCurrency = 0x10`, `typeIssuer = 0x20`, `typeMPT = 0x40`. Accounts and issuers are 20-byte `AccountID` values; currencies are 20-byte `Currency` values; MPT IDs are 24-byte `MPTID` values read with `sit.get192()`.
- Between consecutive paths, a `typeBoundary` byte (`0xFF`) is written as a separator.
- The entire path set is terminated by a `typeNone` byte (`0x00`).

The `add()` serialization method emits this format exactly, while the `STPathSet(SerialIter&, SField const&)` constructor reverses it. The constructor loops indefinitely, peeling off one type byte per iteration. When it encounters `typeBoundary` or `typeNone`, it flushes the current `path` vector to the set — but first asserts the path is non-empty and throws `std::runtime_error("empty path")` if it is, since an empty path between separators would indicate corrupt input. Any unknown bit in the type byte (bits outside `typeAll = 0x71`) cause an immediate `std::runtime_error("bad path element")`. These two checks are the sole validation gate for untrusted binary data entering the pathset, making them critical for resilience.

## Hashing Strategy

`STPathElement::get_hash()` computes a non-cryptographic hash over the element's three fields using the pattern `hash = hash * multiplier ^ byte`. The three multipliers — 257, 509, 911 — are small primes chosen to produce good dispersion without collisions for the expected input domain. The comment explicitly notes this need not be secure. The hash is eagerly stored in `hash_value_` at every construction point, so equality comparisons (which check `hash_value_` before the full field comparison) short-circuit quickly. This matters because `assembleAdd()` must scan backward through accumulated paths to detect duplicates.

Notably, `get_hash()` dispatches through `getPathAsset().visit()` rather than branching on `mType` directly. This is intentional: a comment in the source notes that `mType` may carry `typeAccount` while the asset slot is still set (e.g. from `Pathfinder::addLink()`), so the hash must reflect actual content rather than the type flags.

## Duplicate Suppression During Pathfinding

`assembleAdd(STPath const& base, STPathElement const& tail)` is a helper used by the pathfinder when constructing candidate paths incrementally. It appends `base` to the internal vector and then appends `tail` to form a candidate. It then scans all *other* paths in reverse to check for an exact duplicate. If one is found, the newly appended path is popped and the function returns `false`; otherwise it returns `true`. The reverse iteration (via `rbegin()`) is a micro-optimization: duplicates are most likely to be the most recently added paths, so scanning backward catches them earliest.

The reason this dedup is done at add time rather than post-hoc is to bound the size of the path set during pathfinding. Allowing duplicate paths would waste computation in the payment engine, which must simulate flow through each path independently.

## `copy()` and `move()` Placement Semantics

`STBase` subclasses implement `copy()` and `move()` to support placement into an external buffer owned by `detail::STVar`, the type-erased storage used by `STObject` for its fields. Both simply delegate to `emplace()`, which calls placement-new. This is the standard pattern across all serialized types in `libxrpl/protocol` and keeps field storage compact without heap allocation per field.

## JSON Rendering

`STPath::getJson()` emits an array of objects, one per hop. Each object always includes the numeric `type` field so consumers can distinguish hop kinds without re-parsing the optional keys. Account-typed elements render their 20-byte ID as a base58-encoded string via `to_string()`; MPT elements emit `mpt_issuance_id`; IOU currency elements emit `currency`. The assertion that `typeCurrency` and `typeMPT` cannot both be set is repeated here in the JSON path, ensuring JSON output is always coherent even if an element reaches rendering through an unexpected code path.