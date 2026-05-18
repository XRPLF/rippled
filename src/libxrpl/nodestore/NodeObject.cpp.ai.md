# `NodeObject.cpp` — Immutable Ledger Storage Unit

`NodeObject` is the fundamental value type of the XRPL node store: a read-only triple of (type, hash, blob) representing a single serialized ledger entry. The implementation file is short because the design is deliberately constrained — instances are created once, never mutated, and identified entirely by their hash.

## Factory Method and the `PrivateAccess` Sentinel

The constructor is nominally `public` but effectively private. The header defines a nested `struct PrivateAccess` whose `explicit` default constructor makes it impossible to construct without naming it — and since `PrivateAccess` is declared `private` in the class, only code inside `NodeObject` can produce one. This is the standard portable workaround for the fact that C++ provides no mechanism to make `std::make_shared` a `friend`: the factory method `createObject()` passes a `PrivateAccess{}` token to `std::make_shared<NodeObject>(...)`, which satisfies the constructor signature while preventing any external caller from doing the same.

The caller passes `Blob&&` — a move reference — so ownership of the raw serialized payload transfers into the object with no copy. Combined with the `const` declarations on all three members (`mType`, `mHash`, `mData`), this guarantees that once a `NodeObject` is constructed its state cannot change. Every reference holder sees the same immutable view, which is important when the same object may be referenced from cache, the write queue, and in-flight read callbacks simultaneously.

## No Hash Verification

The header comment explicitly notes that no check is performed to confirm the hash actually matches the data. Validation is left to the caller. This keeps `NodeObject` lightweight and avoids redundant hashing when the object is reconstructed from a trusted backend store where the hash was already verified on write.

## Instance Counting via `CountedObject`

`NodeObject` inherits from `CountedObject<NodeObject>`, which wires into a global lock-free linked list of per-type counters. Each constructor increments an `std::atomic<int>` and the destructor decrements it, letting the node store report how many `NodeObject` instances are live at any time — useful for diagnosing cache pressure without adding any overhead to the fast path.