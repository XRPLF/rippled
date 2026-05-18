# `AMMWithdraw.cpp` — AMM Liquidity Withdrawal Transactor

## Role in the System

`AMMWithdraw.cpp` implements the XRPL AMM Withdraw transaction, one of the core DEX operations defined by [XLS-30d](https://github.com/XRPLF/XRPL-Standards/discussions/78). Its purpose is to redeem LP tokens held by a liquidity provider in exchange for a proportional share of the AMM pool's reserves. The file lives in `src/libxrpl/tx/transactors/dex/` alongside `AMMDeposit.cpp`, `AMMBid.cpp`, `AMMVote.cpp`, and `AMMDelete.cpp` — each a self-contained transactor. `AMMWithdraw` is the mirror of `AMMDeposit`; where the deposit mints LP tokens, the withdrawal burns them.

## Withdrawal Modes

The transaction specification defines seven distinct withdrawal sub-types, each controlled by an exclusive flag bit from `tfWithdrawSubTx`. `preflight()` enforces exclusivity with `std::popcount(flags & tfWithdrawSubTx) != 1`, guaranteeing exactly one mode is selected. Each mode has a strict combination of required and forbidden fields:

| Flag | Required fields | Forbidden fields | Fee charged? |
|---|---|---|---|
| `tfLPToken` | `sfLPTokenIn` | amount, amount2, ePrice | No |
| `tfWithdrawAll` | — (none) | all optional fields | No |
| `tfOneAssetWithdrawAll` | `sfAmount` (asset spec only) | lpTokens, amount2, ePrice | Yes |
| `tfSingleAsset` | `sfAmount` | lpTokens, amount2, ePrice | Yes |
| `tfTwoAsset` | `sfAmount`, `sfAmount2` | lpTokens, ePrice | No |
| `tfOneAssetLPToken` | `sfAmount`, `sfLPTokenIn` | amount2, ePrice | Yes |
| `tfLimitLPToken` | `sfAmount`, `sfEPrice` | lpTokens, amount2 | Yes |

The flag-to-field matrix in `preflight()` is enforced with explicit boolean checks — the asymmetric use of `!field` vs `field` mirrors the "must have" vs "must not have" semantics clearly and cheaply.

## Three-Phase Transaction Flow

`AMMWithdraw` follows the standard XRPL transactor lifecycle:

**`preflight()`** performs purely static validation against the transaction's own fields — flag consistency, asset pair validity via `invalidAMMAssetPair()`, same-asset check (`amount->asset() == amount2->asset()` returns `temBAD_AMM_TOKENS`), and per-field sanity via `invalidAMMAmount()`. It also runs `checkExtraFeatures()`, which gates the entire transaction on `ammEnabled(ctx.rules)` and blocks MPT-bearing fields unless `featureMPTokensV2` is active. No ledger reads occur here.

**`preclaim()`** reads ledger state to validate the request against live pool composition. It fetches the AMM SLE via `keylet::amm()`, calls `ammHolds()` to get current balances (with freeze and auth handling both set to `fhIGNORE_FREEZE`/`ahIGNORE_AUTH` because this is a read-only check), verifies the pool is non-empty (`lptAMMBalance != beast::zero`), confirms the LP actually holds tokens, checks against over-withdrawal, and validates authorization and freeze status per-asset. The `checkAmount` lambda centralizes the per-asset checks for `sfAmount` and `sfAmount2` without duplicating the freeze/auth/MPT logic.

**`doApply()`** wraps everything in a `Sandbox` — a copy-on-write overlay of the ledger view. All mutations happen inside `applyGuts()` against `sb`; if that returns a success result, `sb.apply(ctx_.rawView())` atomically commits the changes. If anything fails mid-withdrawal, the sandbox is simply discarded.

## `applyGuts()` — Dispatch and AMM Lifecycle

`applyGuts()` re-reads pool state from the sandbox (now with `fhZERO_IF_FROZEN`/`ahZERO_IF_UNAUTHORIZED` so frozen balances are treated as zero for calculation purposes), then dispatches to the appropriate calculation function based on `subTxType`. After calculation, it calls `deleteAMMAccountIfEmpty()` — if the new LP token balance is exactly zero, the entire AMM account is deleted from the ledger. This lifecycle management is critical: the AMM object must not persist with zero liquidity, as doing so would leave stale state and waste ledger object slots.

The `fixAMMv1_1` amendment guard near the top of `applyGuts()` calls `verifyAndAdjustLPTokenBalance()` to handle a known rounding drift between the last LP's trustline balance and the AMM object's recorded `LPTokenBalance`. Without this correction, the final withdrawal could fail because the two don't match exactly.

## Calculation Functions

### Equal withdrawal (`equalWithdrawTokens`)

The proportional mode — `tfLPToken` and `tfWithdrawAll` — computes asset amounts as `frac = tokensWithdrawn / totalLPTokens` and applies it to both pool balances: `amountWithdraw = amountBalance * frac`. The helper `adjustLPTokensIn()` applies `fixAMMv1_3`-gated rounding to the LP token count before computing the fraction. A critical guard fires if this rounding rounds either asset amount to zero: `return {tecAMM_FAILED, ...}`. This protects against the degenerate case where the withdrawal is too small to produce a non-zero asset amount due to fixed-point truncation, telling the user to increase the token amount rather than silently producing a one-sided withdrawal.

When `lpTokensWithdraw == lptAMMBalance` (the last LP is withdrawing everything), the function short-circuits to `withdraw(..., WithdrawAll::Yes, ...)`, bypassing the rounding adjustments and using the raw pool balances. This is the correct behavior: the last LP must receive exactly what remains.

### Dual-asset bounded withdrawal (`equalWithdrawLimit`)

`tfTwoAsset` accepts maximums for both assets and uses the constant-product relationship to determine the actual amounts. It calculates `t` (LP tokens) from `amount / amountBalance`, derives the implied `amount2`, and checks if that fits within the user's stated maximum. If not, it pivots to use `amount2` as the binding constraint. The algorithm is documented inline with the formulae from the spec (equations 5 and 6).

### Single-asset withdrawals (`singleWithdraw`, `singleWithdrawTokens`, `singleWithdrawEPrice`)

All three charge a trading fee. `singleWithdraw` solves equation 7 for LP tokens given a desired asset output: `t = T * (c - sqrt(c² - 4R)) / 2` via the `lpTokensIn()` helper from `AMMHelpers`. `singleWithdrawTokens` is the inverse — given LP tokens, it computes asset output via `ammAssetOut()`. `singleWithdrawEPrice` solves the two-constraint problem algebraically, deriving the unique LP token amount where the effective price equals `ePrice`, with the derivation shown step-by-step in the comments.

## Core `withdraw()` — Atomic State Mutation

The static `withdraw()` overload is the only place actual ledger state changes. Its logic:

1. **Rounding adjustment**: If `WithdrawAll::No`, calls `adjustAmountsByLPTokens()` to recompute actual withdrawal amounts from the rounded LP token count, ensuring the constant-product invariant holds after rounding.
2. **Invariant guards**: Checks that LP tokens don't exceed the caller's own balance, that the withdrawal doesn't drain exactly one side of the pool (which would violate `k = x * y`), and that consuming all LP tokens only happens when both pool balances are being fully drained.
3. **MPT pool state validity** (under `featureMPTokensV2`): after subtracting the withdrawn amounts, all three post-state values — balance1, balance2, LP tokens — must be simultaneously zero or simultaneously non-zero.
4. **Reserve check**: the `sufficientReserve` lambda (gated on `fixAMMv1_2`) verifies the receiving account has enough XRP to cover a new trustline or MPToken object before creating it, using `priorBalance` (the pre-fee XRP balance) to avoid penalizing accounts that are paying XRP fees.
5. **Transfers**: `accountSend()` moves asset1 (and optionally asset2) from the AMM account to the LP account with `WaiveTransferFee::Yes`, then `redeemIOU()` burns the LP tokens against the AMM's token issuance.

The private instance-method `withdraw()` overload is a thin adapter that fills in instance-specific parameters (`account_`, `preFeeBalance_`, freeze handling) and discards the per-asset return values, returning only the new LP token balance.

## Design Decisions Worth Noting

The `WithdrawAll` scoped enum (not just a `bool`) makes the "drain everything" intent explicit across function signatures, preventing accidental pass-by-position mistakes when the state variable propagates through several layers.

The `tokensWithdraw()` free function normalizes the "which tokens to redeem" question: for `tfWithdrawAll` and `tfOneAssetWithdrawAll`, the answer is always "all the LP tokens the account holds," regardless of what `sfLPTokenIn` says — because those flags don't even accept `sfLPTokenIn`. This normalization happens once in both `preclaim()` and `applyGuts()`, keeping the downstream functions free of flag-checking logic.

The amendment guards (`fixAMMv1_1`, `fixAMMv1_2`, `fixAMMv1_3`) are not merely feature flags — each corrects a specific class of bugs in the original AMM math. `v1_1` addresses LP token trustline drift, `v1_2` adds reserve checking for the receiving account, and `v1_3` tightens rounding handling so zero-token results are `tecAMM_INVALID_TOKENS` rather than silent failures or `tecAMM_FAILED`. The old `tecAMM_FAILED` branches remain guarded under `!enabled(fixAMMv1_3)` to preserve behavior on pre-amendment ledgers.