# STXChainBridge.cpp

## Role and Purpose

`STXChainBridge` is the serialized protocol type that encodes a cross-chain bridge specification in the XRPL. A bridge is defined by exactly four pieces of information: the *door account* on the locking chain, the *asset* locked on that chain, the *door account* on the issuing chain, and the *wrapped asset* minted on that chain. This file implements the construction, serialization, deserialization, and JSON conversion of that four-tuple as a first-class `STBase`-derived field — meaning it participates in the same wire-format and field-tag infrastructure as every other serialized type in the ledger (amounts, accounts, blobs, etc.).

## Class Hierarchy and Field Architecture

`STXChainBridge` inherits from `STBase`, the root of all "Serialized Type" objects. `STBase` is a polymorphic type identified by an `SField` tag and participating in the ledger's binary serialization protocol. It also inherits from `CountedObject<STXChainBridge>` for diagnostic instance tracking.

The four member fields are themselves `STBase`-derived types:

- `lockingChainDoor_` and `issuingChainDoor_` — typed `STAccount`, wrapping an `AccountID`
- `lockingChainIssue_` and `issuingChainIssue_` — typed `STIssue`, wrapping an `Issue` (currency + issuer pair)

Each field is associated with a named `SField` constant (`sfLockingChainDoor`, `sfIssuingChainDoor`, etc.) rather than a positional index. This design reflects the XRPL's general approach: every serialized field carries an identity tag that appears in the wire format, making the binary representation self-describing and robust to field reordering.

## Construction Paths and Validation Tiers

The class exposes six construction paths representing three trust levels:

**Trusted / internal construction**: the `(AccountID, Issue, AccountID, Issue)` overload and the `STObject const&` overload perform no validation beyond member initialization. These are used when data is known valid — for example, when materializing a bridge object from already-validated ledger state.

**Binary deserialization**: `STXChainBridge(SerialIter& sit, SField const& name)` reads the four fields sequentially from a binary stream, delegating validation to the underlying `STAccount` and `STIssue` deserialization. The `static construct()` factory wrapping this constructor is the hook called by the `STVar` dispatch table in `STVar.cpp`, which maps `STI_XCHAIN_BRIDGE` to this type during wire-format decoding.

**JSON construction**: `STXChainBridge(SField const&, Json::Value const&)` is the most defensive path and the entry point for RPC/API input. It applies a layered validation strategy:
1. Checks that the JSON value is an object (`v.isObject()`).
2. Runs a `checkExtra` lambda that constructs a default-initialized bridge, serializes it to JSON, and compares the expected key set against the input — any unknown field name triggers an exception. This prevents silent discard of typo'd or future fields.
3. Validates that both door fields are JSON strings, then decodes them with `parseBase58<AccountID>`, returning `std::nullopt` for any malformed address rather than throwing internally.
4. Delegates issue parsing to `issueFromJson()`.

All failures throw `std::runtime_error` via the `Throw<>` wrapper (from `contract.h`), which consistently signals protocol-layer parse errors.

## Serialization: `add()` and `toSTObject()`

`add(Serializer&)` serializes the four fields in declaration order — locking door, locking issue, issuing door, issuing issue. Because `STXChainBridge` is itself a field (not a container like `STObject`), no length prefix or inner field-type tags are written here; the outer framing is already in place from `addFieldID()` called by the enclosing `STObject`.

`toSTObject()` exists as a bridge (pun aside) between this type and the generic `STObject` container. It constructs a fresh `STObject` tagged `sfXChainBridge` and copies the four sub-fields into it. This is needed wherever the ledger's generic object processing pipeline requires a flat `STObject` rather than the strongly-typed `STXChainBridge`.

## Memory Management: `copy()` and `move()`

The two private overrides satisfy the `STBase` small-buffer-optimization interface. `STVar` — the type-erased container used inside `STObject` and `STArray` — maintains an aligned internal buffer. If the concrete type fits within `max_size` bytes, it is placement-constructed into that buffer; otherwise it heap-allocates. The `emplace()` helper in `STBase` encapsulates this decision: given a size hint and a raw buffer, it either placement-news or heap-news the concrete object. `copy()` forwards to `emplace(n, buf, *this)` (copy-constructs) and `move()` forwards to `emplace(n, buf, std::move(*this))` (move-constructs). This pattern avoids virtual dispatch overhead for the common case where types are small enough to fit in-place.

## The `ChainType` Enum

The header defines `ChainType { locking, issuing }` and three static helpers — `otherChain()`, `srcChain(bool)`, `dstChain(bool)` — as inline functions. These allow callers dealing with cross-chain transaction processing to write direction-agnostic code by parameterizing on chain role rather than always naming locking or issuing explicitly. The `door(ChainType)` and `issue(ChainType)` accessors extend this polymorphism to field access.

## Equivalence and Default Detection

`isEquivalent()` uses `dynamic_cast` to ensure type identity before invoking the field-tuple comparison defined by `operator==` in the header. The `operator==` itself compares all four sub-fields via `std::tie`, which delegates to each field's own equality. `isDefault()` returns true only when all four members are in their field-default state — this is the hook used by serialization to skip optional fields that were never populated.

## Integration in the Type Registry

`STVar.cpp` registers `STXChainBridge` in the central `constructST()` switch via `case STI_XCHAIN_BRIDGE`. This means the general-purpose binary deserializer can construct a bridge value from a `SerialIter` by type ID alone, treating it identically to `STAmount`, `STAccount`, or any other primitive type. The `getSType()` override returning `STI_XCHAIN_BRIDGE` provides the runtime type tag that drives this dispatch.