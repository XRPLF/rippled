# `SHAMapMissingNode.h` — Missing-Node Exception and Map-Type Classification

This small header forms the error-signalling contract for the entire SHAMap subsystem. It defines two things: the `SHAMapType` enum that classifies what kind of Merkle-Patricia tree a `SHAMap` instance represents, and the `SHAMapMissingNode` exception that is thrown whenever tree traversal encounters a node whose data is not locally available.

## `SHAMapType` — Classifying Trees by Ledger Role

```cpp
enum class SHAMapType {
    TRANSACTION = 1,  // A tree of transactions
    STATE = 2,        // A tree of state nodes
    FREE = 3,         // A tree not part of a ledger
};
```

Every closed XRPL ledger contains two SHAMaps: a `TRANSACTION` tree holding the set of transactions included in that ledger (with or without metadata depending on the node type), and a `STATE` tree holding the full account-state database at that ledger's close. `FREE` trees are used for ephemeral or ad-hoc purposes — for instance, building a proposed transaction set during consensus — where the tree is not tied to a finalized ledger.

The numeric values (`1`, `2`, `3`) are meaningful beyond this header: they appear in wire-protocol serialization for sync packets, so they must not be changed. The accompanying `to_string()` helper converts enum values to human-readable labels (`"Transaction Tree"`, `"State Tree"`, `"Free Tree"`) for log output; the `default` branch guards against any future out-of-range integer by falling back to `safe_cast<std::underlying_type_t<SHAMapType>>(t)` rather than silently producing garbage text.

## `SHAMapMissingNode` — Signalling Incomplete Local State

```cpp
class SHAMapMissingNode : public std::runtime_error
```

`SHAMapMissingNode` is the primary signal that a SHAMap tree traversal failed because a required node is absent from local storage. This is a routine operational condition in a distributed ledger node: since nodes do not necessarily possess every historical ledger in full, a traversal of an older or partially-synced ledger can legally hit a gap. The exception propagates upward through multiple call frames, decoupling the traversal code in `SHAMap.cpp` from the policy decisions about how to handle gaps.

There are two constructors, each encoding a different point of failure during traversal:

- `SHAMapMissingNode(SHAMapType t, SHAMapHash const& hash)` — used when the tree descends toward a child whose **hash** is known (stored in the parent inner node) but whose backing data has not been loaded into memory. The `SHAMapHash` is a strong typedef over `uint256` defined in `SHAMapHash.h`, which adds type safety while carrying a SHA-512Half digest identifying the absent node.

- `SHAMapMissingNode(SHAMapType t, uint256 const& id)` — used when the failure is expressed at the level of an **item key** (a leaf identifier like an account ID or transaction ID) rather than a structural hash. This form appears when a lookup by key ID descends into the tree far enough to know the key should exist but cannot find the leaf node.

Both constructors build the `std::runtime_error` message eagerly: `"Missing Node: <tree type>: hash <hex>"` or `"Missing Node: <tree type>: id <hex>"`. This is intentional — the `what()` string is the primary data surface available to catch sites, and catch handlers in `LedgerCleaner.cpp`, `LedgerMaster.cpp`, and `RCLConsensus.cpp` all log it directly via `mn.what()`. Constructing the message at throw time rather than on demand is the right tradeoff here because the exception represents an abnormal situation that occurs rarely relative to normal traversal.

## Usage Pattern at Catch Sites

The `SHAMapMissingNode` exception surfaces in roughly two modes in the application layer:

**Recovery**: In `LedgerCleaner.cpp` and `LedgerMaster.cpp`, catching `SHAMapMissingNode` triggers a call into the inbound-ledger acquisition system (`getInboundLedgers().acquire()`), scheduling a peer fetch for the incomplete ledger. The exception message is logged at `warn` level so the operator can observe gaps.

**Fatal signalling**: In `RCLConsensus.cpp`, catching `SHAMapMissingNode` during consensus timer processing is treated as "should never happen" — the message is logged at `error` level and the exception is re-thrown via `Rethrow()`, ultimately crashing the consensus round. `Ledger.cpp` catches it silently and returns a failure result, treating any incomplete state tree as an invalid ledger rather than attempting recovery.

This layered catch strategy — enabled by the exception being a named type rather than a generic error code — lets each subsystem apply its own policy without the SHAMap traversal code being aware of those policies. The co-location of `SHAMapType` in this header is natural: the type is part of the exception's identity, passed to every `Throw<SHAMapMissingNode>` call in `SHAMap.cpp` so that log messages always carry the context of which tree (transaction vs. state) had the missing node.