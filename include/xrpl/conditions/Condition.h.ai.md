# `include/xrpl/conditions/Condition.h`

## Role in the System

This header defines the `Condition` type at the heart of the XRPL *cryptoconditions* subsystem — an implementation of [draft-thomas-crypto-conditions-02](https://tools.ietf.org/html/draft-thomas-crypto-conditions-02), the IETF specification that underpins XRPL's *EscrowCreate* / *EscrowFinish* transaction logic. A `Condition` is the commitment half of the two-part cryptoconditions protocol: it encodes *what* must be proven (a fingerprint and the type of proof required) without revealing *how* to prove it. The complementary `Fulfillment` type (in `Fulfillment.h`) carries the proof itself.

## The `Type` Enum

The `Type` enum maps directly onto the five condition types defined by the RFC — `preimageSha256 (0)` through `ed25519Sha256 (4)`. The underlying `uint8_t` tag values matter: they are the actual DER context-specific constructed tags used in the wire encoding, so the enum's integer values are protocol constants, not implementation conveniences. In practice, XRPL currently only supports `preimageSha256`; deserializing any other type results in `error::unsupported_type`, which is an intentional, forward-compatible design — the type taxonomy is fully declared so the ledger can recognise all RFC types and reject unsupported ones cleanly, rather than silently treating them as unknown.

## The `Condition` Class

`Condition` is a plain-data aggregate of four fields:

- **`type`** — which of the five RFC condition types this is.
- **`fingerprint`** — a 32-byte `Buffer` that uniquely identifies the condition within its type. For `preimageSha256`, this is the SHA-256 hash of the preimage; the RFC guarantees that two conditions of the same type with the same fingerprint are equivalent.
- **`cost`** — a `uint32_t` upper-bounding the computational cost of verifying any fulfillment for this condition. For preimage conditions this equals the preimage length in bytes. The cost field lets a validator reject economically ruinous fulfillments before performing the actual verification.
- **`subtypes`** — a `std::set<Type>` that is only populated for compound condition types (prefix, threshold). It lists all condition types reachable through the compound tree, enabling validators to reject compound conditions that embed unsupported sub-types without recursing into the full structure.

The default constructor is `= delete`. Every `Condition` must be constructed with all three essential fields — `type`, `cost`, and `fingerprint` — preventing partially-initialised instances from being created accidentally. Two constructors are provided: one taking `Slice fp` (copies the fingerprint bytes) and one taking `Buffer&& fp` (moves ownership), covering both the serialisation path (where the buffer was freshly parsed) and any in-memory construction path.

## `maxSerializedCondition`

The class-level constant `maxSerializedCondition = 128` caps the accepted wire size of a binary condition. The comment in the header makes an important asymmetry explicit: the value may increase in future versions to accommodate larger condition types, but it may *never decrease*, because doing so would invalidate conditions already accepted onto the ledger. This is a ledger-consensus invariant encoded as a comment — the numeric constant and its immutability contract must be kept in sync across protocol upgrades.

## Deserialization

`Condition::deserialize(Slice s, std::error_code& ec)` is a static factory that parses a DER-encoded condition from a raw byte slice. The implementation (in `Condition.cpp`) first reads the DER preamble to extract the context tag, maps it to one of the five `Type` values, then dispatches to `detail::loadSimpleSha256` or returns `error::unsupported_type` for compound types. The `loadSimpleSha256` helper extracts the 32-byte fingerprint and the 4-byte cost integer from the SEQUENCE fields, enforcing exact sizes (e.g., fingerprint must be exactly 32 bytes) and rejecting trailing garbage.

Error reporting follows XRPL's standard pattern of an output `std::error_code&` parameter. `error.h` integrates with `<system_error>` via the `is_error_code_enum` specialisation so that condition errors participate in the standard C++ error-category system.

## Comparison Operators

The free `operator==` compares all four fields — type, cost, subtypes, and fingerprint. The fingerprint comparison relies on `Buffer`'s own `operator==`. Including `cost` in equality is deliberate: two conditions with identical fingerprints but different costs would be semantically distinct (different verification budgets) and must not be treated as equivalent. This also means `Condition` equality is well-defined for use in `std::set` (if a comparator is provided) or flat comparison, but `Condition` has no `operator<`, so it cannot be stored directly in an ordered container without a custom comparator.

## Relationship to `Fulfillment`

`Fulfillment` is the abstract counterpart. Its virtual `condition()` method returns a `Condition` value object computed deterministically from the fulfillment — making `Condition` the canonical, storable commitment that lives on the ledger, while `Fulfillment` is the transient proof supplied in a transaction. The `match()` and `validate()` free functions in `Fulfillment.h` tie them together: `validate()` checks that a fulfillment's derived condition matches the stored condition *and* that the fulfillment's cryptographic claim holds against the message. This separation means the ledger never needs to store or recompute fulfillments after an escrow is created — only the compact `Condition` is persisted.