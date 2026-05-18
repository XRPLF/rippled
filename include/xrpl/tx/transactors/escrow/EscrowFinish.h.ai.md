# `EscrowFinish.h` — Transactor for Releasing Held Escrow Funds

`EscrowFinish` is one of three escrow transactors in the XRPL, alongside `EscrowCreate` and `EscrowCancel`. Where `EscrowCreate` locks funds into an on-ledger escrow object and `EscrowCancel` returns them to the originator when conditions are not met, `EscrowFinish` is the success path: it verifies that release conditions are satisfied and transfers the locked funds to the intended recipient.

## Role in the Transactor Hierarchy

Like all transactors, `EscrowFinish` inherits from `Transactor` and participates in the four-phase processing pipeline: `preflight` → `preflightSigValidated` → `preclaim` → `doApply`. The base class drives this pipeline through `invokePreflight<T>` and the `operator()()` call chain, using compile-time name hiding rather than virtual dispatch for the static methods. `EscrowFinish` fills in every hook of this pipeline, making it the most feature-rich of the three escrow transactors.

The `ConsequencesFactory` is set to `Normal`, meaning the transaction is handled with standard consequence logic. This contrasts with `EscrowCreate`, which sets `Custom` and provides a `makeTxConsequences` factory because locking up funds affects fee-processing semantics differently than releasing them.

## Amendment Gating via `checkExtraFeatures`

`checkExtraFeatures` is an optional override that `EscrowFinish` uses to gate one specific capability: including `sfCredentialIDs` in the transaction is only permitted when the `featureCredentials` amendment is active. The base `Transactor` implementation always returns `true`; returning `false` here causes `invokePreflight` to short-circuit with `temDISABLED` before any other validation runs. This pattern keeps amendment gating cleanly separated from field-level validation in `preflight`.

## Expensive Crypto-Condition Work in `preflightSigValidated`

The most distinctive feature of `EscrowFinish` compared to its siblings is `preflightSigValidated`. Escrow supports PREIMAGE-SHA-256 crypto-conditions (from the Interledger Crypto-Conditions specification): when an escrow is created with a condition, finishing it requires supplying a fulfillment whose SHA-256 hash matches that condition.

Validating a fulfillment is computationally expensive. Two design choices address this:

1. **Post-signature placement**: `preflightSigValidated` runs after `preflight2` has verified the transaction's cryptographic signature. This ensures that condition validation is never performed on unauthenticated input — an attacker cannot force the node to do expensive crypto work by submitting a badly-signed transaction.

2. **Hash router caching**: The result of condition validation is stored in the node's `HashRouter` using two private flag bits, `SF_CF_INVALID` and `SF_CF_VALID`. Once set against the transaction ID, subsequent phases of the pipeline (`doApply` in particular) read the cached verdict rather than re-running the validation. If the cache entry has expired by the time `doApply` runs (unlikely but possible), `doApply` repeats the check and re-caches it. Notably, a failed condition check does not cause `preflightSigValidated` to return an error — the result is cached for `doApply` to act on, keeping preflight non-blocking for broadcasting purposes.

`preflightSigValidated` also validates credential fields via `credentials::checkFields` when `sfCredentialIDs` is present.

## Fee Scaling for Fulfillments

`calculateBaseFee` overrides the base implementation to add a surcharge when a fulfillment is attached:

```
extraFee = base_fee * (32 + fulfillment_size / 16)
```

This is intentional economic design: larger fulfillments impose more validation cost on the network, and the fee schedule ensures that cost is borne by the submitter. Without this surcharge, attackers could submit transactions with large fulfillments at standard fees, creating a denial-of-service vector for validators.

## `preclaim`: Asset Eligibility Checks

`preclaim` handles two independent concerns guarded by separate amendments. When `featureCredentials` is enabled, it validates credential authorization via `credentials::valid`. When `featureTokenEscrow` is enabled (the amendment allowing non-XRP assets to be held in escrow), it reads the escrow ledger object and, for non-XRP amounts, dispatches through a `std::visit` to one of two template specializations:

- **`Issue` (IOU)**: Checks `requireAuth` authorization and verifies the destination is not deep-frozen by the issuer.
- **`MPTIssue` (MPToken)**: Checks that the issuance object exists, verifies `requireAuth` (using `WeakAuth` semantics), and checks for MPT-level freeze.

The base `Transactor::preclaim` returns `tesSUCCESS` and does nothing; `EscrowFinish` overrides it because the asset eligibility checks must happen before the transaction is committed to avoid charging fees for certain precondition failures.

## `doApply`: State Mutation

The application phase enforces the temporal and cryptographic release conditions, then performs the ledger mutations:

**Time window enforcement**: The ledger's `parentCloseTime` is compared against the optional `sfFinishAfter` (too early) and `sfCancelAfter` (too late) fields on the escrow object. Both checks returning `tecNO_PERMISSION` means the transaction is well-formed but timing prevents execution.

**Condition re-check**: The hash router cache is consulted. If `SF_CF_INVALID` is set, execution fails with `tecCRYPTOCONDITION_ERROR`. Three additional semantic checks follow: a condition attached to the transaction must match one stored in the escrow object; a transaction must not supply a condition if none was recorded at creation; if a condition was recorded, the exact same bytes must be presented.

**Deposit pre-auth**: `verifyDepositPreauth` ensures that if the destination account requires deposit authorization, the originating account is on the allow-list.

**Directory cleanup**: The escrow object is removed from the originating account's owner directory and, if present, from the recipient's owner directory. For non-XRP escrows, it is also removed from the issuer's owner directory. Failure to remove from any directory returns `tefBAD_LEDGER`, flagging internal ledger corruption.

**Fund transfer**: For XRP, the destination account's balance is incremented directly. For non-XRP assets, `escrowUnlockApplyHelper` handles transfer rate application, using `parityRate` when no locked transfer rate was recorded at escrow creation.

**Owner count**: The originating account's owner count is decremented by one, reflecting that the escrow object no longer occupies reservation space.

## Relationship to Sibling Transactors

`EscrowFinish.h` is structurally the most complex of the three escrow headers. `EscrowCreate.h` is unique in its `Custom` consequences factory and the `makeTxConsequences` method it provides; `EscrowCancel.h` is the simplest, with only the standard `preflight`/`preclaim`/`doApply` trio. `EscrowFinish` sits between them in complexity, distinguished by its custom fee calculation, the post-signature crypto-condition pipeline hook, and multi-asset `preclaim` logic — all driven by the fact that this is the only path that both validates external cryptographic proof and actually moves locked value to its destination.