# `src/libxrpl/tx/paths/OfferStream.cpp`

## Role in the System

`OfferStream.cpp` implements the order-book cursor used during payment processing on the XRPL decentralized exchange. When the payment engine (flow calculation) needs to cross the book for a given asset pair, it instantiates a `FlowOfferStream` and calls `step()` repeatedly to walk from the best-quality offer down to lower-quality ones, skipping or permanently removing any that are stale, expired, frozen, or effectively unfunded. The result of each successful `step()` is a typed `TOffer` representing a live, crossable offer at the tip.

The file is entirely template-driven. Both `TOfferStreamBase` and `FlowOfferStream` are parameterised on `TIn` and `TOut`, which can each be `XRPAmount`, `IOUAmount`, or `MPTAmount`. All eight combinations used in practice are explicitly instantiated at the bottom of the file, enforcing that the translation unit emits concrete machine code for every valid asset pairing including the newer `MPTAmount` pairings introduced alongside Multi-Purpose Tokens.

## The Two Views

The constructor takes two `ApplyView` references: `view_` and `cancelView_`. `view_` is the working ledger state where all side effects during the current transaction accumulate. `cancelView_` is a pristine snapshot of the ledger before the transaction began.

This two-view design is fundamental to one of the trickiest distinctions `step()` must make: whether an offer is *found unfunded* or *became unfunded*.

- **Found unfunded**: The owner's balance was already zero in `cancelView_`. This offer is genuinely bad and should be permanently removed from the ledger regardless of whether the current payment strand is committed.
- **Became unfunded**: The owner's balance dropped to zero only inside `view_`, because an earlier step in the same transaction consumed the funds. This offer is still valid in the original ledger and should only be skipped—not deleted—since the current payment attempt might not ultimately succeed.

`FlowOfferStream::permRmOffer()` records offers that qualify for permanent removal into a `boost::container::flat_set<uint256>`. The flat set is chosen for its cache-friendly sorted-array layout, which is efficient for small sets of removals typical in a single transaction.

## The `step()` Loop

`step()` is the main workhorse and carries the explicit comment: *"Modifying the order or logic of these operations causes a protocol breaking change."* Every early-exit path in the loop must maintain consensus compatibility. The checks proceed in this order:

1. **Missing ledger entry**: `BookTip::step()` deletes the current offer from the view before advancing. If the resulting `entry` pointer is null, `erase()` cleans up the dangling directory reference and the loop continues.
2. **Expiry**: If `sfExpiration` is present and the offer's deadline is at or before the current ledger close time, it is permanently removed.
3. **Zero amounts**: An offer with either `TakerPays` or `TakerGets` at zero is malformed and permanently removed.
4. **Deep freeze**: If the offer's input asset trust line is deep-frozen for the owner (`isDeepFrozen`), the offer is removed. Deep freeze is a stricter state than regular freeze, preventing even outbound transfers.
5. **Permissioned DEX domain**: If the offer carries an `sfDomainID` field and `permissioned_dex::offerInDomain` returns false (the owner or counterparty no longer meets domain criteria), the offer is removed.
6. **Owner funds check**: `accountFundsHelper` computes how much of `assetOut` the owner actually holds. A zero or negative balance triggers the found-unfunded vs. became-unfunded distinction described above.
7. **Small increased-quality check**: Even if the owner has some funds, the *effective* amounts after clamping to those funds may be so small that the offer's quality degrades below its stated quality. Such offers block the order book without providing meaningful liquidity and are removed.

## The `accountFundsHelper` Template

This file-local function unifies fund lookup across all three amount types via `if constexpr` branches:

- For `IOUAmount`, if the account is the issuer, the function returns `amtDefault` immediately—an issuer's balance with themselves is effectively unlimited for IOU purposes (they are self-funded).
- For `MPTAmount`, issuers are not unlimited; instead `issuerFundsToSelfIssue` computes the actual issuable headroom from the MPT's supply limits.
- For all other cases, `accountHolds` with `fhZERO_IF_FROZEN` and `ahZERO_IF_UNAUTHORIZED` ensures frozen trust lines and unauthorized accounts surface as zero funds, making those offers appear unfunded.

## `shouldRmSmallIncreasedQOffer`

This method guards against a subtle order-book-blocking scenario. When an owner's real funds fall below `offer_.amount().out`, the effective exchange amounts shrink proportionally via `quality().ceil_out_strict(...)` with `roundUp = false`. The `false` rounding is intentional: rounding up would prevent the blocking but could also raise the effective quality, which would itself distort the book ordering. The check then asks: is the effective input (`TTakerPays`) so small (at or below `minPositiveAmount()`) that rounding effects cause the effective quality to drop below the original stated quality? If so, the offer is removed.

The check is skipped entirely when `TakerGets` is XRP: because XRP is indivisible integer drops, the worst-case adjusted quality for a one-drop output is still astronomically good for any realistic IOU, so this protection is unnecessary.

## The `erase()` Repair Path

When a directory entry exists but points to a missing SLE, `erase()` manually removes the orphaned index from `sfIndexes` of the directory page. The code comment honestly acknowledges that this should use `ApplyView::dirRemove` (which would also clean up empty directories), but that change would alter how empty directories are handled consensus-wide and is therefore locked out as a protocol-breaking change. The current implementation leaves empty directories in place rather than risk breaking consensus.

## Explicit Template Instantiations

The eight `template class` lines at the bottom serve as the translation unit's public contract. They force the compiler to emit code for every combination—`IOUAmount×IOUAmount`, `XRPAmount×IOUAmount`, `IOUAmount×XRPAmount`, and all five `MPTAmount`-involved pairings—in this single compilation unit, keeping link times predictable and avoiding duplicate symbols across translation units that include the header.