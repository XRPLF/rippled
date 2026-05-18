# `STArray.h` — Serialized Array of `STObject` Instances

`STArray` is one of the fundamental composite types in the XRPL protocol type system. It represents an ordered, variable-length sequence of `STObject` instances — the protocol's mechanism for encoding repeated structured sub-fields within transactions and ledger entries. Real-world examples include the `Memos` field (a list of memo objects attached to a transaction) and `SignerEntries` (the ordered list of signers in a multisig account). Like every `STBase`-derived type, `STArray` participates in both the canonical binary wire format and the JSON representation used by the RPC layer.

## Inheritance and Instance Tracking

`STArray` inherits from both `STBase` and `CountedObject<STArray>`. The `STBase` base class provides the field name (`SField`) association, the serialization interface (`add()`, `addFieldID()`), and the virtual `copy()`/`move()` protocol used by `detail::STVar`. The `CountedObject<STArray>` mixin increments an atomic counter on every constructor call and decrements on destruction, feeding the diagnostics surface exposed through `GetCounts`. This imposes no runtime cost on hot paths — the counter is a single atomic increment/decrement per object lifetime.

The storage is a plain `std::vector<STObject>` aliased as `list_type`. Because `STObject` is itself a concrete class (not a pointer type), the vector holds values directly, which means copy and move operations on `STArray` copy or move all elements. The `STBase` comment in the base class warns against putting `STBase`-derived objects in ordinary vectors due to copy-assignment name-erasure semantics; `STArray` sidesteps this for its *elements* by storing concrete `STObject` values rather than base-class references.

## Binary Deserialization

The `SerialIter`-based constructor is the most architecturally significant piece of this class. XRPL's binary format encodes an array as a sentinel-terminated stream: each element begins with its field ID and ends with a per-object terminator (`STI_OBJECT, 1`), and the entire array ends with an array-level terminator (`STI_ARRAY, 1`). The constructor loops calling `sit.getFieldID(type, field)` and breaks when it sees the array terminator. Three classes of ill-formed input are rejected explicitly:

- An `STI_ARRAY/1` marker that is not the array-end — caught by the loop's break condition.
- An `STI_OBJECT/1` marker appearing at the array element level (an end-of-object sentinel leaked outside its scope) — rejected with `"Illegal terminator in array"`.
- A non-`STI_OBJECT` field type inside the array — rejected with `"Non-object in array"`, enforcing the invariant that every array element is a typed object, never a scalar.

The `depth` parameter passed through to `v_.emplace_back(sit, fn, depth + 1)` is a recursion guard. `STObject`'s own deserialization constructor enforces a maximum depth of 10; since `STArray` increments the counter before passing it down, a maximally nested structure hits the limit before it can blow the call stack. This is a deliberate defense against crafted payloads.

After constructing each `STObject`, `applyTemplateFromSField(fn)` is called immediately. The outer `SField` associated with each array element (e.g., `sfMemo`, `sfSigner`) carries schema information (an `SOTemplate`), and applying it validates the just-deserialized object against the known field layout for that type. This call can throw, and the comment acknowledges it — a partially constructed array is simply abandoned via the exception unwind.

## Binary Serialization

`add()` mirrors the deserializer precisely: for each element it writes `object.addFieldID(s)` (the element's own field ID prefix), then `object.add(s)` (the element body including the inner `STI_OBJECT/1` per-object end marker from `STObject::add()`), then explicitly appends `s.addFieldID(STI_OBJECT, 1)`. The outer `STI_ARRAY/1` array-end marker is *not* written here — that is the enclosing `STObject`'s responsibility. This split keeps each level responsible only for its own content, matching the XRPL convention that a container writes its body but its parent closes the outer scope.

## JSON Representation

`getJson()` produces a JSON array where each element is a JSON object keyed by the inner `STObject`'s field name — for instance `[{"Memo": {...}}, {"Memo": {...}}]`. The outer key is always present, making the field type unambiguous to JSON consumers. Objects whose `getSType()` returns `STI_NOTPRESENT` are silently skipped, which handles optional or absent inner objects without introducing nulls into the output.

## Move Semantics

The explicit move constructor and move assignment operator in the `.cpp` file exist because `STBase` stores an `SField const*` that is *not* moved by default (the default move constructor of `STBase` would leave the field name in the moved-from state). Both operations explicitly call `STBase(other.getFName())` or `setFName(other.getFName())` before moving `v_`, preserving the `SField` association on the destination. The `copy()` and `move()` virtual overrides use `STBase::emplace()` to placement-construct into a caller-provided fixed-size buffer if the object fits, or heap-allocate otherwise — the mechanism by which `detail::STVar` implements small-buffer optimization for the XRPL type system.

## Design Notes

The `sort()` method accepts a raw C function pointer `bool (*compare)(STObject const&, STObject const&)` rather than a `std::function` or template parameter. This keeps the interface simple for the handful of call sites that need it (primarily sorting `SignerEntries` by account ID for canonical transaction form), without introducing template instantiation in the header.

`isDefault()` returns `true` for an empty array. This matters because the enclosing `STObject` serializer can skip default-valued fields, so an empty `STArray` contributes nothing to the binary encoding — consistent with how absent optional array fields are represented in the ledger.