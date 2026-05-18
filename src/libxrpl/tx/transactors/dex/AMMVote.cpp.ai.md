# `AMMVote.cpp` — AMM Trading Fee Governance Transactor

This file implements the `AMMVote` transactor, which allows liquidity providers (LPs) to participate in on-chain governance of an AMM pool's trading fee. It is one of the DEX-specific transactors in `src/libxrpl/tx/transactors/dex/` and follows the standard XRPL three-phase transaction pipeline: feature-gate check, stateless preflight validation, stateful preclaim validation, and mutation application.

## Transaction Pipeline

**`checkExtraFeatures()`** is the first gate. It enforces that the base AMM amendment is active via `ammEnabled()`, and also blocks `AMMVote` transactions on MPT-backed pools unless `featureMPTokensV2` is separately enabled. This two-layer gating pattern appears across AMM transactors: the base feature enables the DEX, while newer token type support requires its own amendment.

**`preflight()`** performs stateless validation before any ledger reads. It calls `invalidAMMAssetPair()` to reject malformed or identical asset pairs, and enforces the `TRADING_FEE_THRESHOLD` ceiling of 1000 basis points (1%). A fee of exactly zero is legal — an LP can vote for a free pool.

**`preclaim()`** performs three ledger-state checks in sequence: the AMM must exist for the specified asset pair (`terNO_AMM`), the pool's total `sfLPTokenBalance` must not be zero (`tecAMM_EMPTY`), and the submitting account must hold a non-zero balance of LP tokens (`tecAMM_INVALID_TOKENS`). The third check is critical to the governance model: only current LPs may vote, preventing external actors from influencing fee dynamics.

## Vote Application Logic

The real work lives in the file-scope static function `applyVote()`. Isolating it from the `Transactor` class hierarchy is deliberate — it keeps the logic testable independently and avoids bloating the virtual dispatch surface.

### Vote Slot Maintenance

The AMM ledger entry (`sfVoteSlots`) holds up to `VOTE_MAX_SLOTS` (8) vote entries. Each entry records the voter's `AccountID`, their proposed `sfTradingFee`, and a computed `sfVoteWeight`. On every vote execution, the function iterates all existing entries and re-evaluates each voter's current LP balance via `ammLPHolds()`. Entries where the account no longer holds LP tokens are silently dropped rather than explicitly removed — they are simply not pushed into `updatedVoteSlots`. This passive eviction keeps the slot array clean without requiring a separate cleanup transaction.

For the submitting account, if an existing entry is found (`foundAccount = true`), its fee value is updated in place to `feeNew` and its token balance is refreshed to `lpTokensNew`. The iteration simultaneously accumulates running numerator (`num`) and denominator (`den`) sums for the weighted-average fee calculation.

### Slot Eviction Policy

When the submitting account is new (no existing entry) and fewer than eight slots are occupied, a new entry is appended unconditionally. When all slots are full, the function must decide whether to evict the weakest current voter. The eviction criterion is: the newcomer must hold **more** LP tokens than the least-token holder, or hold equal tokens and propose a **higher** fee. If neither condition holds, no eviction occurs and the voter is not recorded — but the transaction still succeeds. This design choice is significant: it treats the fee recalculation (which still happens over stale balances) as valuable even when no new vote can be inserted.

The minimum-slot detection is made deterministic through a three-level comparison: fewest LP tokens first, then lowest fee, then lowest `AccountID`. This lexicographic ordering ensures all validators reach the same eviction decision regardless of iteration order differences.

### Weighted Fee Recalculation

After building `updatedVoteSlots`, the effective fee is computed as `num / den` using the XRPL `Number` type (arbitrary precision rational arithmetic). The result is cast to `std::int64_t` to truncate to an integer basis-point value. If the result is non-zero, it is written to `sfTradingFee` on the AMM SLE. If it rounds to zero, the field is explicitly made absent with `makeFieldAbsent()` rather than being set to zero. This matters because absent fields serialize differently than present-but-zero fields in the XRPL's canonical binary format.

### Auction Slot Side Effect

Each fee update has a cascading effect on the AMM's auction slot. The `sfDiscountedFee` field inside `sfAuctionSlot` is recalculated as `fee / AUCTION_SLOT_DISCOUNTED_FEE_FRACTION` (dividing by 10, so the discounted rate is one-tenth of the new trading fee). If the division yields zero, `sfDiscountedFee` is cleared. This tight coupling means every successful vote touches three conceptually distinct areas of the AMM SLE: the vote array, the pool-wide trading fee, and the auction slot's discount rate.

An `XRPL_ASSERT` guards the structural invariant that `sfAuctionSlot` is present when the `fixInnerObjTemplate` amendment is active. This acts as a consensus-critical sanity check: if the AMM's SLE is ever malformed, the assert fires loudly during validation rather than silently corrupting the auction fee.

## Sandbox Isolation

`doApply()` wraps all mutations in a `Sandbox` constructed over the current apply view. Only on `tesSUCCESS` (the second element of `applyVote()`'s return pair) does it call `sb.apply()` to flush changes into the real ledger view. This two-phase commit pattern is standard across XRPL transactors and ensures that any failure path inside `applyVote()` — including the `tecINTERNAL` guard on the AMM SLE peek — leaves the ledger completely unmodified.

## Key Constants

From `AMMCore.h`, the constants governing this transactor are:
- `VOTE_MAX_SLOTS = 8` — maximum concurrent vote entries per AMM
- `VOTE_WEIGHT_SCALE_FACTOR = 100000` — vote weight is expressed in units of 1/100,000 of total LP supply
- `TRADING_FEE_THRESHOLD = 1000` — maximum votable fee (1%, since fees are in units of 1/100,000)
- `AUCTION_SLOT_DISCOUNTED_FEE_FRACTION = 10` — auction slot discount is always one-tenth of the current trading fee