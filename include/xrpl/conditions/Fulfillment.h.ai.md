# `include/xrpl/conditions/Fulfillment.h`

## Role in the System

This header defines the abstract interface for the *fulfillment* side of the [Crypto-Conditions RFC](https://tools.ietf.org/html/draft-thomas-crypto-conditions-02) as implemented in the XRPL. Crypto-conditions are a two-part scheme: a **condition** is a compact commitment (hash + cost + type tag), and a **fulfillment** is the preimage or cryptographic proof that satisfies it. `Fulfillment.h` owns the polymorphic base type `Fulfillment` that all concrete condition types inherit from, plus the free functions that tie fulfillments and conditions together at the point of use.

In the XRP Ledger, crypto-conditions gate escrow releases: a sender locks funds with a condition, and the claimer must supply the correct fulfillment to unlock them. This header, together with `Condition.h`, forms the complete public surface of the `xrpl::cryptoconditions` module.

## `Fulfillment` Abstract Base

`Fulfillment` is a pure-virtual struct with ownership semantics enforced via `std::unique_ptr`. Every concrete type must implement four methods:

- **`type()`** — returns the `Type` enum tag (`preimageSha256`, `ed25519Sha256`, etc.) so that dispatch can be done without `dynamic_cast`.
- **`fingerprint()`** — returns an opaque `Buffer` that is type-specific. For `PreimageSha256` this is `SHA-256(preimage)`; for signature types it would be a hash of the public key material. The fingerprint is meaningful only within a type — two conditions of different types with the same fingerprint bytes are not equivalent.
- **`cost()`** — a `uint32_t` measure of the computational work required to evaluate the fulfillment. For preimage conditions it equals the preimage length; for threshold conditions it accumulates sub-condition costs. The ledger uses this to bound resource consumption per transaction.
- **`validate(Slice data)`** — checks whether the fulfillment is internally self-consistent given a message. For preimage conditions the message is irrelevant (the fulfillment *is* its own proof) and this always returns `true`; for signature-based conditions the message would be the signed payload.
- **`condition()`** — derives the matching `Condition` value from the fulfillment deterministically. The derived condition can then be compared against the committed condition stored on-ledger.

The design choice to derive the condition *from* the fulfillment, rather than storing it, is intentional: it makes the relationship provably deterministic and removes a whole class of inconsistency bugs where a stored condition could disagree with the actual fulfillment.

## Deserialization

`Fulfillment::deserialize(Slice s, std::error_code& ec)` is the sole entry point for loading a fulfillment from its DER-encoded binary form. The implementation (in `Fulfillment.cpp`) reads the outer ASN.1 preamble, inspects the context-specific constructed tag to determine the type, enforces the `maxSerializedFulfillment = 256` byte cap, and delegates to the appropriate concrete deserializer. The size cap is declared with an explicit `@note` that it must never *decrease* — doing so would retroactively invalidate fulfillments already stored in closed ledgers.

Currently only `PreimageSha256` is fully implemented; the remaining four RFC types (`prefixSha256`, `thresholdSha256`, `rsaSha256`, `ed25519Sha256`) all return `error::unsupported_type` immediately. This is a deliberate scope limitation — XRPL escrow only requires preimage conditions, and partially implementing the other types would risk subtle RFC non-compliance.

## Free Functions: `match` and `validate`

Three free functions compose the fulfillment/condition check:

`match(f, c)` first fast-checks the type tag, then derives `f.condition()` and does a full `Condition` equality comparison (type + cost + fingerprint + subtypes). This two-step approach avoids computing `SHA-256` if the type tags already differ.

`validate(f, c, m)` calls `match` followed by `f.validate(m)`, combining structural match and cryptographic self-validation in one call. A second overload `validate(f, c)` passes an empty `Slice` as the message, serving the **cryptoconditional trigger** pattern where conditions carry no external message context. The inline documentation specifically notes that signature-type conditions used as triggers should employ single-use keys — a security warning meaningful for ledger integrators.

## Equality Operators

`operator==` compares type, cost, and fingerprint. A `FIXME` comment acknowledges that compound conditions (threshold, prefix) also require comparison of the `subtypes` bitset, which `Condition::operator==` does include. This means comparing two `Fulfillment` objects directly may produce false positives for compound types if they share the same fingerprint but differ in subtypes — a known gap to be closed when compound fulfillments are implemented.

## Relationship to Concrete Implementations

`PreimageSha256` (in `detail/PreimageSha256.h`) is the canonical example of the pattern. It stores a raw `Buffer` payload, computes `fingerprint()` on demand via `sha256_hasher`, and reports `cost()` as payload length. Its `validate(Slice)` ignores the message entirely — the act of knowing the preimage is sufficient proof. This makes the base class's `validate` contract slightly unusual: the method is formally about message validation, but for preimage types it degenerates to a tautology.

The `detail/error.h` error taxonomy covers the full range of parse failures: malformed DER preamble, under/overfull buffers, trailing garbage, oversized preimages, and unknown type tags. All deserialization paths propagate errors through `std::error_code` rather than exceptions, consistent with the rest of `libxrpl`.