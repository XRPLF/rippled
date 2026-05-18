# `AMMBid.cpp` — AMM Auction Slot Bidding Transactor

## Role in the System

This file implements the `AMMBid` transaction type, one of several AMM-specific transactors living under `src/libxrpl/tx/transactors/dex/`. It allows liquidity providers (LPs) to compete for the AMM's single **auction slot** — a 24-hour exclusive grant that entitles the holder and up to four authorized accounts to trade at a deeply discounted fee. Every AMM instance has exactly one auction slot, and the bidding mechanism is designed so that a competitive market in slot ownership benefits all LPs via LP token burns.

The sibling transactors (`AMMCreate`, `AMMDeposit`, `AMMWithdraw`, `AMMVote`, `AMMDelete`) handle other AMM lifecycle operations; `AMMBid` is specific to the fee-discount auction mechanic.

## Auction Slot Economics

The slot's pricing is governed by constants from `AMMCore.h`:

- **Slot duration**: 24 hours, divided into 20 equal intervals of 72 minutes each (`AUCTION_SLOT_TIME_INTERVALS = 20`)
- **Discounted fee**: the AMM's trading fee divided by 10 (`AUCTION_SLOT_DISCOUNTED_FEE_FRACTION = 10`)
- **Minimum slot price**: `lptAMMBalance × tradingFee / 25`, computed freshly at apply time against the current total LP token supply

Bid amounts are always denominated in LP tokens, not XRP or the AMM's underlying assets. This design ensures that the cost of holding a discount slot scales with the AMM's size.

When an active holder is outbid, the new bidder pays a computed price that includes a 5% premium (`p1_05 = 1.05`) over the original purchase price, adjusted downward as the slot ages. The formula applies `1 - fractionUsed^60` as a decay multiplier (where `fractionUsed = (timeSlot + 1) / 20`), making it progressively cheaper to outbid a slot holder the further into the 24-hour window they are. In the first interval (slot 0) the decay term is omitted and the full 5% premium applies. The previous holder is refunded `(1 - fractionUsed) × pricePurchased` in LP tokens, representing the unused fraction of their slot. The bid amount minus this refund is **permanently burned** by calling `redeemIOU` to destroy the tokens and decrementing `sfLPTokenBalance` on the AMM ledger entry. This deflationary burn benefits all remaining LPs.

The **tailing slot** (slot 19) is treated specially: `validOwner` uses `< tailingSlot` rather than `<=`. At the last interval the holder gets no refund, pays minimum price, and the cheapest rational action is to simply let the slot expire.

## Transaction Processing Pipeline

### `checkExtraFeatures`

Called before `preflight` to test feature flag compatibility. It rejects the transaction if the `ammEnabled` guard fails (the core AMM amendment), or if the transaction references MPT-typed assets while `featureMPTokensV2` is not yet active. This keeps the AMM from processing asset types that the ledger rules don't yet support.

### `preflight` (stateless validation)

Validates the transaction fields without touching ledger state. It rejects malformed asset pairs via `invalidAMMAssetPair()`, validates the optional `sfBidMin`/`sfBidMax` amounts via `invalidAMMAmount()`, and caps the `sfAuthAccounts` array at `AUCTION_SLOT_MAX_AUTH_ACCOUNTS = 4`. Under the `fixAMMv1_3` amendment, it additionally enforces that no auth account appears more than once and that the submitter's own account is not in the list — a deduplication check that was absent in the initial deployment.

### `preclaim` (stateful validation)

Reads the AMM ledger entry and performs live checks. It rejects if the AMM doesn't exist (`terNO_AMM`), if the pool is empty (`tecAMM_EMPTY`), or if any listed auth account lacks a ledger entry (`terNO_ACCOUNT`). Critically it verifies that the submitter is actually an LP (`ammLPHolds()` returning non-zero) and that any `sfBidMin`/`sfBidMax` values do not exceed the submitter's own holdings and are consistent with the correct LP token asset type. A `bidMin > bidMax` cross-check catches inverted ranges.

### `doApply` and `applyBid`

`doApply` wraps execution in a `Sandbox` — all mutations are staged against a copy of the view and committed to `ctx_.rawView()` only if `applyBid` succeeds. This is the standard XRPL transactor pattern for atomic application.

`applyBid` is a file-scope free function (not a class method), which is architecturally deliberate: it takes explicit parameters and has no hidden access to `AMMBid` member state, making its preconditions clear and testable. It contains two nested lambdas:

- **`getPayPrice`**: Given a computed market price, constrains it to the `[sfBidMin, sfBidMax]` range. If `sfBidMax` is present and the market price exceeds it, the bid fails with `tecAMM_FAILED`. If only `sfBidMin` is present, the bidder pays the maximum of their stated minimum and the market price. This lets callers set a price floor (ensuring they get the slot if they want it) or a ceiling (capping their exposure).

- **`updateSlot`**: Atomically rewrites all `sfAuctionSlot` fields — owner account, expiration, discounted fee, slot price, and auth accounts — then burns the token amount by calling `redeemIOU` on the sandbox and updating `sfLPTokenBalance`. Using `adjustLPTokens(..., IsDeposit::No)` corrects for the 16-digit precision loss inherent in IOU arithmetic when subtracting from the running LP balance.

### Feature Flag Handling in `applyBid`

The `fixInnerObjTemplate` amendment changes how the `sfAuctionSlot` inner object is managed. Before the fix, the code would lazily add the field if absent via `makeFieldPresent`. After the fix, the slot must already be present (initialized during AMM creation), and the code asserts this with `XRPL_ASSERT`, returning `tecINTERNAL` if violated. This shift from lazy initialization to eager initialization prevents a class of inconsistency bugs where the object might be partially constructed.

## Error Handling and Invariants

Two error paths are annotated `// LCOV_EXCL_START` — the case where the LP token burn amount would equal or exceed the total AMM balance, and the case where the computed refund exceeds the pay price. Both are mathematically impossible given valid inputs, but the code still guards them to catch any future numerical regression. They return `tecINTERNAL` rather than silently producing corrupted state.

The entire apply path is guarded by the `Sandbox` pattern: no ledger change is visible outside the transaction unless `applyBid` returns a success code. Any intermediate failure (failed `accountSend` for the refund, failed `redeemIOU` for the burn) rolls back cleanly without partial writes.