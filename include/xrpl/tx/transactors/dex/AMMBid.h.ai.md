# `AMMBid.h` — AMM Auction Slot Bid Transactor

`AMMBid` declares the transactor that implements XRPL's continuous auction mechanism for AMM trading-fee slots, as specified in [XLS-30d](https://github.com/XRPLF/XRPL-Standards/discussions/78). Its purpose is to let liquidity providers (LPs) bid—using their own LP tokens—for a 24-hour window during which they trade against the pool at a heavily discounted fee.

## Context in the DEX Transactor Family

`AMMBid.h` lives alongside nine other DEX transactors (`AMMCreate`, `AMMDeposit`, `AMMWithdraw`, `AMMVote`, `AMMDelete`, `AMMClawback`, `OfferCreate`, `OfferCancel`). Every AMM transactor in this directory follows the same minimal declaration pattern: a single class that inherits `Transactor`, re-declares `ConsequencesFactory`, and exposes the three static phase entry-points plus the virtual `doApply`. The header is intentionally thin — all logic lives in the corresponding `.cpp`.

## Auction Slot Semantics

The slot's lifetime is 24 hours divided into 20 equal intervals (each ~72 minutes). At any moment the slot is in one of three states:

- **Empty** — no current holder; minimum price applies.
- **Occupied** — a holder exists and is in interval 1–18 (at least 5 % of time remains); the outbid formula applies.
- **Tailing** — holder is in the final interval (< 5 % remaining); the holder retains privileges but receives no refund and effectively pays only the minimum.

The implementation in `applyBid()` captures this with a `validOwner` lambda that returns true only when `timeSlot` is set and less than `tailingSlot` (interval 19). This means displacing a tailing holder costs nothing beyond the minimum price — a design choice that discourages camping on a nearly-expired slot.

The pricing curve is:

```
computedPrice = pricePurchased * 1.05 * (1 - fractionUsed^60) + minSlotPrice
```

The `power(fractionUsed, 60)` term causes the curve to decay rapidly in early intervals (when `fractionUsed` is small) and flatten near expiry. The 5 % multiplier on `pricePurchased` ensures there is always a meaningful premium over the previous bid, preventing token-free slot squatting.

## Three-Phase Processing

`AMMBid` uses the standard XRPL three-phase transaction lifecycle:

**`checkExtraFeatures`** runs inside `invokePreflight` before anything else. It gates the entire transaction on two amendment checks: `ammEnabled(ctx.rules)` for the base AMM feature, and `featureMPTokensV2` if either pool asset is an MPT. Returning `false` yields `temDISABLED`, which means the transaction type is treated as unknown — it won't claim a fee. This is the canonical XRPL pattern for tying a transaction type to a specific ledger amendment.

**`preflight`** validates the transaction itself without ledger access. It rejects malformed asset pairs (via `invalidAMMAssetPair`), validates that any `BidMin`/`BidMax` amounts are well-formed LP token quantities, and enforces that `AuthAccounts` contains at most four accounts. Under `fixAMMv1_3` it additionally prevents duplicate account entries and self-authorization — a fix for an edge case in the original spec.

**`preclaim`** performs read-only ledger validation. It confirms the AMM object exists for the specified asset pair, the pool is not empty (`sfLPTokenBalance != 0`), all `AuthAccounts` reference existing on-ledger accounts, and the submitting account actually holds LP tokens. It also cross-checks that any `BidMin`/`BidMax` amounts share the same LP token asset type and do not exceed the submitter's own balance — catching attempts to bid with someone else's tokens.

**`doApply`** wraps execution in a `Sandbox` over the current ledger view. This allows the `applyBid` helper to build all ledger mutations — LP token burns, refunds to the previous slot holder, and the updated `AuctionSlot` object inside `ltAMM` — as a tentative set. Only on full success does `sb.apply(ctx_.rawView())` commit them atomically. Any internal failure leaves the ledger untouched.

## Revenue Distribution Inside `applyBid`

The static `applyBid` function, called exclusively by `doApply`, computes:

1. **Refund to previous holder**: `(1 − fractionUsed) × pricePurchased` LP tokens transferred directly from the bidder to the outgoing slot-holder via `accountSend`.
2. **Burn**: `payPrice − refund` LP tokens are redeemed and removed from `sfLPTokenBalance` on the AMM object. Burning LP tokens increases the proportional share of all remaining LPs, so the pool itself benefits from each auction cycle.

The `updateSlot` lambda writes the new `sfAccount`, `sfExpiration` (current time + `TOTAL_TIME_SLOT_SECS`), `sfDiscountedFee` (set to `tradingFee / AUCTION_SLOT_DISCOUNTED_FEE_FRACTION`, or absent if zero), `sfPrice`, and `sfAuthAccounts`. Removing `sfDiscountedFee` when the fee is zero rather than storing a zero value keeps the ledger object compact.

## `ConsequencesFactory` and Error Taxonomy

`ConsequencesFactory{Normal}` signals to the transaction queue that `AMMBid` does not block other transactions from the same account while it is pending — it neither depletes the full account balance like a payment nor locks the account like an `AccountDelete`. The choice reflects that a bid only consumes a bounded amount of LP tokens.

Error codes returned span both `NotTEC` (preflight: `temMALFORMED`, `temBAD_AMM_TOKENS`) and `TER` (preclaim/apply: `terNO_AMM`, `terNO_ACCOUNT`, `tecAMM_EMPTY`, `tecAMM_INVALID_TOKENS`, `tecAMM_FAILED`), following XRPL conventions where `ter*` codes may retry and `tec*` codes claim a fee.