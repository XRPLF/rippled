# `TransactionBuilderBase.h` — CRTP Base for Auto-generated Transaction Builders

## Role and Context

`TransactionBuilderBase` is the shared ancestor for all 85 auto-generated transaction builder classes in the `xrpl::transactions` namespace. It lives in `include/xrpl/protocol_autogen/`, a directory populated at CMake configure time by `scripts/generate_tx_classes.py`. Derived builders such as `PaymentBuilder` and `AccountSetBuilder` each inherit from `TransactionBuilderBase<DerivedBuilder>` via the Curiously Recurring Template Pattern (CRTP), gaining a full set of common-field setters without any virtual dispatch overhead.

The file is the counterpart to `TransactionBase.h`, which provides immutable read accessors over a finished `STTx`. The split is intentional: mutable construction state lives entirely in the builder; once `build()` is called on a derived class the object is moved into an `STTx` and ownership transfers to the immutable wrapper.

## Core Design: CRTP and the "Free Object" Invariant

The template parameter `Derived` is the concrete builder class. Every setter casts `*this` to `Derived&` before returning, so call chains like

```cpp
PaymentBuilder builder(account, dest, amount);
builder.setFee(drops).setLastLedgerSequence(seq).setMemos(memos);
```

stay typed as `PaymentBuilder&` throughout — no slicing, no intermediate base references.

The single data member is:

```cpp
STObject object_{sfTransaction};
```

This constructs a *free* `STObject` tagged with the `sfTransaction` SField but **without** an `SOTemplate` bound to it. The constructor comment explains why this matters: calling `object_.set(soTemplate)` would cause `STObject` to pre-create `STBase` placeholder entries for every `soeDEFAULT` field in the transaction's schema. Later, when `STTx(STObject&&)` calls `applyTemplate()` internally, those pre-created defaults would trigger the exception "may not be explicitly set to default". By keeping the object free, the builder accumulates only the fields actually set by the caller; `applyTemplate()` then inserts defaults for everything else cleanly during `STTx` construction.

## Constructor

The four-parameter constructor initialises the mandatory fields common to every XRPL transaction: `sfTransactionType` (the `uint16` discriminator), `sfAccount`, and the two optional-but-nearly-universal fields `sfSequence` and `sfFee`. Derived constructors forward their required arguments here and then set their own type-specific fields via `object_[sfXxx] = value`.

## Common Field Setters

All setters follow the same pattern — assign a field on `object_` and return `static_cast<Derived&>(*this)` — so there is nothing architecturally surprising about most of them. The one exception worth highlighting is `setTicketSequence()`:

```cpp
object_[sfSequence] = 0u;
object_[sfTicketSequence] = value;
```

The XRPL protocol requires that when a ticket is consumed, the transaction's regular sequence number must be exactly 0. `setTicketSequence()` enforces this invariant in one atomic call rather than leaving the caller to remember to also zero out `sfSequence`.

`setMemos()` and `setSigners()` use `object_.setFieldArray()` instead of the `operator[]` shorthand because `STArray` fields require the dedicated accessor path.

`setDelegate()` sets `sfDelegate`, which enables delegated transaction submission — a relatively recent addition allowing one account to act on behalf of another with explicit permission.

## The `sign()` Method

`sign()` is `protected`, so only derived builders can expose it (typically through a public `build()` method). Its signing pipeline follows the XRPL specification precisely:

1. Write the public key into `sfSigningPubKey` as a variable-length blob.
2. Create a `Serializer`, prepend `HashPrefix::txSign` (a four-byte domain-separation prefix), and append the serialized object *excluding* its signing fields via `addWithoutSigningFields()`.
3. Call `xrpl::sign(publicKey, secretKey, s.slice())` to produce the ECDSA/Ed25519 signature.
4. Write the signature into `sfTxnSignature`.

This ordering — public key first, signature last — matches the order in which the fields appear in the canonical XRPL binary serialisation, which is important because `STTx` validates the signature immediately on construction when given a populated `STObject`.

## Relationship to `STTx` and the Builder Terminal

Derived builders expose a `build()` method such as:

```cpp
Payment build(PublicKey const& pk, SecretKey const& sk) {
    sign(pk, sk);
    return Payment{std::make_shared<STTx>(std::move(object_))};
}
```

`STTx(STObject&&)` moves the free object in, runs `applyTemplate()` to validate and fill defaults, and computes the transaction hash. From that point the `STObject` is consumed and the builder is invalid. The returned `Payment` (a `TransactionBase` subclass) holds a `shared_ptr<STTx const>` and provides only read accessors — enforcing immutability for signed, finalised transactions.

## Parallel with `LedgerEntryBuilderBase`

`LedgerEntryBuilderBase<Derived>` in the same directory mirrors this design exactly for ledger-entry construction. Both share the "free object" invariant, both use CRTP for chainable setters, and both delegate the actual template application to the downstream type (`STTx` / `STLedgerEntry`). The parallel structure reflects that the two hierarchies were generated by sibling scripts and are maintained together.