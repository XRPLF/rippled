# `PaymentChannelClaim.h` — Auto-Generated Transaction Wrapper

## Purpose and Context

This file is part of the `xrpl/protocol_autogen` layer — a code-generated facade over XRPL's raw serialized transaction objects. It defines two classes for the `PaymentChannelClaim` transaction type (`ttPAYCHAN_CLAIM`, type code 15): an immutable reader (`PaymentChannelClaim`) and a fluent builder (`PaymentChannelClaimBuilder`).

In the XRPL payment channel lifecycle, a channel is opened with `PaymentChannelCreate` (type 13), optionally topped up with `PaymentChannelFund` (type 14), and then settled or drained by the destination party using `PaymentChannelClaim`. The claim transaction is the mechanism through which off-ledger micropayments are redeemed on-chain: the channel sender signs claim authorizations offline, and the receiver later submits one to the ledger, collecting XRP in a single on-chain transaction regardless of how many off-chain payments occurred.

## The Immutable Wrapper: `PaymentChannelClaim`

`PaymentChannelClaim` extends `TransactionBase`, which holds the underlying `std::shared_ptr<STTx const> tx_` and exposes all common transaction fields (account, sequence, fee, flags, memos, signers, etc.). The derived class adds the five payment-channel-specific fields:

- **`sfChannel`** (`getChannel()`) — a required 256-bit identifier (`SF_UINT256`) for the payment channel ledger object being claimed against. This is the only required field beyond the universal transaction fields.
- **`sfAmount`** (`getAmount()`) — an optional `SF_AMOUNT` specifying how much XRP to deliver to the destination. When present, funds are transferred from the channel to the destination account.
- **`sfBalance`** (`getBalance()`) — an optional `SF_AMOUNT` representing the cumulative total XRP claimed to date, as asserted by the sender's signed authorization. The ledger verifies this against the sender's off-ledger signature.
- **`sfSignature`** (`getSignature()`) — an optional variable-length blob (`SF_VL`) carrying the channel sender's cryptographic signature authorizing the claimed balance. Without this, only the channel owner can close the channel.
- **`sfPublicKey`** (`getPublicKey()`) — an optional `SF_VL` blob containing the public key used to verify `sfSignature`. Together, `sfSignature` and `sfPublicKey` encode the full off-ledger authorization.
- **`sfCredentialIDs`** (`getCredentialIDs()`) — an optional `SF_VECTOR256` array of credential identifiers, used when the channel's destination requires deposit pre-authorization via verified credentials.

Every optional field follows the same two-method pattern: `getX()` returns `protocol_autogen::Optional<T>` (which resolves to `std::optional<T>` for value types and `std::optional<std::reference_wrapper<T>>` for reference types via the `Utils.h` type alias), while `hasX()` returns a plain `bool`. This pattern avoids the exception that `STTx::at()` would throw for a missing field, and it makes client code self-documenting about optionality.

The constructor performs an eager type guard: it calls `tx_->getTxnType()` immediately and throws `std::runtime_error` if the wrapped `STTx` does not carry `ttPAYCHAN_CLAIM`. This prevents silent misuse where a caller might construct a `PaymentChannelClaim` around a `Payment` transaction and read garbage field values.

## The Builder: `PaymentChannelClaimBuilder`

`PaymentChannelClaimBuilder` inherits from `TransactionBuilderBase<PaymentChannelClaimBuilder>` using the Curiously Recurring Template Pattern (CRTP). The base class is parameterized on the concrete derived type so that every setter defined in the base (`setAccount()`, `setFee()`, `setSequence()`, `setFlags()`, etc.) can return `Derived&` rather than `TransactionBuilderBase&`, preserving fluent method chaining across both base and derived setters without virtual dispatch.

The builder holds a mutable `STObject object_{sfTransaction}` (declared in `TransactionBuilderBase`). A deliberate design decision documented in the base's constructor comment is that the builder never calls `object_.set(soTemplate)`. If it did, `STTx`'s `applyTemplate()` would encounter explicit placeholders for `soeDEFAULT` fields and throw. Instead, the builder keeps a "free" object and lets the `STTx` constructor run `applyTemplate()` cleanly, which properly inserts or validates required fields based on the transaction format registry.

`PaymentChannelClaimBuilder` offers two construction paths. The primary constructor takes `account`, `channel`, and optional `sequence` and `fee`, enforcing the minimum required data up front. The secondary constructor accepts an existing `std::shared_ptr<STTx const>` and copies it into `object_` — useful for modifying or re-signing a previously deserialized transaction. Both paths perform the same type check against `ttPAYCHAN_CLAIM`.

The `build()` method calls the protected `sign()` helper (which serializes the object with `HashPrefix::txSign`, computes the signature via `xrpl::sign()`, and sets `sfSigningPubKey` and `sfTxnSignature`), then moves `object_` into a new `STTx` and wraps it in a `shared_ptr<STTx const>`. That pointer is passed to the `PaymentChannelClaim` wrapper constructor, transferring ownership and transitioning from mutable builder to immutable reader. The `STTx` is const from this point forward; the `PaymentChannelClaim` reader cannot mutate it.

## Code Generation and Consistency

The `// This file is auto-generated. Do not edit.` header and the uniform structure shared across all ~70 sibling transaction headers (e.g., `PaymentChannelCreate.h`, `EscrowFinish.h`, `OfferCreate.h`) confirm this file is produced by a schema-driven generator. Every transaction type in the `xrpl::transactions` namespace follows the same `Foo` / `FooBuilder` dual-class pattern with identical field access conventions, making the generated API predictable and easy to use programmatically. The generator encodes field cardinality (`soeREQUIRED` vs `soeOPTIONAL`) directly into the C++ types — required fields return values directly; optional fields return `Optional<T>`.

The transaction is marked `Delegation::delegable`, meaning it can be submitted by a delegate account on behalf of the originating account using the `sfDelegate` field inherited from `TransactionBase`. The `sfCredentialIDs` optional field further reflects integration with XRPL's permissioned deposit pre-authorization system, allowing channels to be restricted to holders of specific verifiable credentials.