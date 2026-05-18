# `XChainCreateClaimID.h` — Cross-Chain Claim ID Creation Transaction

This auto-generated header defines the `ttXCHAIN_CREATE_CLAIM_ID` transaction type (type code 41), gated behind the `featureXChainBridge` amendment. In the XRP Ledger's cross-chain bridge protocol, a "claim ID" is a reservation object that must exist on the destination chain before a cross-chain value transfer can complete. `XChainCreateClaimID` is the mandatory first step in that two-phase flow: the account that will *receive* funds on the destination chain submits this transaction to allocate the claim ID, after which the sending account on the source chain references it in an `XChainCommit` (type 42). The file lives in `include/xrpl/protocol_autogen/transactions/` and is part of the auto-generated layer that maps the canonical `transactions.macro` definitions into strongly typed C++ classes.

## Dual-Class Pattern: Wrapper and Builder

The file follows the pattern used uniformly across the `xrpl::transactions` namespace: one immutable wrapper class and one fluent builder class. The separation reflects a real lifecycle constraint — once a transaction is signed, it must not be modified, so the wrapper only exposes `const` getters over an opaque `STTx`. The builder accumulates field values in a mutable `STObject` before the one-way signing step that produces the final wrapper. Neither class is designed for the other role.

## `XChainCreateClaimID`: Type-Safe Read-Only Wrapper

`XChainCreateClaimID` inherits from `TransactionBase`, which holds a `std::shared_ptr<STTx const>` and provides all common transaction field accessors (`getAccount()`, `getFee()`, `getSequence()`, `getDelegate()`, etc.). The constructor performs a runtime type check against `ttXCHAIN_CREATE_CLAIM_ID` and throws `std::runtime_error` on mismatch — a defensive guard that prevents accidental wrapping of a wrong transaction type, which would otherwise silently read incorrect field values. The `static constexpr txType` member enables compile-time dispatch in generic code that templates over transaction types.

The three transaction-specific required fields correspond directly to the fields declared `soeREQUIRED` in `transactions.macro`:

- `getXChainBridge()` returns the `STXChainBridge` value identifying the bridge — encoding the locking-chain door account, the issuing-chain door account, and the asset being bridged.
- `getSignatureReward()` returns the `STAmount` that will be paid to bridge witness servers for their cross-chain attestations. This reward exists to provide economic incentive for witnesses to monitor the source chain and submit `XChainAddClaimAttestation` transactions.
- `getOtherChainSource()` returns the `AccountID` of the account on the *sending* chain that will lock or burn the funds. The destination chain uses this to correlate attestations to the correct claim ID when multiple transfers are in flight.

Because all three fields are `soeREQUIRED`, the getters use `STTx::at()` rather than optional-returning accessors — access is safe by construction for any valid transaction object that has passed schema validation in `TransactionBase::validate()`.

## `XChainCreateClaimIDBuilder`: Construction and Signing

`XChainCreateClaimIDBuilder` inherits from `TransactionBuilderBase<XChainCreateClaimIDBuilder>`, which uses CRTP (Curiously Recurring Template Pattern) so that the base class's common setters (`setFee()`, `setSequence()`, `setLastLedgerSequence()`, `setDelegate()`, etc.) return a `XChainCreateClaimIDBuilder&` rather than a `TransactionBuilderBase&`. This enables method chaining without virtual dispatch — a zero-cost abstraction important for transaction-heavy hot paths.

The primary constructor requires all three domain-specific fields at construction time alongside the initiating `account`, with `sequence` and `fee` as optional parameters. This enforces that no builder instance can be in an under-specified state: the three bridge fields are set by delegating to their respective setters immediately in the constructor body. Optional fields can be added afterward via the inherited base setters.

A second constructor accepts a `std::shared_ptr<STTx const>` and performs the same type guard check, allowing round-tripping — loading a signed transaction back into a builder to re-sign or modify. Internally this assigns `object_ = *tx`, copying the `STObject` content.

The `build()` method calls `sign()` from `TransactionBuilderBase`, which serializes the object (excluding signing fields), prepends `HashPrefix::txSign`, computes the signature with the provided `SecretKey`, and sets both `sfSigningPubKey` and `sfTxnSignature`. It then moves `object_` into a freshly constructed `STTx` — move semantics mean the builder's internal state is left in a valid but unspecified state after `build()` returns, so callers should treat the builder as consumed. The resulting `XChainCreateClaimID` wrapper owns the signed transaction through `std::shared_ptr`.

## Relationship to the Cross-Chain Bridge Protocol

Within the bridge workflow, `XChainCreateClaimID` is the prerequisite for `XChainCommit` — the commit transaction references `sfXChainClaimID` (the numeric ID allocated by the ledger when this transaction executes) to associate the locked funds with the correct destination. The `delegable` attribute recorded in the comments (and enforced by the transaction processor layer) means this transaction supports the XRPL delegation mechanism, allowing a separate account to submit it on behalf of the originating account. The `featureXChainBridge` amendment guard ensures that ledgers which have not activated the amendment reject this transaction type entirely, keeping the protocol addition backward-compatible.