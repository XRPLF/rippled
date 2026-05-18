# `Condition.cpp` — Cryptocondition Deserialization

## Role in the System

This file implements the binary deserialization entry point for the XRPL cryptoconditions subsystem, translating a raw byte buffer into a validated `Condition` object. Cryptoconditions (defined in [draft-thomas-crypto-conditions](https://tools.ietf.org/html/draft-thomas-crypto-conditions-02)) are a cross-chain interoperability primitive used in XRPL to express spending requirements—most commonly for Escrow transactions. A `Condition` is the commitment (hash plus metadata) that an off-ledger party must satisfy by submitting a matching `Fulfillment`.

The file sits alongside the `Fulfillment` hierarchy but focuses on the far simpler task of decoding the *commitment side* of the pair. A condition contains no secret—it is stored on-ledger and must be decoded quickly and safely whenever a fulfillment is checked.

## Binary Encoding and the CHOICE Dispatch

The on-wire format is ASN.1 DER, using CONTEXT-SPECIFIC CONSTRUCTED tags to distinguish condition types (a DER `CHOICE` encoding). The outer tag value identifies the type:

- `[0]` → `preimageSha256` (a `SimpleSha256Condition`)
- `[1]` → `prefixSha256` (a `CompoundSha256Condition`)
- `[2]` → `thresholdSha256` (a `CompoundSha256Condition`)
- `[3]` → `rsaSha256` (a `SimpleSha256Condition`)
- `[4]` → `ed25519Sha256` (a `SimpleSha256Condition`)

`Condition::deserialize()` reads the outer preamble first, validates that it is CONTEXT-SPECIFIC and CONSTRUCTED (the RFC encoding for the outer `CHOICE` wrapper), and then dispatches on `p.tag`. Only tag `0` (`preimageSha256`) actually proceeds to parsing; tags 1–4 immediately return `error::unsupported_type`. This is a deliberate scope limitation—XRPL currently only uses preimage conditions for Escrow, and supporting the compound types (`prefixSha256`, `thresholdSha256`) would require parsing recursive structures that significantly expand the attack surface. The unknown-type path (`default:`) is kept separate from the unsupported-type path so that future RFC extensions can be distinguished from currently-known-but-unimplemented types in error logs.

An overall size gate (`maxSerializedCondition = 128` bytes, defined in `Condition.h`) is checked before type dispatch. This limit exists to prevent allocating unbounded memory before any structural validation can occur, and its comment in the header explicitly notes it must never *decrease* across protocol versions, since doing so could invalidate previously-accepted conditions.

## The `loadSimpleSha256` Inner Parser

The file-private `detail::loadSimpleSha256()` handles the inner DER `SEQUENCE` for the `SimpleSha256Condition` structure, which contains exactly two fields:

1. **Fingerprint** (tag `[0]`, OCTET STRING, exactly 32 bytes) — the SHA-256 hash identifying the condition.
2. **Cost** (tag `[1]`, INTEGER, `uint32_t`) — the computational/storage cost for fulfilling the condition.

The function deliberately re-validates the inner preamble as PRIMITIVE and CONTEXT-SPECIFIC (not CONSTRUCTED), because the outer condition wrapper is CONSTRUCTED while the inner fields are PRIMITIVE. Conflating these would be a parsing bug. Each field is consumed from the `Slice` in order using the `der::` utilities from `detail/utils.h`: `parsePreamble()` advances the slice by the preamble bytes, `parseOctetString()` copies the next `count` bytes into a `Buffer`, and `parseInteger<uint32_t>()` decodes the cost as a big-endian two's complement value.

After both fields are consumed, the function checks `s.empty()` to reject trailing garbage. This matters because `loadSimpleSha256` is called with a sub-`Slice` exactly sized to `p.length` from the outer preamble—so any remaining bytes after the cost integer indicate a malformed encoding rather than unrelated data.

The type-specific cost validation for `preimageSha256` is the only business-logic check: `cost > PreimageSha256::maxPreimageLength` (128 bytes) triggers `error::preimage_too_long`. For `preimageSha256`, cost equals the preimage length in bytes (as seen in `PreimageSha256::cost()` in `PreimageSha256.h`), so a cost exceeding 128 would mean no conforming fulfillment could ever satisfy the condition on this ledger.

## Error Handling Design

All errors are communicated through `std::error_code& ec` — no exceptions are thrown anywhere in this path. On any error, the function immediately returns an empty `std::unique_ptr<Condition>`. This is the standard XRPL pattern for performance-critical deserialization that runs inside transaction processing, where exception overhead is unacceptable and callers are expected to always check the error code. The `cryptoconditions::error` enum is integrated with `<system_error>` via the `is_error_code_enum` specialization in `error.h`, providing descriptive category-level messages.

Ownership is managed entirely via `std::unique_ptr<Condition>`, constructed at the very end of `loadSimpleSha256` only after all validation passes. The `Condition` struct stores the fingerprint as a heap-allocated `Buffer`, cost as a plain `uint32_t`, and type as the `Type` enum — a minimal, value-semantic representation with no shared ownership needed.

## Relationship to Sibling Files

`Condition.cpp` is the read path; it never *validates* that a fulfillment satisfies a condition — that responsibility belongs to `Fulfillment::validate()` in the `Fulfillment` hierarchy. `PreimageSha256` in `detail/PreimageSha256.h` provides both the fulfillment-side deserialization and the `maxPreimageLength` constant referenced here. The DER utility functions in `detail/utils.h` are header-only and shared with `PreimageSha256::deserialize()`, keeping the parsing primitives in one place rather than duplicated across condition and fulfillment parsing.