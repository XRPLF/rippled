# AMMDeposit.cpp

`AMMDeposit` implements the `AMMDeposit` transaction for the XRP Ledger's on-chain Automated Market Maker (AMM), specified in [XLS-30d](https://github.com/XRPLF/XRPL-Standards/discussions/78). Its purpose is to allow a liquidity provider (LP) to deposit one or both assets into an existing AMM pool and receive LP tokens representing their proportional ownership of the pool. The file lives alongside the other DEX transactors (`AMMWithdraw`, `AMMBid`, `AMMVote`, etc.) under `src/libxrpl/tx/transactors/dex/`.

## Deposit Modes and Flag Dispatch

The transaction carries exactly one sub-transaction flag drawn from `tfDepositSubTx`. The `preflight` function enforces this with `std::popcount(flags & tfDepositSubTx) != 1`, returning `temMALFORMED` for anything other than exactly one bit set. Each flag defines a distinct deposit strategy with a strictly prescribed set of required and forbidden fields:

| Flag | Required | Forbidden |
|---|---|---|
| `tfLPToken` | `sfLPTokenOut`; `sfAmount`/`sfAmount2` must appear together or not at all | `sfEPrice`, `sfTradingFee` |
| `tfSingleAsset` | `sfAmount` | `sfAmount2`, `sfEPrice`, `sfTradingFee` |
| `tfTwoAsset` | `sfAmount`, `sfAmount2` | `sfEPrice`, `sfTradingFee` |
| `tfOneAssetLPToken` | `sfAmount`, `sfLPTokenOut` | `sfAmount2`, `sfEPrice`, `sfTradingFee` |
| `tfLimitLPToken` | `sfAmount`, `sfEPrice` | `sfLPTokenOut`, `sfAmount2`, `sfTradingFee` |
| `tfTwoAssetIfEmpty` | `sfAmount`, `sfAmount2` | `sfEPrice`, `sfLPTokenOut` |

The `tfTwoAssetIfEmpty` flag is the only mode that allows `sfTradingFee` (optional) and is the only path that seeds a brand-new, empty pool. Every other mode requires a live pool (`lptAMMBalance > 0`), and `preclaim` enforces the asymmetry: `tfTwoAssetIfEmpty` demands `lptAMMBalance == 0`, while all others fail with `tecAMM_EMPTY` if the pool holds no LP tokens.

## Validation Pipeline

`checkExtraFeatures` is the earliest gate. It checks that the AMM amendment is enabled via `ammEnabled(ctx.rules)` and that if `featureMPTokensV2` is not live, neither the asset pair descriptors (`sfAsset`, `sfAsset2`) nor the deposit amounts hold an `MPTIssue`. This prevents MPT-denominated pool participation until the feature is activated, with no impact on IOU/XRP pools.

`preflight` is fully stateless and performs three additional checks after the flag/field consistency tests: `invalidAMMAssetPair` ensures the two pool assets differ and form a valid pair; amounts whose `asset()` are identical are rejected with `temBAD_AMM_TOKENS`; and `lpTokens`, if present, must be strictly positive. The `sfEPrice` field receives a special treatment under `featureMPTokensV2` — the asset pair constraint on effective-price is relaxed (set to `std::nullopt`) to allow MPT-valued effective prices.

`preclaim` is where ledger state is first consulted. It reads the AMM `SLE` and calls `ammHolds` to retrieve current pool balances `(amountBalance, amount2Balance, lptAMMBalance)`. The balance checks in this phase are advisory: for `tfLPToken` mode (where the actual deposit amounts are derived from the token quantity, not stated up front), only authorization and freeze checks run against the current pool balances, not against specific deposit amounts. For every other mode, a concrete balance check fires. The comment explicitly notes that these checks must be repeated inside `deposit()` because amounts may shift during calculation.

The `featureAMMClawback` amendment adds an additional layer in `preclaim`: a `WeakAuth` `requireAuth` check on both pool assets and a full freeze check (`isFrozen`) against the LP account. The weak variant is used because the LP account may not hold an MPT object yet; the actual MPT object existence is deferred to the send operation.

## Application: Sandbox and applyGuts

`doApply` follows the canonical transactor pattern for state-mutating operations. It creates a `Sandbox` — a copy-on-write view layered over the committed ledger — and delegates to `applyGuts`. Only if `applyGuts` signals success does the sandbox commit its changes with `sb.apply(ctx_.rawView())`. This ensures partial-failure atomicity: a deposit that fails balance checks after amount calculation leaves the ledger unchanged.

