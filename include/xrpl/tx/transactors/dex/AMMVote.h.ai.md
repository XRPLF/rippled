# `AMMVote.h` — AMM Trading Fee Governance Transactor

`AMMVote.h` declares the `AMMVote` transactor, the governance mechanism through which liquidity providers (LPs) collectively control the trading fee of an Automated Market Maker instance on the XRP Ledger. Rather than fixing the fee at pool creation or entrusting it to a single authority, the design distributes fee control proportionally to capital — each LP votes with a weight derived from their share of the pool's total `LPToken` supply.

## Role in the System

This header is part of the DEX transactor family alongside `AMMCreate`, `AMMDeposit`, `AMMBid`, and others. Each class in this directory inherits from `Transactor` and implements the same three-phase pipeline: `preflight` (stateless field validation), `preclaim` (read-only ledger checks), and `doApply` (state-mutating apply). `AMMVote` participates in this pattern without deviation, and its `ConsequencesFactory{Normal}` declaration signals that the framework should handle fee and sequence number consequences in the standard way — no custom fee scaling and no transaction blocking semantics.

## The Three-Phase Contract

**`checkExtraFeatures`** acts as an amendment gate. It returns `false` (causing `invokePreflight` to emit `temDISABLED`) when the core AMM amendment is not enabled. It also enforces a secondary check: if either pool asset is an MPT (`MPTIssue`), the `featureMPTokensV2` amendment must additionally be active. This guards against submitting MPT-based vote transactions before the network has upgraded.

**`preflight`** is the stateless fast path. It validates that the asset pair is structurally coherent via `invalidAMMAssetPair` and that the proposed `sfTradingFee` does not exceed `TRADING_FEE_THRESHOLD` (1000, representing 1%). Catching malformed fees here — before any ledger reads — avoids burning preclaim resources on obviously invalid input.

**`preclaim`** performs the first ledger-dependent checks with a read-only view. It verifies that the AMM object exists for the specified asset pair, that the pool is not empty (a zero `sfLPTokenBalance` means there are no LPs), and — critically — that the submitting account actually holds LPTokens via `ammLPHolds`. An account with no stake in the pool has no standing to influence its fee, so `tecAMM_INVALID_TOKENS` is returned early.

**`doApply`** wraps execution in a `Sandbox` view so that all ledger mutations are applied atomically only on success. The actual logic is delegated to the file-scoped `applyVote` function in the implementation.

## Vote Slot Management

The `ltAMM` ledger object stores up to `VOTE_MAX_SLOTS` (8) `VoteEntry` objects in its `sfVoteSlots` array. Each entry records the voting account, the proposed `sfTradingFee`, and a `sfVoteWeight` — the LP's proportional token holding scaled by `VOTE_WEIGHT_SCALE_FACTOR` (100,000).

During `applyVote`, stale entries (accounts that no longer hold LPTokens) are silently pruned on each vote transaction, keeping the slot array clean without requiring a separate maintenance transaction. When a new voter arrives and all eight slots are occupied, the entry with the fewest tokens is a candidate for eviction. The incoming vote only displaces it if the new LP holds *more* tokens than the current minimum holder, or holds equal tokens but proposes a higher fee. Tiebreaking by account ID (`account < minAccount`) ensures the eviction decision is deterministic across all validators. If no slot can be displaced, the transaction still succeeds but has no effect on the slot array — it simply refreshes existing entries.

## Fee Calculation and Auction Slot Coupling

The new `sfTradingFee` is computed as a weighted average: `sum(fee_i * lpTokens_i) / sum(lpTokens_i)` using running `num` and `den` accumulators over the updated slot array. Notably, the stored `sfVoteWeight` per entry is not used in this arithmetic — the fee is recalculated from raw token holdings each time. This avoids rounding drift that would accumulate if the fee were derived from previously stored (already-rounded) weights.

After updating `sfTradingFee` on the AMM object, the transactor also propagates a change to `sfDiscountedFee` inside the `sfAuctionSlot` subobject if one is present. The discounted fee is `tradingFee / AUCTION_SLOT_DISCOUNTED_FEE_FRACTION` (divided by 10), keeping the auction slot winner's price advantage in sync with any governance-driven fee change. When either fee rounds to zero, the field is removed rather than stored as zero, preserving ledger object compactness.

## Design Rationale

The eight-slot cap and stake-weighted eviction policy together prevent griefing: an attacker cannot cheaply flood the vote array with dust-sized positions to lock out larger LPs. The proportional weighting ensures that votes from well-capitalized LPs dominate, aligning fee governance with economic exposure to the pool's performance — a deliberate parallel to shareholder voting in traditional finance.