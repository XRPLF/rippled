# `PreimageSha256` — Cryptocondition Fulfillment Type 0

## Role in the System

`PreimageSha256` is the simplest fulfillment type defined by the [Interledger Crypto-Conditions draft RFC](https://tools.ietf.org/html/draft-thomas-crypto-conditions-02). It implements the classic hash-lock pattern: the *condition* is the SHA-256 digest of a secret byte string, and the *fulfillment* is the preimage itself. Revealing the preimage proves knowledge of the secret without requiring any asymmetric cryptography.

Within the XRPL `cryptoconditions` subsystem, five condition types are recognized (`preimageSha256`, `prefixSha256`, `thresholdSha256`, `rsaSha256`, `ed25519Sha256`). `PreimageSha256` is type 0 — the lowest-cost, lowest-complexity variant and the only one whose validation is entirely message-independent. Notably, `utils.h` states outright that its DER decoder "only implements the bare minimum needed to support PreimageSha256," making this class the foundational use case for the entire conditions detail subsystem.

## Class Design

`PreimageSha256` is a `final` concrete subclass of the abstract `Fulfillment` interface, which mandates four operations: `type()`, `fingerprint()`, `cost()`, `condition()`, and `validate()`. The class stores a single `Buffer payload_` — the raw preimage bytes.

Two constructors are provided: one accepting a `Buffer&&` (used by `deserialize()` after parsing) and one accepting a `Slice` (for constructing a fulfillment directly from in-memory data, such as when building a transaction). Both are `noexcept` because `Buffer` construction from these sources cannot throw.

## Deserialization

`deserialize()` is a static factory that parses a DER-encoded preimage fulfillment. The RFC specifies the wire format as a context-specific, primitive, tag-0 octet string. The parser enforces this strictly in sequence:

1. **Preamble check** — `parsePreamble()` extracts the type byte and length; both long-form tags and malformed lengths are rejected with specific error codes.
2. **Encoding class** — must be `contextSpecific` and `primitive` (bit patterns `0x80` and `~0x20`); any other combination yields `error::incorrect_encoding`.
3. **Tag value** — must be exactly `0`; any other tag yields `error::unexpected_tag`.
4. **Trailing data** — `s.size() != p.length` is checked after preamble consumption. If the input slice has bytes beyond what the length field declares, `error::trailing_garbage` is returned rather than silently ignoring them.
5. **Length cap** — the preimage must not exceed `maxPreimageLength` (128 bytes); overlong preimages yield `error::preimage_too_long`.

All error paths return `nullptr` via `std::error_code` rather than throwing exceptions, consistent with the rest of the rippled error-handling convention.

## Fingerprint and Condition Derivation

`fingerprint()` computes SHA-256 of the stored preimage using the OpenSSL-backed `sha256_hasher` from `xrpl/protocol/digest.h`. This hash is returned as a freshly allocated `Buffer` on every call. There is no caching, which is a minor performance consideration, but acceptable given the 128-byte cap and the relatively infrequent call pattern.

`condition()` assembles a `Condition` value from the type tag, `cost()`, and `fingerprint()`. This is the object that gets embedded in a transaction to declare what must be revealed to unlock funds.

## Cost and DoS Resistance

`cost()` returns `payload_.size()` — the raw byte length of the preimage. This directly follows the RFC's definition of cost for this type, and it serves a deliberate anti-DoS purpose: more expensive fulfillments require the submitter to pay proportionally more resources. The `maxPreimageLength` ceiling of 128 bytes is a policy bound that can be raised in future versions but must never be lowered, as that would retroactively invalidate previously accepted fulfillments. The same monotonicity constraint applies to `Fulfillment::maxSerializedFulfillment` (256 bytes) and `Condition::maxSerializedCondition` (128 bytes) throughout the subsystem.

## Message Irrelevance

`validate(Slice)` unconditionally returns `true`. This is counterintuitive at first glance, but is correct by design: a `PreimageSha256` fulfillment is self-validating. There is no message to verify a signature over — the act of providing the correct preimage *is* the proof. Higher-level validation (matching the derived condition fingerprint against the on-chain condition) is handled by the free function `match(Fulfillment const&, Condition const&)` in `Fulfillment.h`, which compares `type`, `cost`, and `fingerprint` fields. The `validate` overload taking both a fulfillment and a condition calls `match` first, then `validate` on the fulfillment itself — for `PreimageSha256`, the `match` call does all the real work.