# `AMMDeposit.h` — AMM Liquidity Deposit Transactor

## Role in the System

`AMMDeposit` is the `Transactor` subclass responsible for processing `AMMDeposit` transactions on the XRP Ledger, as defined in [XLS-30d](https://github.com/XRPLF/XRPL-Standards/discussions/78). It implements the mechanism by which liquidity providers add assets to an AMM pool, receiving LP tokens that represent their fractional share of the pool's reserves. The file lives alongside its counterpart `AMMWithdraw.h` and the supporting `AMMCreate.h`, `AMMBid.h`, `AMMVote.h`, and `AMMDelete.h` transactors in the `dex/` directory.

## Deposit Modes and Flag Dispatch

The most architecturally significant aspect of `AMMDeposit` is that it exposes six distinct deposit modes, each selected by a single mutually-exclusive transaction flag (`tfDepositSubTx`). The `preflight` enforces this exclusivity using `std::popcount` — exactly one sub-mode bit must be set or the transaction is `temMALFORMED`. The six modes correspond to six private methods, selected in `applyGuts` by testing `subTxType & tfXxx`:

| Flag | Method | Fee charged? | Description |
|---|---|---|---|
| `tfLPToken` | `equalDepositTokens` | No | Deposit proportional assets for a target LP token amount |
| `tfTwoAsset` | `equalDepositLimit` | No | Proportional deposit with per-asset maximum constraints |
| `tfSingleAsset` | `singleDeposit` | Yes | Single-asset deposit by amount |
| `tfOneAssetLPToken` | `singleDepositTokens` | Yes | Single-asset deposit targeting an LP token quantity |
| `tfLimitLPToken` | `singleDepositEPrice` | Yes | Single-asset deposit with an effective-price ceiling |
| `tfTwoAssetIfEmpty` | `equalDepositInEmptyState` | N/A | Pool bootstrapping for a zero-balance AMM |

The fee asymmetry is economically motivated: proportional deposits preserve the pool's price ratio and create no arb opportunity, so no trading fee is warranted. Single-asset deposits are mathematically equivalent to a swap followed by a proportional deposit, so the pool's trading fee is applied to the swap component.

## Three-Phase Validation Pipeline

`AMMDeposit` follows the standard XRPL `Transactor` lifecycle:

**`checkExtraFeatures`** guards the `featureAMM` amendment (via `ammEnabled`) and additionally enforces that MPT-backed (Multi-Purpose Token) pools are only permitted when `featureMPTokensV2` is active. This separates amendment gating from field validation.

**`preflight`** performs purely structural checks against the transaction fields without touching ledger state: valid flag combination, no conflicting optional fields for each mode, asset-pair validity, non-zero LP token amounts, and trading fee within `TRADING_FEE_THRESHOLD`. Because `preflight` has no ledger view, it intentionally cannot check balances.

**`preclaim`** performs ledger-state checks after signature verification. Critically, it distinguishes between two pool states: the `tfTwoAssetIfEmpty` flag requires `lptAMMBalance == 0` (returns `tecAMM_NOT_EMPTY` if populated), while all other modes require `lptAMMBalance > 0` (returns `tecAMM_EMPTY` if the pool is drained). It also validates account sufficiency, freeze state, and authorization. A comment in the source acknowledges that the balance check in `preclaim` is optimistic for modes where the actual deposit amount is derived from pool math — those modes re-validate inside `deposit()`.

When `featureAMMClawback` is enabled, `preclaim` also checks whether either pool asset is individually frozen on the depositor's account, rejecting with `tecFROZEN`.

## `applyGuts` and the Sandbox Pattern

`doApply` creates a `Sandbox` (a copy-on-write ledger view) and delegates to `applyGuts`. All state mutations — balance transfers, trustline updates, LP token issuance — are staged against the sandbox. Only on `tesSUCCESS` does `applyGuts` call `sb.apply(ctx_.rawView())` to commit atomically. This is the standard XRPL pattern for ensuring ledger consistency: if any sub-operation fails, the sandbox is discarded without affecting consensus state.

After a successful deposit, `applyGuts` updates the AMM ledger entry's `sfLPTokenBalance` field. In the empty-pool case (`lptAMMBalance == beast::zero`), it also calls `initializeFeeAuctionVote`, which initializes the auction slot and voting structure — granting the bootstrapping LP their initial fee-governance position.

The trading fee used during `applyGuts` is determined by pool state: for non-empty pools, `getTradingFee` is called with the caller's `account_`, which accounts for any discounted fee won through the auction mechanism (`AMMBid`). For the empty-pool initialization path, the fee is taken directly from the optional `sfTradingFee` field in the transaction itself, defaulting to zero.

## Structural Comparison with `AMMWithdraw`

`AMMWithdraw` is the structural mirror of `AMMDeposit`, with a one-to-one mapping of deposit modes to withdrawal modes. One notable difference is that `AMMWithdraw` exposes its core `withdraw` and `equalWithdrawTokens` methods as `public static` functions. This allows `AMMDelete` to reuse the withdrawal logic when draining a pool for deletion. `AMMDeposit` has no such need and keeps all its mode-specific methods `private`, enforcing that the deposit path is only entered through the validated transactor lifecycle.

The `deposit` private method acts as a shared execution kernel: after each mode-specific method calculates the precise asset amounts, it calls `deposit` to execute the actual token transfers, including creating LP token trustlines for new liquidity providers and XRP balance adjustments for XRP-denominated pool assets.