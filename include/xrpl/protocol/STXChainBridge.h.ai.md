# `STXChainBridge` — Cross-Chain Bridge Serialized Type

## Role in the System

`STXChainBridge` is a first-class serialized type in the XRPL protocol, analogous to `STAccount` or `STAmount`, that encodes the four-field specification of a cross-chain bridge. A bridge in XRPL Sidechains connects two independent ledgers: a *locking chain* (where XRP or tokens are locked in escrow) and an *issuing chain* (where a wrapped representation is minted). Each side of the bridge is described by a door account (`AccountID`) and an asset type (`Issue`). `STXChainBridge` bundles these four pieces — `LockingChainDoor`, `LockingChainIssue`, `IssuingChainDoor`, `IssuingChainIssue` — into a single, typed, serializable ledger object that appears in bridge-related transactions and ledger entries.

The class inherits from `STBase` (the abstract serialized-type base, identified by its `STI_XCHAIN_BRIDGE` type ID) and from `CountedObject<STXChainBridge>` (a debug/diagnostic mixin that tracks live instance counts via an atomic lock-free linked list managed by `CountedObjects`).

## The `ChainType` Abstraction

The most architecturally significant element of this header is the `ChainType` enum and its accompanying static helper trio:

```cpp
enum class ChainType { locking, issuing };
static ChainType otherChain(ChainType ct);
static ChainType srcChain(bool wasLockingChainSend);
static ChainType dstChain(bool wasLockingChainSend);
```

Cross-chain transfers are inherently directional, but the direction flips depending on which chain initiated the send. Witness servers record this as a boolean `wasLockingChainSend`. Rather than scattering `if (wasLockingChainSend)` branches through attestation and transaction processing code, `srcChain()` and `dstChain()` normalize that boolean into a `ChainType`, allowing callers to write:

```cpp
auto src = bridge.door(STXChainBridge::srcChain(wasLockingChainSend));
auto dst = bridge.door(STXChainBridge::dstChain(wasLockingChainSend));
```

This is why `door(ChainType)` and `issue(ChainType)` exist as dispatch accessors alongside the four named getters. The named accessors (`lockingChainDoor()`, etc.) are for contexts with fixed structural knowledge; the `ChainType`-parameterized accessors support generic code in `XChainAttestations` and transaction handlers that operate on either chain uniformly. All six of these accessors are `inline`, avoiding function call overhead on what are effectively field reads.

## Multiple Construction Paths

`STXChainBridge` provides seven constructors, reflecting the multiple ingress points for serialized types in XRPL:

- **Default / `SField`**: Creates an empty bridge bound to a field name, used when constructing container objects (`STObject`) before values are filled in.
- **`AccountID`+`Issue` quadruple**: Direct programmatic construction, used in tests and internal code.
- **`STObject const&`**: Extracts sub-fields from an existing generic `STObject`, used during ledger deserialization when the parent object has already been parsed.
- **`SerialIter&`**: Streams the four sub-fields directly from a binary iterator, the hot path for on-disk and network deserialization.
- **`Json::Value const&`**: Deserializes from API JSON input. This constructor includes a strict field-whitelist check — it constructs a canonical empty bridge, inspects its JSON keys, and throws `std::runtime_error` on any unrecognized key in the input. This "extra field detection" acts as an API contract guard, catching typos and version mismatches at parse time rather than silently ignoring unknown data.

## Serialization and Interoperability

`add(Serializer&)` writes the four sub-fields sequentially in canonical order: `LockingChainDoor`, `LockingChainIssue`, `IssuingChainDoor`, `IssuingChainIssue`. Each sub-field delegates to its own `STAccount::add()` or `STIssue::add()`, which prepend the XRPL field type/ID header before the raw bytes. The deserialization constructor mirrors this exactly by reading from `SerialIter` in the same order.

`toSTObject()` is a conversion that wraps the bridge's four fields into a generic `STObject`, needed when the bridge must participate in parts of the codebase that operate on `STObject` graphs (such as transaction metadata construction). This is a one-way lossy conversion in the sense that `STObject` carries dynamic field sets; `STXChainBridge` is the strongly-typed, canonical form.

## Comparison and Value Semantics

`operator==` and `operator<` are both implemented via `std::tie` across all four member fields in declaration order. This makes `STXChainBridge` usable as a key in `std::map` and `std::set`, which matters because bridge objects are used as lookup keys in cross-chain claim processing. The virtual `isEquivalent()` satisfies the `STBase` polymorphic comparison interface (used when bridges are stored as `STBase*` in containers); it performs a safe `dynamic_cast` before delegating to the concrete `operator==`.

The `value_type = STXChainBridge` self-alias and the `value()` accessor that returns `*this` follow a convention shared by all XRPL serialized types: template code that expects `.value()` to strip the ST wrapper works uniformly whether the underlying type is primitive (where `value_type` differs) or compound (where, as here, it is the type itself).

## Memory Management

`copy(n, buf)` and `move(n, buf)` override the `STBase` small-buffer optimization protocol. The inherited `STBase::emplace()` placement-news the object into a caller-provided buffer if it fits in `n` bytes, otherwise falls back to heap allocation. This pattern allows `STVar` (the internal type-erased variant used inside `STObject`) to avoid heap allocations for common small types, though `STXChainBridge` — with its four member fields — is larger than most primitives and will typically heap-allocate.