# `src/libxrpl/conditions/Fulfillment.cpp`

## Role in the System

This file implements the public API surface for the XRPL cryptoconditions subsystem — an implementation of the [Crypto-Conditions Internet Draft (draft-thomas-crypto-conditions-02)](https://tools.ietf.org/html/draft-thomas-crypto-conditions-02). Cryptoconditions allow ledger objects to attach unforgeable commitments ("conditions") that can only be satisfied by presenting a matching "fulfillment." In XRPL they are used by the `EscrowFinish` transaction type to gate fund release.

The file provides three concerns: the factory method `Fulfillment::deserialize` that reconstructs a typed `Fulfillment` object from raw bytes, the `match` predicate that cryptographically ties a fulfillment to a condition, and the `validate` overloads that combine both checks into the single call used by transaction processing.

## Key Abstractions

`Fulfillment` (declared in `Fulfillment.h`) is a pure-virtual base: five virtual methods — `type()`, `fingerprint()`, `cost()`, `condition()`, and `validate(Slice)` — define everything a fulfillment must provide. `Condition` (in `Condition.h`) is a plain struct holding a `Type` enum, a `fingerprint` buffer, a `cost` integer, and a `subtypes` set for compound conditions. Equality on `Condition` compares all four fields.

Currently the only concrete subclass is `PreimageSha256`, defined entirely in `detail/PreimageSha256.h`. The four other condition types defined by the RFC (`prefixSha256`, `thresholdSha256`, `rsaSha256`, `ed25519Sha256`) are intentionally unimplemented — their `switch` arms immediately set `error::unsupported_type` and return. This is a deliberate scope decision reflecting XRPL's use case: preimage conditions suffice for escrow unlock semantics and avoid the consensus risk of implementing complex compound conditions.

## `Fulfillment::deserialize` — DER Decoding Chain

The binary format is ASN.1 DER using a CHOICE structure where the outer tag identifies the fulfillment type. The function performs a strict sequence of checks before delegating to type-specific parsing:

1. **Empty buffer** → `error::buffer_empty`  
2. **Preamble parse** via `der::parsePreamble`, which advances the `Slice` and populates a `Preamble` struct with a `type` byte, `tag`, and `length`. Long-form tags (≥ 31) are rejected with `error::long_tag`.  
3. **DER class checks** — the outer wrapper must be both constructed (`isConstructed`) and context-specific (`isContextSpecific`), matching the RFC's encoding of the CHOICE alternatives. A universal or application-class preamble signals `error::malformed_encoding`.  
4. **Buffer length invariant** — `p.length` must equal `s.size()` exactly. Greater means the buffer was truncated (`error::buffer_underfull`); less means extra bytes follow (`error::buffer_overfull`). This enforces a canonical one-fulfillment-per-buffer contract.  
5. **Size cap** — `p.length > maxSerializedFulfillment` (256 bytes) triggers `error::large_size`. The comment in `Fulfillment.h` emphasizes this limit may only increase: lowering it could retroactively invalidate previously accepted fulfillments, a ledger-integrity violation.  
6. **Type dispatch** — `p.tag` is compared against the `Type` enum values using `safe_cast<TagType>`. Only `preimageSha256` (tag 0) proceeds to `PreimageSha256::deserialize`; all others fail immediately.  
7. **Trailing garbage** — after `PreimageSha256::deserialize` advances the slice by `p.length`, any remaining bytes indicate `error::trailing_garbage`. This check exists because `parsePreamble` mutates the `Slice` in place, so consumed bytes literally disappear from `s`.

The entire function uses `std::error_code` rather than exceptions, consistent with how XRPL validates untrusted peer data throughout — no stack unwinding cost, no risk of exception escaping into consensus code.

## `match` — Two-Phase Cryptographic Binding

```cpp
bool match(Fulfillment const& f, Condition const& c)
```

The fast path checks `f.type() != c.type` first, discarding mismatched types before any hashing. If types agree, it calls `f.condition()` — which for `PreimageSha256` hashes the preimage payload with SHA-256 to reconstruct the fingerprint — and compares the derived `Condition` object against `c` using `operator==`, which checks type, cost, subtypes, and fingerprint together. This means a fulfillment that presents the correct preimage but was recorded under the wrong cost metadata will still fail matching.

## `validate` — Transaction-Level Entry Points

Two overloads are provided. The three-argument form `validate(f, c, m)` is the general case that sequences `match(f, c) && f.validate(m)`, where `m` is the transaction message being authorized. The two-argument form calls the three-argument one with an empty `Slice{}`. For `PreimageSha256`, `validate(Slice)` always returns `true` — the cryptographic proof is entirely in the preimage-to-condition binding, not in a per-message signature. The empty-message form is therefore the practical path for XRPL escrows, and the comment in `Fulfillment.h` explicitly recommends signature-type conditions use single-use keys when no message is provided.

## Design Tradeoff: Unsupported Types

Reserving `unsupported_type` rather than `unknown_type` for types 1–4 is a meaningful distinction: the implementation knows these types exist in the RFC but has chosen not to support them. Any binary data claiming to be one of those types fails immediately at the dispatch switch, well before any attempt to parse its contents. This prevents malformed payloads from triggering partial parsing of unsupported structures and gives callers a diagnostic error code that distinguishes "we recognize this but don't implement it" from "we have never seen this tag."