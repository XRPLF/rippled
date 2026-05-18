# `PaymentChannelClaim.h` — Payment Channel Claim Transaction Handler

## Role in the System

`PaymentChannelClaim` is the transactor that processes the `PaymentChannelClaim` transaction type on the XRP Ledger. Payment channels are off-ledger scaling constructs: a sender locks XRP into an on-ledger channel object, then issues signed authorization vouchers off-ledger to a receiver. When the receiver (or the sender) wants to settle, they submit a `PaymentChannelClaim` transaction to move those vouchers on-chain. This transactor handles all three phases of that settlement: pre-validation, ledger-read checks, and final ledger mutation.

The file declares the class within the `xrpl` namespace and brings in only `Transactor.h`, keeping the header minimal and compilation fast.

## Class Design and Inheritance

`PaymentChannelClaim` inherits from `Transactor` and follows the framework's compile-time polymorphism pattern. Rather than virtual dispatch for the preflight and preclaim phases, the base class `invokePreflight<T>` template calls `T::checkExtraFeatures`, `T::getFlagsMask`, `T::preflight`, and then `T::preflightSigValidated` in sequence using name hiding. Only `doApply()` is a true virtual override and is the sole instance method on the class.

The `ConsequencesFactory` is set to `Normal`, in contrast to `PaymentChannelCreate` and `PaymentChannelFund`, which both declare `Custom`. This distinction matters for fee/consequence analysis: Create and Fund introduce new XRP obligations (locking funds into the channel), requiring custom `TxConsequences` objects to describe their impact on account balances. A Claim only redistributes XRP that is already locked — it cannot add new obligations — so the standard `Normal` factory suffices.

## Preflight Phase

`checkExtraFeatures` gates credential support: if the transaction carries `sfCredentialIDs`, the `featureCredentials` amendment must be active, or the transaction is rejected with `temDISABLED`. This is the canonical pattern for amendment-gated optional fields.

`getFlagsMask` returns `tfPaymentChannelClaimMask`, restricting which flags are legal on this transaction type. The base `preflight1` enforces this mask before calling the transactor-specific `preflight`.

`preflight` itself enforces the consistency rules that can be checked without ledger state:

- `sfBalance` and `sfAmount`, if present, must be positive XRP values. If both appear, `sfBalance` must not exceed `sfAmount` — the claimed balance cannot exceed the channel's total funding.
- `tfClose` and `tfRenew` are mutually exclusive flags; combining them is `temMALFORMED`.
- If `sfSignature` is present, both `sfPublicKey` and `sfBalance` must accompany it (you can't provide a partial authorization). The signature is cryptographically verified here in preflight against a canonical channel authorization message serialized via `serializePayChanAuthorization`. This is notable: signature verification normally happens in the framework's `preflight2`, but the payment channel authorization signature is a *separate* off-channel payment voucher distinct from the transaction signature itself, so it must be checked explicitly here.
- Credential field well-formedness is validated via `credentials::checkFields`.

## Preclaim Phase

`preclaim` overrides the base class no-op to perform credential validation against live ledger state when the `featureCredentials` amendment is enabled. This two-phase approach — structural check in `preflight`, live validity check in `preclaim` — is necessary because `preflight` runs without ledger access.

## Apply Phase

`doApply` is the heart of the transactor and implements four distinct behaviors selected by the transaction's content and flags.

**Expiry check first.** Before any other logic, the channel's `sfCancelAfter` and `sfExpiration` fields are compared against the ledger's `parentCloseTime`. If the channel is expired by either deadline, it is closed immediately via `closeChannel`, regardless of whether a balance claim was requested. This ensures expired channels cannot be exploited.

**Authorization check.** Only the channel source (`sfAccount`) or the channel destination (`sfDestination`) may submit a claim transaction. Any other account receives `tecNO_PERMISSION`.

**Balance claim.** If `sfBalance` is present, the transactor transfers the incremental XRP. The destination must supply a signature (checked here against the channel's stored public key, not the transaction's signing key). The logic enforces monotonicity: `reqBalance` must exceed the channel's current `sfBalance` — otherwise there's nothing new to transfer and the transaction fails with `tecUNFUNDED_PAYMENT`. The difference (`reqDelta`) is credited to the destination's account balance. A `verifyDepositPreauth` call also enforces any DepositPreauth constraints on the destination account.

**Renew (`tfRenew`).** Clears the channel's `sfExpiration` field, resetting any voluntary expiration the source had previously set. Only the source account may renew; a destination attempting this is rejected with `tecNO_PERMISSION`.

**Close (`tfClose`).** If the destination is closing, or the channel is fully drawn (`Balance == Amount`), the channel closes immediately. Otherwise — when the source is closing a partially-funded channel — the transactor sets `sfExpiration` to `parentCloseTime + sfSettleDelay`, giving the destination a settlement window. Crucially, it only updates the expiration if no sooner expiration already exists, preventing a source from repeatedly extending its own close request.

## Relationship to Sibling Transactors

The three payment channel transactors form a lifecycle:

- `PaymentChannelCreate` opens a channel and locks XRP into it (Custom consequences).
- `PaymentChannelFund` adds more XRP to an existing channel (Custom consequences).
- `PaymentChannelClaim` settles or closes the channel (Normal consequences).

All three share the same header-only structure and `Transactor` inheritance pattern, but only `PaymentChannelClaim` overrides `checkExtraFeatures`, `getFlagsMask`, and `preclaim` — the extra complexity reflecting the richness of the claim operation compared to simple fund-and-create operations.