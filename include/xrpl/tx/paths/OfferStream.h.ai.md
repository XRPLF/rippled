# `include/xrpl/tx/paths/OfferStream.h`

## Role in the System

`OfferStream.h` defines the templated classes that serve as the order-book iterator for XRPL's payment engine. During a payment (either a simple `Payment` transaction or offer-crossing), the engine must traverse offers sorted by quality — highest quality first — consuming or removing them. `TOfferStreamBase` and `FlowOfferStream` encapsulate that traversal, including all the cleanup logic for invalid, expired, or unfunded offers discovered along the way.

The file sits at the heart of the `tx/paths` subsystem, used directly by `BookStep.cpp` — the component that evaluates one leg of a multi-hop payment path. Without this abstraction, BookStep would have to inline all the offer validation, expiry handling, and ledger-cleanup decisions; OfferStream isolates that complexity cleanly.

## Class Structure

### `TOfferStreamBase<TIn, TOut>`

The base template is parameterized on the input and output amount types (`XRPAmount`, `IOUAmount`, or `MPTAmount`). This design allows a single implementation to serve all six canonical currency-pair combinations — XRP↔IOU, IOU↔IOU, MPT↔XRP, MPT↔IOU, MPT↔MPT, and their reverses — without virtual dispatch overhead on the hot path. The `.cpp` file explicitly instantiates all eight combinations.

The base holds two `ApplyView` references: `view_` and `cancelView_`. This dual-view design is central to a subtle but critical distinction between "found unfunded" and "became unfunded" offers. `view_` accumulates changes from the in-progress transaction; `cancelView_` is a pristine snapshot of the ledger before the transaction. During `step()`, when an offer's owner has zero funds, the implementation checks the same balance in `cancelView_`. If the balance is zero there too, the offer was *already* unfunded before this transaction touched anything — it must be permanently removed from the ledger. If the balance is non-zero in `cancelView_` but zero in `view_`, the owner became unfunded because an earlier strand in the same payment consumed their balance; the offer should be skipped for now but not permanently deleted, since it might be valid if this strand is rolled back.

The `permRmOffer()` virtual method is the hook for permanently scheduling an offer for removal. The base class calls it but leaves the storage to the concrete subclass — which is how `FlowOfferStream` accumulates the `permToRemove_` set.

### `StepCounter`

`StepCounter` is a nested guard with a single responsibility: enforcing a ceiling on the total number of offers examined per payment. Each call to `StepCounter::step()` increments an internal count and returns `false` once `limit_` is reached, causing the iteration to terminate. This is an essential denial-of-service protection: without a step budget, a pathological order book with thousands of tiny or invalid offers could force a validator to perform unbounded work while processing a single transaction. In `BookStep.cpp`, this limit is `MaxOffersToConsume`, and the final count is returned to the caller so the engine can track total resource usage across strands.

### `FlowOfferStream<TIn, TOut>`

`FlowOfferStream` is the concrete implementation for the Flow payment engine (as opposed to the legacy `RippleCalc` path). Its sole addition over the base is `permToRemove_` — a `boost::container::flat_set<uint256>` collecting offer indices that should be permanently erased from the ledger. `flat_set` is chosen deliberately: the set is small (typically just a few entries per payment), operations are cache-friendly, and iteration cost at cleanup time is trivial. The set is exposed read-only via `permToRemove()` so `BookStep` can apply the removals after the strand completes — even if the strand itself was discarded.

The `permRmOffer()` override in `FlowOfferStream` supports self-crossed offer removal, referenced in comments pointing to `BookOfferCrossingStep::limitSelfCrossQuality()`. This is the mechanism by which self-crossing offers (where the same account is on both sides of a trade) can be permanently cleaned up during offer-crossing transactions, not just skipped.

## The `step()` Loop

The main `step()` method in `TOfferStreamBase` drives all offer validation and is labeled with a comment: *"Modifying the order or logic of these operations causes a protocol breaking change."* That comment matters: the sequence in which offers are removed or skipped directly determines ledger state, so the exact logic must be consensus-identical across all nodes.

Each iteration of the loop:
1. Delegates to `BookTip::step()` (the raw order-book cursor) to advance to the next offer entry.
2. Enforces the `StepCounter` budget.
3. Removes the offer if its ledger entry is missing (defensive cleanup for corrupted directory state).
4. Removes the offer if it is expired, based on `sfExpiration` compared to the transaction's close time.
5. Skips or permanently removes the offer if it has zero amounts (corrupted offer).
6. Removes the offer if the asset's trust line is deep-frozen.
7. Removes the offer if it belongs to a permissioned DEX domain that no longer matches.
8. Computes owner funds and distinguishes "found unfunded" (permanent removal) from "became unfunded" (skip only).
9. Invokes `shouldRmSmallIncreasedQOffer()` to handle the rounding-granularity edge case.

### Quality Degradation and `shouldRmSmallIncreasedQOffer()`

Offer quality on the ledger is frozen at creation time (a deliberate XRPL business rule preserving fairness on partial fills). However, XRP's integer-drop granularity means that when an offer is partially funded — the owner has less than the offer's `TakerGets` — the effective amounts after rounding can yield a quality *worse* than the stored quality. Consumers earlier in the order book would have already been given the stated quality, so presenting an offer at a quality it can no longer deliver is misleading and can block the book. `shouldRmSmallIncreasedQOffer()` detects this: if the effective quality after accounting for owner funds and rounding is less than the stored quality, and the effective `in` amount is at or below `minPositiveAmount()`, the offer is stale and should be purged.

This check is skipped when `TakerGets` is XRP, because an XRP-output offer can only get *better* in quality (a minimum of 1 drop remains deliverable at high quality for almost any realistic IOU amount).

## The `erase()` Method and Technical Debt

`erase()` handles the case where a directory entry exists but the corresponding offer ledger entry is missing. It manually removes the index from the directory page's `sfIndexes` vector. A comment in the implementation explicitly acknowledges that this *should* use `ApplyView::dirRemove`, which correctly handles empty-page cleanup, but that doing so would be a protocol-breaking change because it would alter the ledger entries touched by payment transactions. This is a tracked legacy constraint — the current implementation leaves orphaned empty directory pages in edge cases.

## Relationships

- **`BookTip`** (`BookTip.h`): provides raw offer iteration from the order book directory structure; `TOfferStreamBase` wraps it and adds all validation logic.
- **`TOffer<TIn, TOut>`** (`Offer.h`): the typed offer abstraction, carrying amounts, quality, owner, and `consume()` logic; `TOfferStreamBase::offer_` is the current offer exposed via `tip()`.
- **`BookStep.cpp`**: the primary consumer; constructs a `FlowOfferStream`, calls `step()` in a loop to fill the payment strand, and after completion applies `permToRemove()` to purge invalid offers from the ledger.
- **`ApplyView`** (`ledger/View.h`): the transactional ledger view abstraction; both `view_` and `cancelView_` satisfy this interface.