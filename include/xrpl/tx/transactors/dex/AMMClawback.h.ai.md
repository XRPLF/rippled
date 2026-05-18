# `AMMClawback.h` — Regulatory Asset Recovery from AMM Pools

## Role in the System

`AMMClawback` implements the transactor for the `AMMClawback` transaction type defined in XLS-73, which allows a token issuer to reclaim assets from a specific holder's position in an Automated Market Maker (AMM) pool. It exists because issuers of regulated tokens (those with `lsfAllowTrustLineClawback` set, or MPT issuances with `lsfMPTCanClawback`) must retain the ability to exercise clawback even when a holder's assets are locked inside an AMM liquidity position. Without this, a holder could circumvent issuer clawback authority simply by depositing regulated tokens into a pool.

The class inherits from `Transactor` and fits the standard three-phase transaction pipeline: static `preflight` validation, ledger-aware `preclaim` checks, and stateful `doApply` execution.

## Design: Clawback as Forced Withdrawal

The core design insight is that clawback from an AMM is implemented entirely as a forced *withdrawal* — the issuer compels the holder's LP tokens to be redeemed and the proceeds sent to the issuer rather than the holder. This reuses `AMMWithdraw::equalWithdrawTokens` and `AMMWithdraw::withdraw` as static helpers rather than duplicating AMM math, making the correctness argument depend on the well-tested withdrawal machinery.

Two paths through `applyGuts` handle the two modes:

1. **Full clawback** (no `sfAmount` field in the transaction): All of the holder's LP tokens are redeemed via `AMMWithdraw::equalWithdrawTokens` with `WithdrawAll::Yes`. This is a proportional two-asset withdrawal that burns every LP token the holder owns.

2. **Partial clawback** (with `sfAmount` specifying a maximum asset1 quantity): The private `equalWithdrawMatchingOneAmount` method calculates what fraction of the pool corresponds to that asset1 amount, derives the matching asset2 and LP token quantities, and calls `AMMWithdraw::withdraw`. Critically, if the fraction exceeds what the holder actually holds in LP tokens, the code falls back to a full clawback of whatever the holder has — the issuer cannot over-claw, but gets at most everything available.

In both modes the trading fee is explicitly passed as `0` (`tfee=0`). This is intentional: the withdrawal is a proportional equal-ratio removal of both assets, so no price-impact fee applies. Charging a trading fee would effectively penalize the issuer for exercising a regulatory right.

## `equalWithdrawMatchingOneAmount` — Proportional Arithmetic

This private method encapsulates the non-trivial case of computing a proportional withdrawal constrained by one asset amount. It computes:

```
frac       = amount / amountBalance        // fraction of pool to withdraw
amount2    = amount2Balance * frac         // paired asset amount
lpTokens   = lptAMMBalance * frac         // LP tokens to burn
```

If `lpTokens > holdLPtokens` (the holder doesn't have enough LP tokens to satisfy the requested asset1 amount), the method degrades gracefully to a full clawback via `equalWithdrawTokens`. This prevents the transaction from failing on a precondition that would be hard to enforce atomically.

When the `fixAMMClawbackRounding` amendment is active, the method invokes `getRoundedLPTokens` and `getRoundedAsset` helpers to snap values to representable amounts before calling `withdraw`, preventing dust accumulation or rounding-driven invariant violations.

## Freeze and Auth Bypass

Both code paths pass `FreezeHandling::fhIGNORE_FREEZE` and `AuthHandling::ahIGNORE_AUTH` to `AMMWithdraw`'s helpers. This is deliberate: an issuer exercising clawback must not be blocked by a trustline freeze or authorization state that they themselves may have set. Clawback is a higher-authority operation that supersedes normal trustline restrictions.

## `preflight` — Static Validation

Key invariants enforced before touching ledger state:
- `sfAsset` cannot be XRP (only issued assets can be clawed back).
- The transaction's `sfAccount` (issuer) must match `sfAsset`'s issuer field — issuers can only claw their own assets, not those of others.
- If `tfClawTwoAssets` is set, both assets must share the same issuer. A single issuer cannot claw an asset they don't control.
- If an `sfAmount` is provided, its asset subfield must match `sfAsset`, and the quantity must be positive.
- The holder (`sfHolder`) cannot be the same account as the issuer.

## `preclaim` — Ledger-State Validation

Permission gating occurs here, where ledger state is readable. For IOU-based assets, the issuer account must have `lsfAllowTrustLineClawback` set and must not have `lsfNoFreeze` set (an account that permanently waived freeze rights cannot reclaim clawback ability). For MPT-based assets, the specific MPT issuance must carry `lsfMPTCanClawback`. This check is encapsulated in the `checkClawAsset` lambda that dispatches over `Issue` vs `MPTIssue` variants.

The `featureAMMClawback` amendment gate lives in `checkExtraFeatures`, which also restricts MPT assets in the transaction to require `featureMPTokensV2`. This guards new token type support behind its own amendment rollout.

## Asset Transfer Mechanics

After the withdrawal succeeds, `applyGuts` calls `directSendNoFee` to move the clawed-back asset1 amount from the holder's account to the issuer. For the second asset, transfer only occurs if `tfClawTwoAssets` is set in the transaction flags — otherwise asset2 stays with the holder. This means by default a single-issuer AMM pair results in only the issuer's own token being recovered, which is the conservative regulatory minimum.

The `doApply` method wraps `applyGuts` in a `Sandbox`: all ledger mutations accumulate in the sandbox, and are flushed to the real `ApplyView` only if `applyGuts` returns success. Failed transactions leave no ledger trace beyond fee deduction, consistent with how all XRPL transactors handle partial-failure isolation.