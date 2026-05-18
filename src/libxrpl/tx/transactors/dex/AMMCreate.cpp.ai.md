# `AMMCreate.cpp` — AMM Pool Bootstrapping Transactor

## Role in the System

This file implements the `AMMCreate` transactor, which handles the `ttAMM_CREATE` transaction type on the XRP Ledger. Its purpose is to bootstrap an Automated Market Maker (AMM) liquidity pool from scratch: creating the ledger objects that represent the pool, minting LP tokens for the pool's first liquidity provider, and registering the new token pair with the payment engine's order book. Every other AMM transaction (`AMMDeposit`, `AMMWithdraw`, `AMMBid`, `AMMVote`, `AMMDelete`) depends on the three-object structure that `AMMCreate` establishes.

## The Transactor Lifecycle

`AMMCreate` inherits from `Transactor` and participates in the standard four-phase processing model: `checkExtraFeatures` → `preflight` → `preclaim` → `doApply`. The fee is computed separately via `calculateBaseFee`, which charges one owner reserve — the cost of the new `ltAMM` ledger entry.

## Validation Layers

### `checkExtraFeatures` (feature gating)

The first gate refuses the transaction entirely if the `ammEnabled` rules check fails, preventing AMM transactions on networks or ledger versions that predate XLS-30. It also blocks Multi-Purpose Token (MPT) assets unless `featureMPTokensV2` is active, enforcing a clean feature boundary between the two amendments.

### `preflight` (stateless checks)

`preflight` operates without ledger access and enforces three invariants: the two assets must be distinct (same asset on both sides makes no economic sense), each amount must pass `invalidAMMAmount` (which validates format, positivity, and type constraints), and the trading fee must not exceed `TRADING_FEE_THRESHOLD` (1000 basis points = 1%).

### `preclaim` (stateful checks)

`preclaim` is the heaviest validation phase. Its checks, in execution order:

**Uniqueness**: `keylet::amm(amount.asset(), amount2.asset())` computes the deterministic AMM object key and checks for prior existence. The key is an order-independent hash of both assets' currency/issuer fields — the same key is reached regardless of how the creator ordered the assets in the transaction.

**Authorization and freeze**: Both assets must be reachable by the creator account (`requireAuth`) and must not be frozen at the global or per-account level (`isFrozen`).

**DefaultRipple**: For IOU (non-XRP, non-MPT) assets, the `noDefaultRipple` lambda verifies the issuer account has the `lsfDefaultRipple` flag set. Without it, the AMM's trust lines cannot participate in rippling, which would make the pool unreachable in payment paths. XRP and MPT assets skip this check.

**Reserve and balance**: `xrpLiquid` calculates the creator's spendable XRP after accounting for one extra reserve (for the LP token trust line the creator will receive). The same XRP liquid balance is also compared against the deposit amount if one of the assets is XRP. For IOU/MPT assets, `accountFunds` is used with frozen-zero and unauthorized-zero semantics to prevent AMM creation from bypassing freezes.

**Anti-nesting**: The `isLPToken` lambda prevents using LP tokens from another AMM pool as an asset in the new pool. It detects LP token issuers by checking if the issuer's `AccountRoot` carries the `sfAMMID` field (a marker added to all AMM pseudo-accounts).

**Address collision**: When `featureSingleAssetVault` is enabled, `pseudoAccountAddress` checks that the would-be pseudo-account address doesn't already exist in the ledger before committing to it.

**MPT allowance**: `checkMPTTxAllowed` validates that the MPT issuance permits `ttAMM_CREATE` operations for the account.

**Clawback guard**: When `featureAMMClawback` is disabled, AMM creation is rejected if either asset's issuer has clawback enabled (`lsfAllowTrustLineClawback` for IOUs, `lsfMPTCanClawback` for MPTs). An issuer with clawback could drain the pool unilaterally, so this blocks the creation until the `featureAMMClawback` amendment — which handles clawback in a controlled manner — is live.

## Ledger Mutation: `applyCreate`

The static `applyCreate` function, called from `doApply` inside a `Sandbox`, performs all actual ledger changes. The sandbox pattern ensures the entire operation is atomic: if any step returns an error, the sandbox is simply discarded rather than partially committed.

**Pseudo-account creation**: `createPseudoAccount(sb, ammKeylet.key, sfAMMID)` derives an `AccountRoot` from the AMM keylet's hash. The account has no master key and is flagged with `sfAMMID`, marking it as a non-user pseudo-account. This account will hold XRP (if one asset is XRP) and serve as the LP token issuer.

**LP token issuance**: `ammLPTIssue` derives the LP token currency from the asset pair and the pseudo-account ID. `ammLPTokens` computes the initial supply as `sqrt(asset1 * asset2)` (the geometric mean), which is the standard constant-product AMM seeding formula. The LP tokens are created with a zero credit-limit trust line — a deliberate design choice called out in an inline comment: this prevents anyone from receiving LP tokens without affirmative action (a deposit, trust line creation, or offer crossing). The tokens are then sent from the pseudo-account to the creator.

**`ltAMM` object construction**: The AMM ledger entry is built with both assets stored in canonical order via `std::minmax`, ensuring the object's content always has a predictable low/high ordering regardless of transaction input order. `initializeFeeAuctionVote` populates the initial fee, auction slot, and voting state, with the creator automatically receiving the first auction slot and vote.

**Asset transfer via `sendAndInitTrustOrMPT`**: Each asset is transferred from the creator to the pseudo-account. For IOU assets, the standard trust line created by `accountSend` is then retrieved and marked with `lsfAMMNode`, distinguishing AMM-held trust lines from normal LP trust lines. For MPT assets, `createMPToken` establishes the pseudo-account's MPT holding with the `lsfMPTAMM` flag; if the MPT requires authorization and the pseudo-account hasn't been pre-authorized, `lsfMPTAuthorized` is also set. In both cases, `WaiveTransferFee::Yes` waives the transfer fee for the seeding transfer.

**Order book registration**: Both swap directions (asset1→asset2 and asset2→asset1) are registered with `OrderBookDB` at their respective initial exchange rates. This makes the new pool immediately visible to the payment and offer-crossing engines as a liquidity source.

## Relationship to Sibling Files

The dex transactor directory contains `AMMDeposit.cpp`, `AMMWithdraw.cpp`, `AMMBid.cpp`, `AMMVote.cpp`, `AMMDelete.cpp`, and `AMMClawback.cpp`. All of them locate their target pool via `keylet::amm()` — the same deterministic key established here. `AMMCreate` is thus the genesis point of a state machine whose lifecycle is terminated only by `AMMDelete` (when the pool is emptied). The `featureAMMClawback` guard in `preclaim` is mirrored by `AMMClawback.cpp`, which provides the controlled clawback path that makes the guard safe to lift.