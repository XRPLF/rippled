# `NodeObject.h` — The Atomic Storage Unit of the XRPL Node Store

## Role in the System

`NodeObject` is the fundamental data carrier for the XRPL ledger's persistent key-value store. Every piece of ledger state — account tree nodes, transaction tree nodes, and ledger headers themselves — is stored as a `NodeObject` when written to or read from the node store. The class is deliberately minimal: a type tag, a 256-bit hash key, and a raw binary blob. There is no higher-level interpretation of the content at this layer.

The file-level comment calls out an intentional architectural decision: `NodeObject` lives in the `xrpl` namespace rather than the `xrpl::NodeStore` sub-namespace. This is because `NodeObject` is consumed broadly across the codebase — by the SHAMap layer, the ledger subsystem, and various serialization paths — while the rest of `NodeStore` is backend-plumbing. Hoisting it to the parent namespace avoids forcing every consumer to import the full nodestore API.

## The `NodeObjectType` Enum

The four meaningful values — `hotLEDGER`, `hotACCOUNT_NODE`, `hotTRANSACTION_NODE`, and the zero-value `hotUNKNOWN` — map to the three kinds of SHAMap content that the ledger needs to persist. The notable gap at value 2 (skipped between `hotLEDGER = 1` and `hotACCOUNT_NODE = 3`) reflects a historical removal. The `hotDUMMY = 512` sentinel is deliberately non-contiguous with the valid range: it signals an invalid or placeholder object and its value ensures it cannot be confused with a legitimate type by accident or by off-by-one arithmetic.

## Immutability and Factory Construction

All three data members — `mType`, `mHash`, and `mData` — are declared `const`. Once a `NodeObject` is constructed, it never changes. This is appropriate for content-addressed storage: the hash identifies the blob, and the blob is write-once.

The class enforces construction exclusively through the `createObject()` static factory. The mechanism used to prevent direct construction while still being compatible with `std::make_shared` is a well-known C++ idiom: a private nested `PrivateAccess` tag struct. The constructor is technically `public` (required so `std::make_shared` can call it), but it demands a `PrivateAccess` argument. Because `PrivateAccess` itself is a `private` nested type, only code inside `NodeObject` can construct one — making the constructor effectively private to all external callers. The comment in the header explicitly acknowledges this as a "hack" necessitated by the lack of a portable way to friend `std::make_shared`.

The factory signature is:

```cpp
static std::shared_ptr<NodeObject>
createObject(NodeObjectType type, Blob&& data, uint256 const& hash);
```

Taking `Blob&&` means the caller relinquishes ownership of the raw buffer, which is moved directly into `mData`. No copies of the payload occur during construction. All external references are then `std::shared_ptr<NodeObject>`, and the `Batch` type defined in `Types.h` is `std::vector<std::shared_ptr<NodeObject>>` — shared ownership is the consistent idiom throughout the nodestore layer.

## Hash Integrity

The class comment explicitly states: *"No checking is performed to make sure the hash matches the data."* The hash is accepted on trust from the caller. This is a deliberate performance tradeoff — re-hashing every object on retrieval would be prohibitively expensive given the volume of node reads during ledger processing. The correctness guarantee is maintained at higher layers (SHAMap traversal, ledger validation) rather than at the storage primitive.

## `CountedObject<NodeObject>` Integration

`NodeObject` inherits from `CountedObject<NodeObject>`, a CRTP utility that maintains a global atomic live-instance count. Every constructor call increments the counter; every destructor decrements it. This feeds into the diagnostic reporting system (`CountedObjects::getCounts()`), allowing operators to observe how many `NodeObject` instances are alive at any moment — a useful signal for cache sizing and memory pressure monitoring. The counter itself is lock-free (backed by `std::atomic<int>`), so the bookkeeping overhead on construction and destruction is negligible.

## Relationship to the Backend Layer

`NodeObject` is the payload type at every level of the nodestore stack. `Backend::fetch()` produces `std::shared_ptr<NodeObject>` instances; `Backend::store()` and `Backend::storeBatch()` consume them. The `Database` interface above `Backend` caches these shared pointers, and the SHAMap layer above that reads them to reconstruct tree nodes. Despite sitting at the bottom of this stack, `NodeObject` itself has no knowledge of any of these consumers — it is a pure value type with no callbacks, virtual functions, or upward dependencies.