# `OfferCreate.h` — Auto-Generated OfferCreate Transaction Wrapper

## Role in the System

This file is part of the `protocol_autogen` layer — a family of auto-generated headers under `include/xrpl/protocol_autogen/transactions/` that provide type-safe C++ wrappers for every XRPL transaction type. The directory contains over 70 such files, one per transaction kind. `OfferCreate.h` wraps `ttOFFER_CREATE` (type code 7), the transaction that places a limit order on XRPL's built-in decentralized exchange. It should never be edited by hand; regenerate it from the source schema instead.

The file lives within `namespace xrpl::transactions` and exposes exactly two classes: an immutable read accessor (`OfferCreate`) and a fluent construction helper (`OfferCreateBuilder`).

## `OfferCreate` — Immutable Transaction Wrapper

`OfferCreate` extends `TransactionBase`, which holds a `std::shared_ptr<STTx const>` and provides accessors for all universal transaction fields (account, sequence, fee, flags, memos, signers, delegate, etc.). `OfferCreate` narrows that to the fields specific to DEX offers.

Construction takes a `std::shared_ptr<STTx const>`. The constructor immediately validates the transaction's type tag against the class-level constant `txType = ttOFFER_CREATE` and throws `std::runtime_error` on mismatch. This is a runtime guard rather than a compile-time one — necessary because `STTx` is a polymorphic, schema-driven object decoded from wire bytes or JSON, so the type cannot be statically enforced at construction time.

### Fields and Why They Are Typed This Way

`getTakerPays()` and `getTakerGets()` both return `SF_AMOUNT::type::value_type`. On XRPL, `sfTakerPays` represents the asset the offer creator is willing to pay and `sfTakerGets` is what they want in return. Both are marked `soeREQUIRED` in the transaction schema and both are annotated with `@note This field supports MPT (Multi-Purpose Token) amounts`, reflecting the `mayCreateMPT` privilege listed in the class docblock. Since MPT amounts share the same `SF_AMOUNT` type as classic XRP/IOU amounts, no special branching is needed in the accessor — the underlying `STAmount` representation handles it.

Optional fields follow a paired `get`/`has` pattern:

- `getExpiration()` / `hasExpiration()` — a `uint32` ledger close time after which the offer is automatically considered expired. Callers must check `hasExpiration()` first, or use the returned `protocol_autogen::Optional<uint32_t>` which is `std::nullopt` when absent.
- `getOfferSequence()` / `hasOfferSequence()` — if present, the offer at this sequence number on the submitting account is canceled atomically when this offer is placed. This is the standard one-step cancel-and-replace mechanism.
- `getDomainID()` / `hasDomainID()` — an optional `uint256` identifying a permissioned domain, a newer XRPL feature allowing restricted trading venues.

The `protocol_autogen::Optional<T>` alias (defined in `Utils.h`) is a thin template that resolves to `std::optional<std::reference_wrapper<std::remove_reference_t<T>>>` for reference types and plain `std::optional<T>` for value types. For the scalar fields used here the distinction is transparent, but it matters for `STArray`-typed optional fields in other transaction wrappers.

## `OfferCreateBuilder` — Fluent Construction

`OfferCreateBuilder` extends `TransactionBuilderBase<OfferCreateBuilder>`, a CRTP base that stores a mutable `STObject object_{sfTransaction}`. The CRTP trick makes all common setters (`setFlags`, `setLastLedgerSequence`, `setMemo`, etc.) return `OfferCreateBuilder&` rather than `TransactionBuilderBase&`, enabling unbroken method chains across both base and derived setters.

**Intentional design detail:** `TransactionBuilderBase` deliberately does **not** call `object_.set(soTemplate)` during construction. The inline comment explains why: initializing with a template inserts `soeDEFAULT` field placeholders, and the `STTx` constructor later calls `applyTemplate()`, which throws "may not be explicitly set to default" for any field that carries a placeholder value. By keeping the `STObject` free-form and letting `STTx`'s own constructor apply the schema, the builder sidesteps that constraint entirely.

The primary constructor requires `account`, `takerPays`, and `takerGets` (the two `soeREQUIRED` offer-specific fields), with `sequence` and `fee` as `std::optional` parameters routed to the base class. A secondary constructor accepts an existing `std::shared_ptr<STTx const>` and copies its `STObject` representation into `object_`, enabling round-trip editing of a transaction received from elsewhere — it too validates the type tag before copying.

### Finalizing with `build()`

`build(PublicKey, SecretKey)` calls the protected `sign()` inherited from `TransactionBuilderBase`. That method serializes the object **without** signing fields (via `addWithoutSigningFields`), prepends the `HashPrefix::txSign` magic bytes, computes an ECDSA/Ed25519 signature with the provided key pair, writes `sfSigningPubKey` and `sfTxnSignature` back onto the `STObject`, then constructs a `std::shared_ptr<STTx>` from the now-final object. The returned `OfferCreate` wrapper is immediately validated against `ttOFFER_CREATE`, completing the construction loop.

## Relationship to Sibling Files

Every header in the `transactions/` directory follows this identical two-class layout (wrapper + builder). They are all generated from the same template, differing only in their field sets and type codes. Shared infrastructure lives in `TransactionBase.h` and `TransactionBuilderBase.h`; the `Utils.h` `Optional` alias and `STObjectValidation.h` validator are consumed indirectly through those bases. Nothing in `OfferCreate.h` is hand-authored beyond the generated output; the canonical source of truth for the field list is the upstream transaction schema definition.