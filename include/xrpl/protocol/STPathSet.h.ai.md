# `include/xrpl/protocol/STPathSet.h` — Payment Path Representation

This header defines the three-class hierarchy used to represent payment paths in XRPL transactions: `STPathElement` (a single hop), `STPath` (an ordered sequence of hops), and `STPathSet` (the collection of alternate paths that the `Paths` field of a `Payment` transaction carries on the wire). Together they encode how a cross-currency payment can route through the order book and the trust-line graph from source to destination.

## The `STPathElement` Node

Each step in a payment path is an instance of `STPathElement`. A node is either an *account node* (rippling through an account's trust lines) or an *offer node* (matching against a DEX order book), and this distinction is captured by the `is_offer_` flag: an element whose `mAccountID` is the XRP "no account" sentinel is treated as an offer node; anything else is an account node. `isOffer()` and `isAccount()` expose this flag and are inverses.

The `Type` bitmask drives both on-wire encoding and runtime dispatch:

```
typeAccount  = 0x01   account field is present
typeCurrency = 0x10   a legacy IOU Currency follows
typeIssuer   = 0x20   an issuer AccountID follows
typeMPT      = 0x40   a Multi-Purpose Token MPTID follows
typeBoundary = 0xFF   separator between alternate paths in a PathSet
typeNone     = 0x00   path terminator
typeAsset    = typeCurrency | typeMPT   either kind of asset
```

`typeCurrency` and `typeMPT` are mutually exclusive — a single hop carries either a legacy IOU currency or an MPT, never both. The aggregate `typeAsset` constant lets callers check "does this node carry any asset kind?" without caring which. This design is the result of the MPT (Multi-Purpose Token) feature extension: rather than a separate node type, MPTs are grafted into the same bitmask namespace as currencies, preserving backward compatibility with the on-wire format.

The asset field is stored as `PathAsset`, a `std::variant<Currency, MPTID>` wrapper defined in `PathAsset.h`. Its `visit()` method avoids `std::get` boilerplate; the constructors in `STPathElement` use it to set or clear the right type bit. The explicit-type constructor (taking `unsigned int uType`) is the one the deserializer calls, and it defensively rewrites the asset bits by visiting the actual `PathAsset` variant, preventing a caller from passing a contradictory type mask.

### Hashing for Equality

`STPathElement` pre-computes and caches `hash_value_` at construction. Equality comparison short-circuits on both the `typeAccount` bit and the hash before doing the full field-by-field comparison — critical because path deduplication iterates over every existing path. The hash function itself (implemented in `STPathSet.cpp`) uses FNV-style multiply-XOR over the raw bytes of each field with intentionally different multipliers (257, 509, 911). A code comment there explicitly notes this does not need to be cryptographically secure — speed matters and a few bytes of each 160-bit ID would suffice. The hash handles the MPT/currency union correctly by calling `PathAsset::visit()` rather than trusting the type bits, because in some pathfinder intermediate states the bits may not have been fully set yet.

## The `STPath` Sequence

`STPath` wraps a `std::vector<STPathElement>` and exposes a standard container interface (`push_back`, `emplace_back`, `begin`, `end`, `front`, `back`, `operator[]`, `reserve`, `size`, `empty`). Most of this is thin delegation to the underlying vector.

The one non-obvious operation is `hasSeen(account, asset, issuer)`, which the pathfinding engine uses to detect cycles. Before adding an intermediate node to a path under construction, `Pathfinder::addLink()` calls this to check whether that (account, asset, issuer) triple has already appeared earlier in the same path. If it has, the candidate extension is discarded. The scan is linear, but the XRPL protocol caps path length so the vector is short in practice (typically 2–6 elements).

## `STPathSet` — The Serialized Type

`STPathSet` is the only class here that participates in the ledger type system. It inherits from `STBase` (the protocol's generic serialized type base) and overrides:

- `getSType()` — returns `STI_PATHSET`, identifying it to the serialization framework
- `add(Serializer&)` — writes the binary representation
- `isEquivalent(STBase const&)` — structural equality for ledger-object comparison
- `isDefault()` — returns `true` when the path set is empty (no `Paths` field needed)

The binary format used by `add()` and the deserializing constructor `STPathSet(SerialIter&, SField const&)` encodes each element as its type byte followed by the present optional fields in order: account (160 bits), asset (160 bits for currency, 192 bits for MPT), issuer (160 bits). Paths within the set are separated by a `typeBoundary` (0xFF) byte; the entire PathSet terminates with a `typeNone` (0x00) byte. Deserialization validates that no unknown type bits are set (via `iType & ~typeAll`) and that no path in the stream is empty; violations throw `std::runtime_error`.

### `assembleAdd()` Deduplication

`assembleAdd(base, tail)` is the pathfinder's workhorse for incrementally extending paths. It appends the candidate `base + tail` to the set, then reverse-iterates over the existing paths to check for a duplicate. If one is found, the newly added path is popped back off and `false` is returned. This avoids a temporary `STPath` allocation by pushing first and comparing against the live entry, at the cost of a vector mutation on failure. The choice of reverse iteration is a minor optimization: the most recently added paths are most likely to collide with a new candidate, since `addLink()` extends from similar bases repeatedly.

The `copy()` and `move()` private overrides, together with `friend class detail::STVar`, plug `STPathSet` into the `STVar` placement-allocation scheme that allows serialized type values to be stored inline in small buffers without heap allocation.

Both `STPathElement` and `STPath` and `STPathSet` inherit from `CountedObject<T>`, a lightweight CRTP mixin that tracks live instance counts for memory diagnostics — useful for detecting leaks in pathfinding code that creates and discards many transient path candidates.