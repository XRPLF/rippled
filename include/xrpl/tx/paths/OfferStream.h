/** @file
 *  Order-book iterator used by the Flow payment engine.
 *
 *  Defines `TOfferStreamBase` and `FlowOfferStream`, which together provide
 *  validated, quality-ordered traversal of a single order-book leg during
 *  payment processing.  All offer validation, expiry handling, and
 *  unfunded-offer detection are encapsulated here so that `BookStep` only
 *  needs to drive the `step()` loop.
 */
#pragma once

#include <xrpl/basics/Log.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/View.h>
#include <xrpl/protocol/Concepts.h>
#include <xrpl/tx/paths/BookTip.h>
#include <xrpl/tx/paths/Offer.h>

#include <boost/container/flat_set.hpp>

namespace xrpl {

/** Base order-book iterator for the Flow payment engine.
 *
 *  Wraps a `BookTip` cursor and adds per-offer validation: expired offers,
 *  missing ledger entries, zero-amount offers, deep-frozen trust lines,
 *  permissioned-DEX domain mismatches, and unfunded-offer detection are all
 *  handled inside `step()`.
 *
 *  The dual-view design (`view_` / `cancelView_`) is critical for correctness:
 *  when an owner's balance is zero in `view_` the implementation checks the
 *  same balance in the pristine `cancelView_`.  A zero balance in both views
 *  means the offer was *already* unfunded before this transaction began and
 *  must be permanently removed.  A zero balance only in `view_` means an
 *  earlier strand consumed the funds; the offer is skipped but not deleted
 *  (it may be valid if that strand is rolled back).
 *
 *  @tparam TIn   Amount type for the book's input asset (`XRPAmount`,
 *      `IOUAmount`, or `MPTAmount`).
 *  @tparam TOut  Amount type for the book's output asset.
 *
 *  @note The order and logic of operations inside `step()` are consensus-
 *      critical.  Changing them constitutes a protocol-breaking change.
 *
 *  @see FlowOfferStream, BookTip, TOffer
 */
template <StepAmount TIn, StepAmount TOut>
class TOfferStreamBase
{
public:
    /** DoS guard that enforces a maximum number of offers examined per payment.
     *
     *  Each call to `step()` increments an internal counter.  Once the counter
     *  reaches the configured limit the method returns `false`, terminating
     *  offer iteration.  This prevents a pathological order book with many
     *  tiny or invalid offers from forcing unbounded validator work within a
     *  single transaction.
     *
     *  In `BookStep`, the limit is `MaxOffersToConsume` and the final count is
     *  returned to the caller so the engine can track total resource usage
     *  across all strands.
     */
    class StepCounter
    {
    private:
        std::uint32_t const limit_;
        std::uint32_t count_{0};
        beast::Journal j_;

    public:
        /** Construct a counter with the given step budget.
         *
         *  @param limit Maximum number of offers that may be examined.
         *  @param j     Journal for logging when the limit is exceeded.
         */
        StepCounter(std::uint32_t limit, beast::Journal j) : limit_(limit), j_(j)
        {
        }

        /** Increment the step count and check the budget.
         *
         *  @return `true` if the budget has not yet been exhausted;
         *      `false` once `limit_` offers have been examined.
         */
        bool
        step()
        {
            if (count_ >= limit_)
            {
                JLOG(j_.debug()) << "Exceeded " << limit_ << " step limit.";
                return false;
            }
            count_++;
            return true;
        }

        /** Return the number of offers examined so far. */
        [[nodiscard]] std::uint32_t
        count() const
        {
            return count_;
        }
    };

protected:
    beast::Journal const j_;
    ApplyView& view_;        ///< Transactional view accumulating current-payment mutations.
    ApplyView& cancelView_;  ///< Pristine pre-transaction snapshot used for unfunded-offer detection.
    Book book_;
    bool validBook_;
    NetClock::time_point const expire_;  ///< Close time of the ledger being built; used for expiry checks.
    BookTip tip_;
    TOffer<TIn, TOut> offer_;  ///< The validated offer at the current position; valid only after a successful `step()`.
    std::optional<TOut> ownerFunds_;  ///< Cached owner funds for the current offer; set by `step()`, nullopt between iterations.
    StepCounter& counter_;

    /** Remove a directory entry whose corresponding offer ledger object is missing.
     *
     *  Surgically erases the orphaned index from the directory page's
     *  `sfIndexes` vector in @p view.  This is a best-effort cleanup that
     *  deliberately avoids `ApplyView::dirRemove` to preserve protocol
     *  compatibility — using `dirRemove` would alter which ledger entries a
     *  payment touches, constituting a protocol-breaking change.  As a
     *  consequence, an empty directory page may be left behind in edge cases.
     *
     *  @param view The view to apply the directory patch to; called for both
     *      `view_` and `cancelView_` when an entry is missing.
     */
    void
    erase(ApplyView& view);

    /** Schedule an offer for permanent removal from the ledger.
     *
     *  Called by `step()` for offers that must be deleted regardless of whether
     *  the current strand is ultimately committed.  The concrete subclass
     *  decides how to store the index (e.g., `FlowOfferStream` accumulates it
     *  in `permToRemove_`).
     *
     *  @param offerIndex Ledger key of the offer to be permanently removed.
     */
    virtual void
    permRmOffer(uint256 const& offerIndex) = 0;

