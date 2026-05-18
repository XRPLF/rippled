# `STVector256` — Serialized Vector of 256-Bit Hashes

## Role in the System

`STVector256` is the XRPL serialized type for ordered lists of 256-bit values (`uint256`). It occupies type identifier `STI_VECTOR256` (wire code 19) in the ledger's binary type system and is how the protocol represents multi-hash fields in ledger objects and transactions — for example, the set of active amendments in a validator vote (`sfAmendments`), the page contents of a `DirectoryNode` (`sfIndexes`), and hash collections in other ledger structures (`sfHashes`).

Its existence is necessary because the XRPL protocol needs a way to pack an arbitrarily-sized list of 32-byte hashes into a single typed, named field that can be serialized, hashed, and round-tripped through JSON without losing field identity. A plain `std::vector<uint256>` would have no schema awareness; wrapping it in an `STBase` subclass gives it a `SField` name, a wire type, and all the hooks the serialization infrastructure requires.

## Inheritance and Design

The class inherits from both `STBase` and `CountedObject<STVector256>`. The `STBase` ancestry is the core contract — it provides the `SField` name binding, the virtual dispatch table for serialization (`add`), type identification (`getSType`), JSON export (`getJson`), equivalence checking (`isEquivalent`), and the placement-new buffer mechanism (`copy`/`move`) used by `detail::STVar` for small-object-optimized storage inside `STObject` fields. The `CountedObject<STVector256>` mixin adds zero-overhead instance counting through an atomic lock-free linked list, enabling diagnostic reporting of live `STVector256` instances at runtime.

The internal store is a simple `std::vector<uint256> mValue`. The class deliberately exposes the full mutation surface of `std::vector` (`push_back`, `insert`, `erase`, `resize`, `clear`, indexed access, iterators) rather than hiding the container behind a narrow interface. This pragmatic choice reflects that callers — ledger object builders, amendment processors, directory management code — routinely need to manipulate the list in-place, and wrapping every vector operation would add friction with no protocol safety benefit.

## Serialization Protocol

The binary wire format for `STVector256` is a variable-length (VL-prefixed) blob containing the hashes packed back-to-back with no padding or separators. The `add()` method calls `s.addVL(begin, end, size * 32)`, writing the byte count as a VL prefix followed by the raw 32-byte values. Two assertions guard this path: the field must be marked binary and its `fieldType` must be `STI_VECTOR256`, catching any accidental field misuse at debug time.

Deserialization in the `SerialIter` constructor is the inverse: it reads the VL-prefixed blob, checks that the byte count is an exact multiple of 32 (throwing `std::runtime_error` otherwise — a deliberate hard failure since a mis-aligned vector is a protocol violation, not a recoverable condition), then iterates through the slice constructing each `uint256` from its 32-byte substring. `mValue.reserve(cnt)` is called first to avoid reallocation during the loop.

## Copy and Move for STVar

`copy()` and `move()` are private virtuals called only by `detail::STVar`, the type-erased wrapper that `STObject` uses to hold heterogeneous field values without pointer indirection for small types. The `STBase::emplace` template checks whether the object fits in the caller-supplied buffer `buf` of size `n`; if it does, it placement-news the object there; if not, it heap-allocates. For `STVector256` this typically means heap allocation because the embedded `std::vector` exceeds the small-buffer size, but the machinery supports both paths uniformly.

## Value Semantics and Assignment

The class provides two `operator=` overloads taking `std::vector<uint256>` by value and by rvalue reference, enabling efficient move assignment when callers have a temporary vector ready. The explicit `operator std::vector<uint256>()` conversion produces a copy — it is marked `explicit` to prevent accidental implicit copies in generic contexts. The separate `setValue(STVector256 const&)` copies only the inner `mValue`, deliberately excluding the `SField` name; this mirrors the wider `STBase` design where assignment copies the value but callers control the field binding independently.

`isDefault()` returns `true` when the vector is empty, which determines whether the field is omitted during canonical serialization of `STObject` fields marked optional.

## JSON Representation

`getJson()` renders the vector as a JSON array of hex strings, one per `uint256` entry using `to_string()`. The `JsonOptions` parameter is accepted but unused — `STVector256` has no API-version-dependent presentation because a list of hashes has no ambiguity across API versions.

## Key Invariants

- The byte length of any deserialized blob must be divisible by 32; any other value is a hard protocol error thrown at construction time.
- `add()` requires the associated `SField` to be binary and typed `STI_VECTOR256`; mismatches are caught by `XRPL_ASSERT` in debug builds.
- An empty `STVector256` is the default state and is treated as absent when serializing optional fields.