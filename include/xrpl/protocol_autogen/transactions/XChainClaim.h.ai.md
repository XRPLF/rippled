# `XChainClaim.h` — Cross-Chain Claim Transaction Wrapper

## Role in the System

This auto-generated header defines the final transaction in the XRPL cross-chain bridge transfer workflow, governed by the `featureXChainBridge` amendment. Cross-chain transfers on XRPL proceed through three discrete on-ledger steps: a `XChainCreateClaimID` (type 41) to reserve a claim slot on the destination chain, a `XChainCommit` (type 42) to lock funds on the source chain, and finally `XChainClaim` (type 43) to release those funds to their destination. This file covers that last step.

The file lives under `include/xrpl/protocol_autogen/transactions/`, a directory that contains one header per transaction type, all generated from the same schema-driven template. The identical structure across `XChainCommit.h`, `XChainCreateClaimID.h`, and `XChainClaim.h` makes this clear: do not edit these files by hand.

## Two-Class Design: Wrapper and Builder

The header defines two classes with sharply separated responsibilities.

`XChainClaim` is a read-only wrapper. It holds a `std::shared_ptr<STTx const>` inherited from `TransactionBase` and exposes strongly-typed getters for the fields specific to this transaction type. Once an `XChainClaim` object exists, the underlying transaction data is immutable — this is intentional, because a signed `STTx` must not be mutated without invalidating the signature. Passing `const` around a shared pointer would normally be easy to circumvent, but by taking `std::shared_ptr<STTx const>` (pointer-to-const), the wrapper enforces immutability at the type system level.

`XChainClaimBuilder` is the construction path. It inherits from `TransactionBuilderBase<XChainClaimBuilder>`, a CRTP base that provides all common transaction fields (`sfAccount`, `sfSequence`, `sfFee`, `sfLastLedgerSequence`, `sfMemos`, `sfDelegate`, etc.) via chainable setters. The CRTP pattern is the reason these shared setters can return `XChainClaimBuilder&` rather than `TransactionBuilderBase&` — each setter in the base casts `*this` to `Derived&` before returning, enabling uninterrupted method chaining without repeated downcasts at the call site.

## Field Structure

`XChainClaim` carries four required fields and one optional:

- `sfXChainBridge` — identifies which bridge (locking/issuing chain door accounts and asset types) this claim is against. Its C++ type is `SF_XCHAIN_BRIDGE::type::value_type`, resolving to `STXChainBridge`.
- `sfXChainClaimID` — a `uint64` that references the specific claim ID created by the earlier `XChainCreateClaimID` transaction. This ID is the on-chain link tying the commit and the claim together across chains.
- `sfDestination` — the `AccountID` that will receive the funds on the destination chain.
- `sfAmount` — the `STAmount` to be released. Cross-chain bridges can carry XRP or IOU assets; `STAmount` is polymorphic enough to handle both.
- `sfDestinationTag` (optional) — a `uint32` routing tag. The getter returns `protocol_autogen::Optional<uint32_t>` (an alias for `std::optional<uint32_t>`), and a companion `hasDestinationTag()` guard is provided following the convention used across `TransactionBase` for all optional fields.

The builder's constructor requires all four mandatory fields upfront, preventing callers from accidentally omitting one and then calling `build()`. The optional `sequence` and `fee` parameters default to `std::nullopt` so that callers who want the network to fill those fields (e.g., during testing) don't have to supply them.

## Type Safety and Defensive Checks

Both the `XChainClaim` constructor and the `XChainClaimBuilder(std::shared_ptr<STTx const>)` overload verify `getTxnType() == ttXCHAIN_CLAIM` and throw `std::runtime_error` otherwise. This guards against the common mistake of wrapping a deserialized transaction of the wrong type — for example, mistakenly passing an `XChainCommit` STTx to an `XChainClaim` wrapper. The check is redundant in the happy path (where the builder's `build()` method produces the object), but essential for the secondary constructor used when loading existing transactions from storage or the network.

Builder setter parameters use `std::decay_t<typename SF_...::type::value_type> const&`. The `std::decay_t` strips reference qualifiers from the SField type alias before forming the parameter type. Without it, a double-reference (`T& const&`) would collapse in ways that could silently accept rvalues where they shouldn't be bound, or vice versa. Using `decay_t` gives explicit, predictable value semantics throughout.

## Build and Sign Flow

The builder's `build(PublicKey const& publicKey, SecretKey const& secretKey)` method calls the `sign()` helper inherited (as `protected`) from `TransactionBuilderBase`. That helper serializes the in-progress `STObject object_` with the `HashPrefix::txSign` prefix (without signing fields), computes an Ed25519 or secp256k1 signature, writes `sfSigningPubKey` and `sfTxnSignature` into the object, then constructs a final `STTx` from it. The completed `STTx` is immediately wrapped in an `XChainClaim` — transitioning from mutable builder state to the immutable read-only wrapper in a single expression.

The base class deliberately avoids calling `object_.set(soTemplate)` during construction. Doing so would insert `soeDEFAULT` placeholder fields into the `STObject`, which would cause `STTx`'s `applyTemplate()` to throw when it encounters an explicitly-set default value. By leaving the object template-free, the builder stays compatible with `STTx`'s own schema enforcement at construction time.

## Relationship to the XChain Bridge Protocol

`XChainClaim` is the redemption leg of a multi-step protocol. The `sfXChainClaimID` it carries is created by `XChainCreateClaimID`, funded by `XChainCommit` on the source chain, and attested to by witness servers using `XChainAddClaimAttestation`. Only after a quorum of attestations exist on-ledger does submitting an `XChainClaim` with the matching ID actually release funds to `sfDestination`. The transaction type number (43) sequentially follows `XChainCommit` (42) and `XChainCreateClaimID` (41), reflecting its position as the terminal step in the protocol. The `Delegation::delegable` attribute means a delegate account — set via `sfDelegate` from the common base — may submit this transaction on behalf of the originating account.