    /** Determine whether a partially-funded offer should be removed due to
     *  quality degradation caused by integer rounding.
     *
     *  Offer quality is frozen at creation time for fairness on partial fills.
     *  When an owner holds less than the offer's `TakerGets`, the effective
     *  amounts after rounding can produce a quality *worse* than the stored
     *  quality, which would misrepresent the offer to later consumers.  This
     *  method detects that condition: if the effective quality is lower than
     *  the stored quality *and* the effective input amount is at or below
     *  `minPositiveAmount()`, the offer is considered stale and should be
     *  purged.
     *
     *  The check is skipped when `TakerGets` is XRP because an XRP-output
     *  offer can only improve in effective quality (a minimum of 1 drop
     *  remains deliverable at arbitrarily high quality for any realistic IOU
     *  input amount).
     *
     *  @tparam TTakerPays Compile-time amount type for TakerPays (input side).
     *  @tparam TTakerGets Compile-time amount type for TakerGets (output side).
     *  @return `true` if the offer should be removed; `false` if it is still
     *      usable.
     */
    template <class TTakerPays, class TTakerGets>
        requires ValidTaker<TTakerPays, TTakerGets>
    [[nodiscard]] bool
    shouldRmSmallIncreasedQOffer() const;

public:
    /** Construct an offer stream for the given order book.
     *
     *  @param view       Mutable ledger view accumulating transaction changes.
     *  @param cancelView Pristine ledger snapshot preceding this transaction,
     *      used to distinguish "found unfunded" from "became unfunded" offers.
     *  @param book       The currency-pair order book to iterate.
     *  @param when       Close time of the ledger under construction; offers
     *      whose `sfExpiration` is ≤ this value are removed.
     *  @param counter    Step-budget guard shared with the caller; iteration
     *      stops when the budget is exhausted.
     *  @param journal    Logging sink.
     */
    TOfferStreamBase(
        ApplyView& view,
        ApplyView& cancelView,
        Book const& book,
        NetClock::time_point when,
        StepCounter& counter,
        beast::Journal journal);

    virtual ~TOfferStreamBase() = default;

    /** Return the offer at the tip of the order book.
     *
     *  Offers are always presented in decreasing quality order.
     *
     *  @note Only valid when the most recent call to `step()` returned `true`.
     */
    [[nodiscard]] TOffer<TIn, TOut>&
    tip() const
    {
        return const_cast<TOfferStreamBase*>(this)->offer_;
    }

    /** Advance to the next valid offer in the order book.
     *
     *  Automatically skips or permanently removes offers that are:
     *  - Missing their ledger entry (corrupted directory state)
     *  - Expired (`sfExpiration` ≤ close time)
     *  - Zero-amount (corrupted offer)
     *  - Backed by a deep-frozen trust line
     *  - No longer matching their permissioned DEX domain
     *  - Found unfunded (owner balance was already zero before this transaction)
     *  - Stale due to quality degradation from integer rounding
     *
     *  "Found unfunded" offers are permanently scheduled for removal via
     *  `permRmOffer()`.  Offers that *became* unfunded because an earlier
     *  strand consumed the owner's balance are skipped but not permanently
     *  removed, since they may be valid if that strand is rolled back.
     *
     *  @return `true` if a valid offer is now available via `tip()`;
     *      `false` when the book is exhausted or the step budget is exceeded.
     *
     *  @note Modifying the order or logic of the checks in this method
     *      constitutes a protocol-breaking change.
     */
    bool
    step();

    /** Return the owner's available funds for the current offer's output asset.
     *
     *  Reflects the balance in the in-progress transactional view, capped to
     *  zero if the trust line is frozen or the holder is unauthorized.
     *
     *  @note Only valid when the most recent call to `step()` returned `true`.
     */
    [[nodiscard]] TOut
    ownerFunds() const
    {
        return *ownerFunds_;  // NOLINT(bugprone-unchecked-optional-access) always set after step()
                              // is called
    }
};

/** Concrete order-book iterator for the Flow payment engine.
 *
 *  Extends `TOfferStreamBase` with a `permToRemove_` set that accumulates the
 *  ledger keys of offers that must be erased from the ledger regardless of
 *  whether the strand that found them is ultimately committed.  `BookStep`
 *  reads this set after the strand completes and applies the removals via
 *  `ApplyView`.
 *
 *  `boost::container::flat_set` is used because the set is typically small
 *  (a handful of entries per payment), making cache-friendly sorted-array
 *  storage faster than a node-based container for both insertion and
 *  iteration.
 *
 *  @tparam TIn   Amount type for the book's input asset.
 *  @tparam TOut  Amount type for the book's output asset.
 *
 *  @see TOfferStreamBase, BookStep
 */
template <StepAmount TIn, StepAmount TOut>
class FlowOfferStream : public TOfferStreamBase<TIn, TOut>
{
private:
    boost::container::flat_set<uint256> permToRemove_;

public:
    using TOfferStreamBase<TIn, TOut>::TOfferStreamBase;

    /** Schedule an offer for permanent ledger removal.
     *
     *  Inserts @p offerIndex into `permToRemove_`.  This override also
     *  supports permanent cleanup of self-crossed offers during
     *  offer-crossing transactions; see
     *  `BookOfferCrossingStep::limitSelfCrossQuality()` for the motivation.
     *
     *  @param offerIndex Ledger key of the offer to be permanently removed.
     */
    void
    permRmOffer(uint256 const& offerIndex) override;

    /** Return the set of offer indices scheduled for permanent ledger removal.
     *
     *  `BookStep` applies these removals after the strand completes, even if
     *  the strand itself was discarded.  The set is non-empty only when
     *  invalid offers (expired, missing, found-unfunded, or self-crossed) were
     *  encountered during iteration.
     */
    [[nodiscard]] boost::container::flat_set<uint256> const&
    permToRemove() const
    {
        return permToRemove_;
    }
};
}  // namespace xrpl