Inside `applyGuts`, the trading fee is resolved differently depending on pool state. For an empty pool (`lptAMMBalance == beast::zero`), the fee comes from `sfTradingFee` in the transaction itself; otherwise it is read from the AMM object via `getTradingFee`, which accounts for any vote-adjusted fee. After a successful deposit, the AMM `SLE`'s `sfLPTokenBalance` is updated in place. When seeding an empty pool, `initializeFeeAuctionVote` also initializes the auction slot and voting records.

## Deposit Strategies

Each mode delegates to a private calculation method, then calls the shared `deposit()` primitive.

**`equalDepositTokens`** (`tfLPToken`): The LP specifies how many LP tokens they want. Both asset deposits are derived as `amount = balance × (tokensAdj / lptAMMBalance)`. The `sfAmount`/`sfAmount2` fields, if present, act as minimum thresholds rather than deposit amounts. No trading fee is charged because the deposit is perfectly proportional.

**`equalDepositLimit`** (`tfTwoAsset`): The LP specifies maximums for both assets. The algorithm computes the required asset-2 deposit from the asset-1 limit; if that fits within the asset-2 maximum the transaction proceeds. If not, it inverts the calculation — computing required asset-1 from the asset-2 limit and checking against the asset-1 maximum. If neither direction satisfies both constraints simultaneously, the transaction fails with `tecAMM_FAILED`. No trading fee applies here either.

**`singleDeposit`** (`tfSingleAsset`): Deposits one asset only; the pool absorbs the imbalance internally. LP tokens are calculated using `lpTokensOut`, which applies equation (3) from the XLS-30 spec. The trading fee is charged because single-asset deposit is mathematically equivalent to a swap followed by a proportional deposit.

**`singleDepositTokens`** (`tfOneAssetLPToken`): The LP specifies how many LP tokens they want, backed by a single asset. `ammAssetIn` inverts the single-deposit formula (equation 4 from XLS-30) to derive the required asset deposit. If the computed deposit exceeds the LP's stated maximum (`sfAmount`), the transaction fails.

**`singleDepositEPrice`** (`tfLimitLPToken`): The most mathematically complex path. An effective price cap `ePrice = assetIn / lpTokensOut` is enforced. If the natural trade at the stated `sfAmount` already satisfies the effective-price bound, that path is taken. Otherwise, a quadratic equation derived from the AMM invariant and effective-price definition is solved via `solveQuadraticEq` to find the exact asset input that hits the price limit exactly, and a second call to `getRoundedLPTokens` computes the resulting token output.

**`equalDepositInEmptyState`** (`tfTwoAssetIfEmpty`): Seeds a new pool. LP tokens are minted as `sqrt(asset1 × asset2)` via `ammLPTokens`, with both asset amounts treated as both the deposit and the initial "balance" (since the pool is empty). The trading fee from `sfTradingFee` is stored on the AMM object.

## The `deposit()` Primitive and LP Token Rounding

All six strategies ultimately call the private `deposit()` method, which applies `adjustAmountsByLPTokens` to account for integer rounding of LP tokens. This adjustment is critical: because LP tokens are stored with finite precision, the actual issuable token count may be slightly less than computed, and the corresponding asset deposits must be scaled down proportionally. After adjustment, minimum threshold checks run against all three quantities (`depositMin`, `deposit2Min`, `lpTokensDepositMin`), failing with `tecAMM_FAILED` if any threshold is not met.

The `fixAMMv1_3` amendment sharpens the rounding path via the file-local `adjustLPTokensOut` helper. Without the fix, a zero-token result after adjustment returned `tecAMM_FAILED`; with it, the more precise `tecAMM_INVALID_TOKENS` is returned so callers can distinguish rounding collapse from a genuine fee/price failure.

Fund movement in `deposit()` is sequential: asset-1 is sent from the LP account to the AMM account first, then asset-2 if present, and finally LP tokens are issued from the AMM account to the LP. Transfer fees are waived (`WaiveTransferFee::Yes`) for both asset transfers because the AMM is a ledger-native construct, not a user-to-user payment. The XRP liquidity check accounts for the LP-token trustline reserve — a fresh depositor who does not yet hold an LP-token trustline needs an additional owner reserve, and `xrpLiquid` is called with `!sle` (the trustline flag) to factor that in.