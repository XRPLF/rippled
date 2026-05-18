# EscrowFinish.cpp

`EscrowFinish.cpp` implements the transaction handler that releases escrowed funds to their intended destination on the XRP Ledger. It is the counterpart to `EscrowCreate` — where `EscrowCreate` locks value into a ledger object with attached time and/or crypto-condition constraints, `EscrowFinish` enforces those constraints and, when satisfied, transfers the locked value to the destination account and removes the escrow object from the ledger.

## Transaction Lifecycle and Validation Phases

`EscrowFinish` inherits from `Transactor` and participates in the standard four-phase processing pipeline: `preflight` → `preflightSigValidated` → `preclaim` → `doApply`. The split across these phases is architecturally deliberate.

**`preflight`** performs only structural validation — if `sfCondition` is present without `sfFulfillment` or vice versa, the transaction is immediately rejected as `temMALFORMED`. Crypto-condition verification is intentionally deferred because it is computationally expensive and should only be performed after the transaction's signatures have been validated (which happens between `preflight` and `preflightSigValidated`).

**`preflightSigValidated`** is where the actual crypto-condition check runs, via the static helper `checkCondition(Slice f, Slice c)`. This function deserializes both the `sfFulfillment` and `sfCondition` fields using the `cryptoconditions` library and verifies that the fulfillment satisfies the condition. Importantly, the result is cached in the `HashRouter` under two private flag bits — `SF_CF_VALID` (mapped to `PRIVATE6`) and `SF_CF_INVALID` (mapped to `PRIVATE5`). This means that if the same transaction is processed multiple times by different code paths during consensus, the expensive cryptographic check is only performed once; subsequent passes find the flags already set and skip straight to the conclusion.

**`preclaim`** handles read-only ledger state checks that don't modify anything. For token escrows (gated on `featureTokenEscrow`), this phase fetches the escrow SLE and calls template-specialized helpers to verify that the destination is authorized to hold the asset and is not frozen or deep-frozen. The two specializations — `escrowFinishPreclaimHelper<Issue>` for IOU/trust-line assets and `escrowFinishPreclaimHelper<MPTIssue>` for Multi-Purpose Tokens — handle the different authorization models of each asset type. MPTIssue uses `AuthType::WeakAuth`, reflecting a less strict authorization requirement, while IOU issues use the standard `requireAuth` check and also test for deep-freezing.

## Fee Calculation as a DoS Defense

`calculateBaseFee` charges extra for fulfillments according to the formula `base * (32 + size/16)`. This is a deliberate anti-abuse measure: crypto-condition validation is O(fulfillment size), so larger fulfillments translate directly into more computational work per validator. By making the fee scale with fulfillment size, the protocol discourages arbitrarily large fulfillment payloads.

## The Apply Phase

`doApply` assembles all the checks and performs the actual ledger mutations. Its logic proceeds in a carefully ordered sequence:

1. **Time window enforcement** — The escrow's `sfFinishAfter` and `sfCancelAfter` times are checked against `parentCloseTime` of the current ledger header. If the finish time hasn't passed yet, or the cancel time has already passed, the transaction fails with `tecNO_PERMISSION`. Using `parentCloseTime` (rather than the current ledger's close time) is a standard XRPL pattern that ensures determinism across validators.

2. **Crypto-condition re-check** — The code reads the `SF_CF_VALID`/`SF_CF_INVALID` flags from the `HashRouter`. The comment acknowledges the theoretical edge case: if the router's aged cache evicts the flags before `doApply` runs, the condition is re-evaluated inline. This path is marked `LCOV_EXCL_START` as it is essentially untestable in practice. After that, the condition stored *in the escrow SLE* (`sfCondition` on the `slep` object) is compared against the condition provided *in the transaction*. These must match exactly — the condition in the transaction is not trusted on its own; it must be the identical condition that was locked in at escrow creation time.

3. **Deposit authorization** — `verifyDepositPreauth` checks whether the destination account requires deposit preauthorization and whether the transaction submitter (`account_`) is preauthorized. This prevents escrow payouts from bypassing deposit preauth settings that the destination may have set after the escrow was created.

4. **Directory cleanup** — The escrow SLE is removed from the owner's directory, from the recipient's directory (if `sfDestinationNode` is present on the SLE — it may not be for older escrows created before the `DestinationTag` feature), and for token escrows, also from the issuer's directory. Failing to remove from any directory is a fatal ledger corruption (`tefBAD_LEDGER`).

5. **Asset transfer** — For XRP, this is a direct balance increment on the destination account SLE. For non-XRP amounts, the call is dispatched via `std::visit` to `escrowUnlockApplyHelper<T>` (defined in `EscrowHelpers.h`), which handles trust-line creation if needed, transfer rate calculation, and the actual credit.

## Transfer Rate Semantics for Token Escrows

Token escrows introduce a nuanced transfer-rate mechanic. When an escrow is created, the current transfer rate of the IOU issuer is recorded as `sfTransferRate` in the escrow SLE. At finish time, the *lower* of the locked rate and the current rate is applied. This asymmetry — always taking the more favorable rate from the recipient's perspective — protects against issuers raising their transfer rate after an escrow is committed. The deduction is also taken from the escrowed amount rather than added on top, which differs from normal payment semantics and is explicitly noted in `EscrowHelpers.h`.

The `createAsset` boolean in `escrowUnlockApplyHelper` signals whether the account submitting the finish transaction is itself the destination. When `destID == account_`, the helper is permitted to auto-create trust lines or MPToken entries on behalf of that account, funded by the recipient's XRP reserve. When a third party finishes the escrow (e.g., a bot or the original sender claiming back after expiry), auto-creation is blocked to prevent the finisher from modifying another account's trust line state.

## Feature Gating

The file makes clean use of `featureTokenEscrow` and `featureCredentials` guards throughout. XRP escrows work unconditionally; token escrow paths are entirely off unless `featureTokenEscrow` is enabled. Similarly, `sfCredentialIDs` support (validated via `credentials::checkFields` and `credentials::valid`) is guarded by `featureCredentials`. `checkExtraFeatures` enforces this gate at the preflight level — returning false causes the transaction to be rejected before any deeper processing if the required amendment is not yet active.