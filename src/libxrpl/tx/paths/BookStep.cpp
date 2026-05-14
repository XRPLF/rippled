#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/PaymentSandbox.h>
#include <xrpl/ledger/Sandbox.h>
#include <xrpl/ledger/helpers/AMMHelpers.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/MPTokenHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Book.h>
#include <xrpl/protocol/Concepts.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/IOUAmount.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/MPTAmount.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Quality.h>
#include <xrpl/protocol/Rate.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/paths/AMMLiquidity.h>
#include <xrpl/tx/paths/AMMOffer.h>
#include <xrpl/tx/paths/BookTip.h>
#include <xrpl/tx/paths/OfferStream.h>
#include <xrpl/tx/paths/detail/EitherAmount.h>
#include <xrpl/tx/paths/detail/FlatSets.h>
#include <xrpl/tx/paths/detail/Steps.h>

#include <boost/container/flat_set.hpp>

#include <cstdint>
#include <memory>
#include <numeric>
#include <optional>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace xrpl {

/** CRTP base class for an order-book exchange step in the payment/offer-crossing engine.
 *
 *  A `BookStep` converts one asset to another by consuming offers from the
 *  Central Limit Order Book (CLOB) and/or an AMM pool whose in/out assets
 *  match `book_`.  It sits between endpoint steps and is responsible for
 *  price discovery and execution at the ledger level.
 *
 *  `TIn` and `TOut` encode the amount types (`XRPAmount`, `IOUAmount`,
 *  `MPTAmount`), eliminating virtual dispatch on the hot iteration path.
 *  `TDerived` supplies policy hooks for transfer-fee handling, self-cross
 *  logic, and quality-threshold enforcement; two concrete sub-classes exist:
 *  `BookPaymentStep` (regular payments) and `BookOfferCrossingStep`.
 *
 *  All CRTP dispatch uses `static_cast<TDerived const*>(this)->method()`.
 *
 *  @tparam TIn      Amount type flowing into the book (taker-pays side).
 *  @tparam TOut     Amount type flowing out of the book (taker-gets side).
 *  @tparam TDerived Concrete sub-class providing payment/crossing policy.
 */
template <class TIn, class TOut, class TDerived>
class BookStep : public StepImp<TIn, TOut, BookStep<TIn, TOut, TDerived>>
{
protected:
    /** Discriminates whether the best available offer comes from the AMM or CLOB. */
    enum class OfferType { Amm, Clob };

    /** Maximum number of CLOB offers (funded or not) consumed per pass before
     *  the step is marked inactive to prevent DoS via a dense order book.
     */
    static constexpr uint32_t kMAX_OFFERS_TO_CONSUME{1000};
    Book book_;
    AccountID strandSrc_;
    AccountID strandDst_;
    /** Pointer to the immediately preceding step; used to determine debt
     *  direction and to compute transfer rates for offer crossing.
     *  Null when this is the first step in the strand.
     */
    Step const* const prevStep_ = nullptr;
    bool const ownerPaysTransferFee_;
    /** True once `kMAX_OFFERS_TO_CONSUME` offers have been visited in a single
     *  pass; signals the strand solver to abandon this strand rather than loop.
     */
    bool inactive_ = false;
    /** Number of offers consumed or partially consumed the last time
        the step ran, including expired and unfunded offers.

        N.B. This is not the total number offers consumed by this step for the
        entire payment, it is only the number the last time it ran. Offers may
        be partially consumed multiple times during a payment.
    */
    std::uint32_t offersUsed_ = 0;
    /** AMM liquidity for `book_`, if an AMM with nonzero LP-token balance
     *  exists for this asset pair.  When set, each offer-iteration pass
     *  checks whether the AMM offers better quality than the CLOB tip.
     */
    std::optional<AMMLiquidity<TIn, TOut>> ammLiquidity_;
    beast::Journal const j_;
    Asset const strandDeliver_;

    /** Holds the (in, out) amounts produced by the most recent `revImp` or
     *  `fwdImp` call.  `fwdImp` asserts this is set before running, so
     *  reverse must always precede forward.
     */
    struct Cache
    {
        TIn in;
        TOut out;

        Cache(TIn const& in, TOut const& out) : in(in), out(out)
        {
        }
    };

    std::optional<Cache> cache_;

private:
    /** Construct a BookStep for the given strand context and asset pair.
     *
     *  If an AMM SLE exists for `(in, out)` and its LP-token balance is
     *  nonzero, `ammLiquidity_` is emplaced so subsequent passes can
     *  compare AMM quality against the CLOB tip.
     *
     *  @param ctx  Strand construction context (view, src/dst, flags, etc.).
     *  @param in   Asset on the taker-pays side of the book.
     *  @param out  Asset on the taker-gets side of the book.
     */
    BookStep(StrandContext const& ctx, Asset const& in, Asset const& out)
        : book_(in, out, ctx.domainID)
        , strandSrc_(ctx.strandSrc)
        , strandDst_(ctx.strandDst)
        , prevStep_(ctx.prevStep)
        , ownerPaysTransferFee_(ctx.ownerPaysTransferFee)
        , j_(ctx.j)
        , strandDeliver_(ctx.strandDeliver)
    {
        if (auto const ammSle = ctx.view.read(keylet::amm(in, out));
            ammSle && ammSle->getFieldAmount(sfLPTokenBalance) != beast::kZERO)
        {
            ammLiquidity_.emplace(
                ctx.view,
                (*ammSle)[sfAccount],
                getTradingFee(ctx.view, *ammSle, ctx.ammContext.account()),
                in,
                out,
                ctx.ammContext,
                ctx.j);
        }
    }

public:
    /** Returns the order book this step operates on. */
    [[nodiscard]] Book const&
    book() const
    {
        return book_;
    }

    /** Returns the cached input amount from the last pass, or `nullopt` if no
     *  pass has run yet.
     */
    [[nodiscard]] std::optional<EitherAmount>
    cachedIn() const override
    {
        if (!cache_)
            return std::nullopt;
        return EitherAmount(cache_->in);
    }

    /** Returns the cached output amount from the last pass, or `nullopt` if no
     *  pass has run yet.
     */
    [[nodiscard]] std::optional<EitherAmount>
    cachedOut() const override
    {
        if (!cache_)
            return std::nullopt;
        return EitherAmount(cache_->out);
    }

    /** Returns `Issues` when the offer owner pays the transfer fee (i.e.
     *  downstream steps receive the book-out asset without a trust-line
     *  redemption); `Redeems` otherwise.
     */
    [[nodiscard]] DebtDirection
    debtDirection(ReadView const& sb, StrandDirection dir) const override
    {
        return ownerPaysTransferFee_ ? DebtDirection::Issues : DebtDirection::Redeems;
    }

    /** Returns the book for this step, allowing callers to identify it as a
     *  book step (vs. a direct or endpoint step).
     */
    [[nodiscard]] std::optional<Book>
    bookStepBook() const override
    {
        return book_;
    }

    /** Returns a conservative upper bound on the quality deliverable by this
     *  step, taking transfer fees into account, plus the resulting debt
     *  direction.  Returns `nullopt` quality when the book and AMM are both
     *  empty.
     *
     *  @param v            Read-only view of the current ledger state.
     *  @param prevStepDir  Debt direction reported by the preceding step.
     */
    [[nodiscard]] std::pair<std::optional<Quality>, DebtDirection>
    qualityUpperBound(ReadView const& v, DebtDirection prevStepDir) const override;

    /** Returns the quality function for the best available offer (AMM or
     *  CLOB), adjusted for transfer fees.  AMM quality functions are
     *  non-constant (price moves with size); CLOB functions are constant.
     *
     *  @param v            Read-only view of the current ledger state.
     *  @param prevStepDir  Debt direction reported by the preceding step.
     */
    [[nodiscard]] std::pair<std::optional<QualityFunction>, DebtDirection>
    getQualityFunc(ReadView const& v, DebtDirection prevStepDir) const override;

    /** Returns the number of offers (funded, unfunded, or expired) touched
     *  during the most recent `revImp` or `fwdImp` call.
     */
    [[nodiscard]] std::uint32_t
    offersUsed() const override;

    /** Backward (reverse) simulation pass.
     *
     *  Iterates CLOB/AMM offers starting from the desired output `out`,
     *  accumulates consumed amounts into a cache, and returns the total
     *  `(in, out)` pair.  Offer IDs to remove are appended to `ofrsToRm`.
     *
     *  @param sb       Payment sandbox (mutable layered view).
     *  @param afView   Apply view used for the offer stream.
     *  @param ofrsToRm Accumulates keys of unfunded/bad offers to erase.
     *  @param out      Desired output amount this step should deliver.
     *  @return         Actual `(in, out)` amounts consumed/produced.
     */
    std::pair<TIn, TOut>
    revImp(
        PaymentSandbox& sb,
        ApplyView& afView,
        boost::container::flat_set<uint256>& ofrsToRm,
        TOut const& out);

    /** Forward pass.
     *
     *  Re-runs offer consumption from the actual available input `in`.
     *  Requires `cache_` to be set (i.e., `revImp` must have run first).
     *  A subtle normalization adjustment reconciles cases where IOU mantissa
     *  subtraction yields zero, making an offer appear fully consumed when
     *  it is not.
     *
     *  @param sb       Payment sandbox (mutable layered view).
     *  @param afView   Apply view used for the offer stream.
     *  @param ofrsToRm Accumulates keys of unfunded/bad offers to erase.
     *  @param in       Actual input amount available for this step.
     *  @return         Actual `(in, out)` amounts consumed/produced.
     */
    std::pair<TIn, TOut>
    fwdImp(
        PaymentSandbox& sb,
        ApplyView& afView,
        boost::container::flat_set<uint256>& ofrsToRm,
        TIn const& in);

    /** Validates the forward pass by re-running `fwdImp` and comparing the
     *  result against the cached reverse output via `checkNear`.  Returns
     *  `false` (rejecting the strand) if amounts diverge, which can happen
     *  when ledger state changes between pathfinding reverse and apply forward.
     *
     *  @param sb     Payment sandbox.
     *  @param afView Apply view.
     *  @param in     Input amount to validate.
     *  @return       `{true, out}` on success; `{false, zero}` on mismatch.
     */
    std::pair<bool, EitherAmount>
    validFwd(PaymentSandbox& sb, ApplyView& afView, EitherAmount const& in) override;

    /** Validates structural invariants for this book step before a strand is
     *  committed.  Checks: same-asset book, issuer existence, loop detection
     *  via `seenBookOuts`, NoRipple flag on the preceding trust line (IOU),
     *  and MPT tradability.
     *
     *  @param ctx  Strand construction context.
     *  @return     `tesSUCCESS` or an appropriate error code.
     */
    [[nodiscard]] TER
    check(StrandContext const& ctx) const;

    /** Returns true if too many offers were consumed on the last pass, causing
     *  the strand solver to abandon this strand.
     */
    [[nodiscard]] bool
    inactive() const override
    {
        return inactive_;
    }

protected:
    /** Formats book issuers and currencies into a human-readable string for
     *  logging.  Called by `logString()` in each derived class.
     *
     *  @param name  Class name prefix (e.g. `"BookPaymentStep"`).
     *  @return      Multi-line diagnostic string.
     */
    std::string
    logStringImpl(char const* name) const
    {
        std::ostringstream ostr;
        ostr << name << ": "
             << "\ninIss: " << book_.in.getIssuer() << "\noutIss: " << book_.out.getIssuer()
             << "\ninCur: " << to_string(book_.in) << "\noutCur: " << to_string(book_.out);
        return ostr.str();
    }

    /** Returns the transfer rate for `asset` when sending to `dstAccount`.
     *  Returns parity (`kPARITY_RATE`) for XRP or when the issuer is the
     *  destination (self-transfer).
     *
     *  @param view        Read-only ledger view.
     *  @param asset       Asset whose issuer's transfer rate is queried.
     *  @param dstAccount  Destination account for this hop.
     */
    [[nodiscard]] Rate
    rate(ReadView const& view, Asset const& asset, AccountID const& dstAccount) const;

private:
    friend bool
    operator==(BookStep const& lhs, BookStep const& rhs)
    {
        return lhs.book_ == rhs.book_;
    }

    friend bool
    operator!=(BookStep const& lhs, BookStep const& rhs)
    {
        return !(lhs == rhs);
    }

    [[nodiscard]] bool
    equal(Step const& rhs) const override;

    /** Iterates offers at the best available quality, invoking `callback` for
     *  each.  Skips (and schedules removal of) unfunded, expired, and
     *  authorization-failing offers.  Stops when `callback` returns false or
     *  when `kMAX_OFFERS_TO_CONSUME` offers have been visited, in which case
     *  `inactive_` is set.  Also tries the AMM once per iteration when its
     *  quality exceeds the CLOB tip.
     *
     *  @param sb              Mutable payment sandbox.
     *  @param afView          Apply view for the offer stream.
     *  @param prevStepDebtDir Debt direction of the preceding step.
     *  @param callback        Callable receiving `(offer, ofrAmt, stpAmt,
     *                             ownerGives, trIn, trOut)`.
     *  @return                `{offersToRemove, offersConsumed}`.
     */
    template <class Callback>
    std::pair<boost::container::flat_set<uint256>, std::uint32_t>
    forEachOffer(
        PaymentSandbox& sb,
        ApplyView& afView,
        DebtDirection prevStepDebtDir,
        Callback& callback) const;

    /** Executes the fund transfer for one CLOB or AMM offer: sends `ofrAmt.in`
     *  from the book-in issuer to the offer owner, then sends `ownerGives`
     *  from the offer owner to the book-out issuer, and marks the offer as
     *  consumed.  Throws `FlowException` on any transfer error or if the AMM
     *  pool product invariant is violated.
     *
     *  @param sb         Mutable payment sandbox.
     *  @param offer      The offer being consumed (CLOB `TOffer` or `AMMOffer`).
     *  @param ofrAmt     Raw offer amounts (before transfer fee).
     *  @param stepAmt    Step amounts (after transfer fee applied to `in`).
     *  @param ownerGives Amount the offer owner actually sends (out minus fee).
     */
    template <template <typename, typename> typename Offer>
    void
    consumeOffer(
        PaymentSandbox& sb,
        Offer<TIn, TOut>& offer,
        TAmounts<TIn, TOut> const& ofrAmt,
        TAmounts<TIn, TOut> const& stepAmt,
        TOut const& ownerGives) const;

    /** Returns an AMM offer for this book, or `nullopt` if no AMM liquidity
     *  is available or if the CLOB tip has better quality than the AMM can
     *  match.
     *
     *  @param view         Read-only view.
     *  @param clobQuality  Quality of the current CLOB tip, if any.
     */
    std::optional<AMMOffer<TIn, TOut>>
    getAMMOffer(ReadView const& view, std::optional<Quality> const& clobQuality) const;

    /** Returns the best offer at the tip of the book: either a CLOB `Quality`
     *  or a fully materialised `AMMOffer`, whichever has better quality.
     *  Returns `nullopt` when both CLOB and AMM are empty.
     *
     *  @param view  Read-only ledger view.
     */
    std::optional<std::variant<Quality, AMMOffer<TIn, TOut>>>
    tip(ReadView const& view) const;

    /** Returns `{tipQuality, offerType}` for the best available offer, or
     *  `nullopt` when the book is empty.
     *
     *  @param view  Read-only ledger view.
     */
    std::optional<std::pair<Quality, OfferType>>
    tipOfferQuality(ReadView const& view) const;

    /** Returns the quality function for the best available offer, or `nullopt`
     *  when the book is empty.  AMM quality functions are non-constant (price
     *  varies with offer size); CLOB functions are constant.
     *
     *  @param view  Read-only ledger view.
     */
    [[nodiscard]] std::optional<QualityFunction>
    tipOfferQualityF(ReadView const& view) const;

    /** Returns true if both sides of the book can currently be traded and
     *  transferred for the given offer owner.  Enforces MPT-specific DEX
     *  rules: `CanTransfer` flag, frozen/locked token state, and whether the
     *  previous step is a `BookStep` or `MPTEndpointStep`.
     *
     *  @param view   Read-only ledger view.
     *  @param owner  Account ID of the offer owner being evaluated.
     */
    [[nodiscard]] bool
    checkMPTDEX(ReadView const& view, AccountID const& owner) const;

    friend TDerived;
};

//------------------------------------------------------------------------------

// Flow is used in two different circumstances for transferring funds:
//  o Payments, and
//  o Offer crossing.
// The rules for handling funds in these two cases are almost, but not
// quite, the same.

/** Payment-mode policy for `BookStep`.
 *
 *  Used when processing a regular payment (not offer crossing).  Compared to
 *  `BookOfferCrossingStep`:
 *  - No quality threshold: all offers in the book are eligible.
 *  - No self-cross deletion: payments may traverse the sender's own offers.
 *  - Transfer fees are always charged by the book owner's rate; the payment
 *    sender never receives a fee waiver because the sender and offer owner
 *    are different roles.
 *
 *  @tparam TIn   Amount type on the taker-pays side.
 *  @tparam TOut  Amount type on the taker-gets side.
 */
template <class TIn, class TOut>
class BookPaymentStep : public BookStep<TIn, TOut, BookPaymentStep<TIn, TOut>>
{
public:
    explicit BookPaymentStep() = default;

    BookPaymentStep(StrandContext const& ctx, Asset const& in, Asset const& out)
        : BookStep<TIn, TOut, BookPaymentStep<TIn, TOut>>(ctx, in, out)
    {
    }

    using BookStep<TIn, TOut, BookPaymentStep<TIn, TOut>>::qualityUpperBound;
    using typename BookStep<TIn, TOut, BookPaymentStep<TIn, TOut>>::OfferType;

    /** No-op for payments: self-crossing is not limited; always returns false. */
    template <template <typename, typename> typename Offer>
    bool
    limitSelfCrossQuality(
        AccountID const&,
        AccountID const&,
        Offer<TIn, TOut> const& offer,
        std::optional<Quality>&,
        FlowOfferStream<TIn, TOut>&,
        bool) const
    {
        return false;
    }

    /** Payments accept offers at any quality; always returns true. */
    [[nodiscard]] bool
    checkQualityThreshold(Quality const& quality) const
    {
        return true;
    }

    /** Returns `lobQuality` unchanged.  Payments do not apply a strand-level
     *  limit quality to individual book steps.
     */
    [[nodiscard]] std::optional<Quality>
    qualityThreshold(Quality const& lobQuality) const
    {
        return lobQuality;
    }

    /** For payments the effective in-rate equals the global transfer rate `trIn`. */
    std::uint32_t
    getOfrInRate(Step const*, AccountID const&, std::uint32_t trIn) const
    {
        return trIn;
    }

    /** For payments the effective out-rate equals the global transfer rate `trOut`. */
    std::uint32_t
    getOfrOutRate(Step const*, AccountID const&, AccountID const&, std::uint32_t trOut) const
    {
        return trOut;
    }

    /** Adjusts `ofrQ` by composing it with the combined in/out transfer-fee
     *  quality factor.  The offer owner — not the payment sender — always pays
     *  the transfer fee (even when owner == issuer), unless `waiveFee` is set.
     *
     *  @param v            Read-only ledger view.
     *  @param ofrQ         Raw offer quality before fee adjustment.
     *  @param prevStepDir  Debt direction of the preceding step.
     *  @param waiveFee     Whether the out transfer fee is waived.
     *  @param offerType    Ignored for `BookPaymentStep`.
     *  @param rules        Current ledger rules (unused here; present for CRTP symmetry).
     */
    [[nodiscard]] Quality
    adjustQualityWithFees(
        ReadView const& v,
        Quality const& ofrQ,
        DebtDirection prevStepDir,
        WaiveTransferFee waiveFee,
        OfferType,
        Rules const&) const
    {
        auto const trIn =
            redeems(prevStepDir) ? this->rate(v, this->book_.in, this->strandDst_) : kPARITY_RATE;
        // Always charge the transfer fee, even if the owner is the issuer,
        // unless the fee is waived
        auto const trOut = (this->ownerPaysTransferFee_ && waiveFee == WaiveTransferFee::No)
            ? this->rate(v, this->book_.out, this->strandDst_)
            : kPARITY_RATE;

        Quality const q1{getRate(STAmount(trOut.value), STAmount(trIn.value))};
        return composedQuality(q1, ofrQ);
    }

    [[nodiscard]] std::string
    logString() const override
    {
        return this->logStringImpl("BookPaymentStep");
    }
};

/** Offer-crossing policy for `BookStep`.
 *
 *  Used when a new offer is placed that can immediately match existing book
 *  offers.  Key behavioral differences from `BookPaymentStep`:
 *  - Enforces `qualityThreshold_`: iteration stops once the CLOB tip falls
 *    below the crossing quality.
 *  - Implements self-cross deletion: when Alice's new offer would cross one
 *    of her own pre-existing offers at an eligible quality, the old offer is
 *    permanently removed from the ledger so the crossing can continue past it.
 *  - Waives the in-side transfer fee when the offer owner is the same account
 *    as the direct-step source (alice paying alice).
 *  - Waives the out-side transfer fee when the offer owner is the strand
 *    destination and the preceding step is a `BookStep`.
 *  - Under `fixAMMv1_1`, single-path AMM quality is adjusted for the in
 *    transfer rate when computing the quality upper bound.
 *
 *  @tparam TIn   Amount type on the taker-pays side.
 *  @tparam TOut  Amount type on the taker-gets side.
 */
template <class TIn, class TOut>
class BookOfferCrossingStep : public BookStep<TIn, TOut, BookOfferCrossingStep<TIn, TOut>>
{
    using BookStep<TIn, TOut, BookOfferCrossingStep<TIn, TOut>>::qualityUpperBound;
    using typename BookStep<TIn, TOut, BookOfferCrossingStep<TIn, TOut>>::OfferType;

private:
    /** Extracts the quality from `limitQuality`, throwing `tefINTERNAL` if
     *  absent.  Missing limit quality is a programming error — callers must
     *  supply it for offer crossing.
     */
    static Quality
    getQuality(std::optional<Quality> const& limitQuality)
    {
        XRPL_ASSERT(limitQuality, "xrpl::BookOfferCrossingStep::getQuality : nonzero quality");
        if (!limitQuality)
            Throw<FlowException>(tefINTERNAL, "Offer requires quality.");
        return *limitQuality;
    }

public:
    BookOfferCrossingStep(StrandContext const& ctx, Asset const& in, Asset const& out)
        : BookStep<TIn, TOut, BookOfferCrossingStep<TIn, TOut>>(ctx, in, out)
        , defaultPath_(ctx.isDefaultPath)
        , qualityThreshold_(getQuality(ctx.limitQuality))
    {
    }

    /** Handles the self-cross scenario for offer crossing.
     *
     *  When all of the following hold:
     *  - this is the default (non-autobridged) path,
     *  - the offer's quality meets the crossing threshold, and
     *  - the offer owner is the same account as both strand source and destination,
     *
     *  the old offer is permanently deleted from the book so that subsequent
     *  offers become accessible.  This is the only mechanism that unblocks
     *  crossing past a self-blocking offer.
     *
     *  @param strandSrc      Source account of the crossing strand.
     *  @param strandDst      Destination account of the crossing strand.
     *  @param offer          The offer at the current book tip.
     *  @param ofrQ           Current quality; reset to `nullopt` if no prior
     *                            offers have been attempted, allowing the next
     *                            offer's quality to set a new baseline.
     *  @param offers         Offer stream used to schedule permanent removal.
     *  @param offerAttempted True if at least one offer has already been tried.
     *  @return               True if the offer was self-crossed and should be
     *                            deleted; false otherwise.
     */
    template <template <typename, typename> typename Offer>
    bool
    limitSelfCrossQuality(
        AccountID const& strandSrc,
        AccountID const& strandDst,
        Offer<TIn, TOut> const& offer,
        std::optional<Quality>& ofrQ,
        FlowOfferStream<TIn, TOut>& offers,
        bool const offerAttempted) const
    {
        if (defaultPath_ && offer.quality() >= qualityThreshold_ && strandSrc == offer.owner() &&
            strandDst == offer.owner())
        {
            if (auto const key = offer.key())
                offers.permRmOffer(*key);

            if (!offerAttempted)
                ofrQ = std::nullopt;

            return true;
        }
        return false;
    }

    /** Returns true if `quality` meets or exceeds the crossing threshold.
     *  On non-default (autobridged) paths all qualities are accepted.
     */
    [[nodiscard]] bool
    checkQualityThreshold(Quality const& quality) const
    {
        return !defaultPath_ || quality >= qualityThreshold_;
    }

    /** Returns the quality to use when generating an AMM synthetic offer.
     *
     *  For single-path AMM scenarios where `qualityThreshold_` exceeds the
     *  CLOB tip quality, returns `nullopt` so the AMM is allowed to generate
     *  its maximum offer rather than being artificially limited to a quality
     *  it cannot reach.  Multi-path AMM follows the same logic as CLOB.
     *
     *  @param lobQuality  Current CLOB tip quality.
     */
    [[nodiscard]] std::optional<Quality>
    qualityThreshold(Quality const& lobQuality) const
    {
        if (this->ammLiquidity_ && !this->ammLiquidity_->multiPath() &&
            qualityThreshold_ > lobQuality)
            return std::nullopt;
        return lobQuality;
    }

    /** Returns parity rate when the offer owner is the direct-step source
     *  (alice paying alice), waiving the in-side transfer fee.  Otherwise
     *  returns `trIn`.
     *
     *  @param prevStep  Preceding step in the strand, or null.
     *  @param owner     Account ID of the offer owner.
     *  @param trIn      Global in-side transfer rate.
     */
    std::uint32_t
    getOfrInRate(Step const* prevStep, AccountID const& owner, std::uint32_t trIn) const
    {
        auto const srcAcct = (prevStep != nullptr) ? prevStep->directStepSrcAcct() : std::nullopt;
        return owner == srcAcct ? QUALITY_ONE : trIn;
    }

    /** Returns parity rate when the offer owner equals the strand destination
     *  and the preceding step is a `BookStep`, waiving the out-side transfer
     *  fee.  Otherwise returns `trOut`.
     *
     *  @param prevStep   Preceding step in the strand, or null.
     *  @param owner      Account ID of the offer owner.
     *  @param strandDst  Strand destination account.
     *  @param trOut      Global out-side transfer rate.
     */
    std::uint32_t
    getOfrOutRate(
        Step const* prevStep,
        AccountID const& owner,
        AccountID const& strandDst,
        std::uint32_t trOut) const
    {
        return (prevStep != nullptr) && prevStep->bookStepBook() && owner == strandDst
            ? QUALITY_ONE
            : trOut;
    }

    /** Adjusts `ofrQ` for transfer fees, keeping it as an upper bound on
     *  achievable quality.
     *
     *  For CLOB offers and multi-path AMM, returns `ofrQ` unchanged (crossing
     *  assumes no fee so the bound remains valid).  For single-path AMM under
     *  `fixAMMv1_1`, composes the in transfer rate into `ofrQ` because
     *  single-path AMM quality is not constant and the rate materially affects
     *  the usable offer.
     *
     *  @param v            Read-only ledger view.
     *  @param ofrQ         Raw offer quality.
     *  @param prevStepDir  Debt direction of the preceding step.
     *  @param waiveFee     Ignored for crossing; kept for CRTP symmetry.
     *  @param offerType    `Amm` or `Clob`.
     *  @param rules        Current ledger rules (gates `fixAMMv1_1`).
     */
    [[nodiscard]] Quality
    adjustQualityWithFees(
        ReadView const& v,
        Quality const& ofrQ,
        DebtDirection prevStepDir,
        WaiveTransferFee waiveFee,
        OfferType offerType,
        Rules const& rules) const
    {
        // Quality upper bound must stay an upper bound: when calculating it,
        // assume no fee is charged so the estimate is never too low.
        if (!rules.enabled(fixAMMv1_1))
        {
            return ofrQ;
        }
        if (offerType == OfferType::Clob ||
            (this->ammLiquidity_ && this->ammLiquidity_->multiPath()))
        {
            return ofrQ;
        }

        auto const trIn =
            redeems(prevStepDir) ? this->rate(v, this->book_.in, this->strandDst_) : kPARITY_RATE;
        // AMM doesn't pay the transfer fee on the out amount
        auto const trOut = kPARITY_RATE;

        Quality const q1{getRate(STAmount(trOut.value), STAmount(trIn.value))};
        return composedQuality(q1, ofrQ);
    }

    [[nodiscard]] std::string
    logString() const override
    {
        return this->logStringImpl("BookOfferCrossingStep");
    }

private:
    /** True when this step is on the default (non-autobridged) path, enabling
     *  both the quality threshold check and the self-cross deletion rule.
     */
    bool const defaultPath_;
    /** Minimum acceptable offer quality for this crossing.  Offers below this
     *  threshold are not consumed and iteration stops.
     */
    Quality const qualityThreshold_;
};

//------------------------------------------------------------------------------

template <class TIn, class TOut, class TDerived>
bool
BookStep<TIn, TOut, TDerived>::equal(Step const& rhs) const
{
    if (auto bs = dynamic_cast<BookStep<TIn, TOut, TDerived> const*>(&rhs))
        return book_ == bs->book_;
    return false;
}

template <class TIn, class TOut, class TDerived>
std::pair<std::optional<Quality>, DebtDirection>
BookStep<TIn, TOut, TDerived>::qualityUpperBound(ReadView const& v, DebtDirection prevStepDir) const
{
    auto const dir = this->debtDirection(v, StrandDirection::Forward);

    std::optional<std::pair<Quality, OfferType>> const res = tipOfferQuality(v);
    if (!res)
        return {std::nullopt, dir};

    auto const waiveFee = (std::get<OfferType>(*res) == OfferType::Amm) ? WaiveTransferFee::Yes
                                                                        : WaiveTransferFee::No;

    Quality const q = static_cast<TDerived const*>(this)->adjustQualityWithFees(
        v, std::get<Quality>(*res), prevStepDir, waiveFee, std::get<OfferType>(*res), v.rules());
    return {q, dir};
}

template <class TIn, class TOut, class TDerived>
std::pair<std::optional<QualityFunction>, DebtDirection>
BookStep<TIn, TOut, TDerived>::getQualityFunc(ReadView const& v, DebtDirection prevStepDir) const
{
    auto const dir = this->debtDirection(v, StrandDirection::Forward);

    std::optional<QualityFunction> const res = tipOfferQualityF(v);
    if (!res)
        return {std::nullopt, dir};

    // AMM
    if (!res->isConst())
    {
        auto static const kQ_ONE = Quality{STAmount::kU_RATE_ONE};
        auto const q = static_cast<TDerived const*>(this)->adjustQualityWithFees(
            v, kQ_ONE, prevStepDir, WaiveTransferFee::Yes, OfferType::Amm, v.rules());
        if (q == kQ_ONE)
            return {res, dir};
        QualityFunction qf{q, QualityFunction::CLOBLikeTag{}};
        qf.combine(*res);
        return {qf, dir};
    }

    // CLOB
    Quality const q = static_cast<TDerived const*>(this)->adjustQualityWithFees(
        v,
        *(res->quality()),  // NOLINT(bugprone-unchecked-optional-access) CLOB QualityFunction
                            // always has quality set
        prevStepDir,
        WaiveTransferFee::No,
        OfferType::Clob,
        v.rules());
    return {QualityFunction{q, QualityFunction::CLOBLikeTag{}}, dir};
}

template <class TIn, class TOut, class TDerived>
std::uint32_t
BookStep<TIn, TOut, TDerived>::offersUsed() const
{
    return offersUsed_;
}

/** Clamps `stpAmt` and `ofrAmt` to a maximum input of `limit`.
 *
 *  When `limit < stpAmt.in`, the step input is reduced to `limit`, the offer
 *  input is recomputed via `offer.limitIn()` (rounded **down** to prevent
 *  order-book blocking), and `ownerGives` / `stpAmt.out` are updated
 *  consistently.  Rounding down guarantees the residual offer in the ledger
 *  retains quality ≥ the book page's quality, preventing page-blocking.
 *
 *  @param offer           The offer being partially consumed.
 *  @param ofrAmt          Raw offer amounts; updated in-place.
 *  @param stpAmt          Step amounts (transfer-fee-adjusted); updated in-place.
 *  @param ownerGives      Out amount paid by the offer owner; updated in-place.
 *  @param transferRateIn  In-side transfer rate multiplier.
 *  @param transferRateOut Out-side transfer rate multiplier.
 *  @param limit           Maximum step input to allow.
 */
template <class TIn, class TOut, class Offer>
static void
limitStepIn(
    Offer const& offer,
    TAmounts<TIn, TOut>& ofrAmt,
    TAmounts<TIn, TOut>& stpAmt,
    TOut& ownerGives,
    std::uint32_t transferRateIn,
    std::uint32_t transferRateOut,
    TIn const& limit)
{
    if (limit < stpAmt.in)
    {
        stpAmt.in = limit;
        auto const inLmt = mulRatio(stpAmt.in, QUALITY_ONE, transferRateIn, /*roundUp*/ false);
        // Rounding down the ceil_in() result prevents order-book blocking:
        // the residual offer keeps quality ≥ the containing page's quality.
        ofrAmt = offer.limitIn(ofrAmt, inLmt, /* roundUp */ false);
        stpAmt.out = ofrAmt.out;
        ownerGives = mulRatio(ofrAmt.out, transferRateOut, QUALITY_ONE, /*roundUp*/ false);
    }
}

/** Clamps `stpAmt` and `ofrAmt` to a maximum output of `limit`.
 *
 *  When `limit < stpAmt.out`, the step output is reduced to `limit`,
 *  `ownerGives` is recomputed, the offer output is re-derived via
 *  `offer.limitOut()` (rounded **up** so the offer owner doesn't under-pay),
 *  and `stpAmt.in` is updated accordingly.
 *
 *  @param offer           The offer being partially consumed.
 *  @param ofrAmt          Raw offer amounts; updated in-place.
 *  @param stpAmt          Step amounts (transfer-fee-adjusted); updated in-place.
 *  @param ownerGives      Out amount paid by the offer owner; updated in-place.
 *  @param transferRateIn  In-side transfer rate multiplier.
 *  @param transferRateOut Out-side transfer rate multiplier.
 *  @param limit           Maximum step output to allow.
 */
template <class TIn, class TOut, class Offer>
static void
limitStepOut(
    Offer const& offer,
    TAmounts<TIn, TOut>& ofrAmt,
    TAmounts<TIn, TOut>& stpAmt,
    TOut& ownerGives,
    std::uint32_t transferRateIn,
    std::uint32_t transferRateOut,
    TOut const& limit)
{
    if (limit < stpAmt.out)
    {
        stpAmt.out = limit;
        ownerGives = mulRatio(stpAmt.out, transferRateOut, QUALITY_ONE, /*roundUp*/ false);
        ofrAmt = offer.limitOut(
            ofrAmt,
            stpAmt.out,
            /*roundUp*/ true);
        stpAmt.in = mulRatio(ofrAmt.in, transferRateIn, QUALITY_ONE, /*roundUp*/ true);
    }
}

template <class TIn, class TOut, class TDerived>
template <class Callback>
std::pair<boost::container::flat_set<uint256>, std::uint32_t>
BookStep<TIn, TOut, TDerived>::forEachOffer(
    PaymentSandbox& sb,
    ApplyView& afView,
    DebtDirection prevStepDir,
    Callback& callback) const
{
    std::uint32_t const trIn =
        redeems(prevStepDir) ? rate(sb, book_.in, this->strandDst_).value : QUALITY_ONE;
    std::uint32_t const trOut =
        ownerPaysTransferFee_ ? rate(sb, book_.out, this->strandDst_).value : QUALITY_ONE;

    typename FlowOfferStream<TIn, TOut>::StepCounter counter(kMAX_OFFERS_TO_CONSUME, j_);

    FlowOfferStream<TIn, TOut> offers(sb, afView, book_, sb.parentCloseTime(), counter, j_);

    bool offerAttempted = false;
    std::optional<Quality> ofrQ;
    auto execOffer = [&](auto& offer) {
        if (!ofrQ)
        {
            ofrQ = offer.quality();
        }
        else if (*ofrQ != offer.quality())
        {
            return false;
        }

        if (static_cast<TDerived const*>(this)->limitSelfCrossQuality(
                strandSrc_, strandDst_, offer, ofrQ, offers, offerAttempted))
            return true;

        Asset const& assetIn = offer.assetIn();
        bool const isAssetInMPT = assetIn.holds<MPTIssue>();
        auto const& owner = offer.owner();

        if (isAssetInMPT)
        {
            // Create MPToken for the offer's owner if it does not yet exist.
            // No reserve check is needed: if the offer is consumed the owner
            // count stays the same; if it is removed it decreases.
            if (auto const err = checkCreateMPT(sb, assetIn.get<MPTIssue>(), owner, j_);
                !isTesSuccess(err))
            {
                return true;
            }
        }

        // Use sb (not afView) for auth checks once featureMPTokensV2 is
        // active; the distinction doesn't affect auth semantics, but using
        // sb ensures consistency with the rest of the execution context.
        auto& applyView = sb.rules().enabled(featureMPTokensV2) ? sb : afView;
        if (!isTesSuccess(requireAuth(applyView, assetIn, owner)) || !checkMPTDEX(sb, owner))
        {
            // Offer owner is not authorized or the MPT cannot be traded.
            // Remove this offer even if no crossing occurs.
            if (auto const key = offer.key())
                offers.permRmOffer(*key);
            if (!offerAttempted)
                ofrQ = std::nullopt;
            return true;
        }

        if (!static_cast<TDerived const*>(this)->checkQualityThreshold(offer.quality()))
            return false;

        auto const [ofrInRate, ofrOutRate] = offer.adjustRates(
            static_cast<TDerived const*>(this)->getOfrInRate(prevStep_, owner, trIn),
            static_cast<TDerived const*>(this)->getOfrOutRate(prevStep_, owner, strandDst_, trOut));

        auto ofrAmt = offer.amount();
        TAmounts stpAmt{mulRatio(ofrAmt.in, ofrInRate, QUALITY_ONE, /*roundUp*/ true), ofrAmt.out};

        auto ownerGives = mulRatio(ofrAmt.out, ofrOutRate, QUALITY_ONE, /*roundUp*/ false);

        auto const funds = offer.isFunded()
            ? ownerGives  // Offer owner is the issuer: effectively unlimited funds
            : offers.ownerFunds();

        // Only CLOB offers can be underfunded; AMM offers are always funded.
        if (funds < ownerGives)
        {
            ownerGives = funds;
            stpAmt.out = mulRatio(ownerGives, QUALITY_ONE, ofrOutRate, /*roundUp*/ false);

            // It turns out we can prevent order book blocking by (strictly)
            // rounding down the ceil_out() result.  This adjustment changes
            // transaction outcomes, so it must be made under an amendment.
            ofrAmt = offer.limitOut(ofrAmt, stpAmt.out, /*roundUp*/ false);

            stpAmt.in = mulRatio(ofrAmt.in, ofrInRate, QUALITY_ONE, /*roundUp*/ true);
        }

        // When this is the first step and the in-asset is an MPT, cap the
        // offer input to the issuer's remaining issuable balance so
        // OutstandingAmount cannot overflow.
        auto const& issuer = assetIn.getIssuer();
        if (isAssetInMPT && !prevStep_ && offer.owner() != issuer)
        {
            // Funds available to issue
            auto const available = toAmount<TIn>(accountFunds(
                sb,
                issuer,
                assetIn,  // STAmount{0}, but the default is not used
                FreezeHandling::IgnoreFreeze,
                AuthHandling::IgnoreAuth,
                j_));
            if (stpAmt.in > available)
            {
                limitStepIn(offer, ofrAmt, stpAmt, ownerGives, ofrInRate, ofrOutRate, available);
            }
        }

        offerAttempted = true;
        return callback(offer, ofrAmt, stpAmt, ownerGives, ofrInRate, ofrOutRate);
    };

    // AMM may only be consumed once per forEachOffer invocation.
    auto tryAMM = [&](std::optional<Quality> const& lobQuality) -> bool {
        if (book_.domain)
            return true;  // AMM does not yet support domain-partitioned books

        auto const qualityThreshold = [&]() -> std::optional<Quality> {
            if (sb.rules().enabled(fixAMMv1_1) && lobQuality)
                return static_cast<TDerived const*>(this)->qualityThreshold(*lobQuality);
            return lobQuality;
        }();
        auto ammOffer = getAMMOffer(sb, qualityThreshold);
        return !ammOffer || execOffer(*ammOffer);
    };

    if (offers.step())
    {
        if (tryAMM(offers.tip().quality()))
        {
            do
            {
                if (!execOffer(offers.tip()))
                    break;
            } while (offers.step());
        }
    }
    else
    {
        // No CLOB offers: try AMM as the sole liquidity source.
        tryAMM(std::nullopt);
    }

    return {offers.permToRemove(), counter.count()};
}

template <class TIn, class TOut, class TDerived>
template <template <typename, typename> typename Offer>
void
BookStep<TIn, TOut, TDerived>::consumeOffer(
    PaymentSandbox& sb,
    Offer<TIn, TOut>& offer,
    TAmounts<TIn, TOut> const& ofrAmt,
    TAmounts<TIn, TOut> const& stepAmt,
    TOut const& ownerGives) const
{
    if (!offer.checkInvariant(ofrAmt, j_))
    {
        // Written as separate if-statements so logging fires even when
        // the amendment is not yet active.
        if (sb.rules().enabled(fixAMMOverflowOffer))
        {
            Throw<FlowException>(tecINVARIANT_FAILED, "AMM pool product invariant failed.");
        }
    }

    // Send ofrAmt.in from the book-in issuer to the offer owner.
    // The excess over stepAmt.in is the in-side transfer fee.
    {
        auto const dr = offer.send(
            sb, book_.in.getIssuer(), offer.owner(), toSTAmount(ofrAmt.in, book_.in), j_);
        if (!isTesSuccess(dr))
            Throw<FlowException>(dr);
    }

    // Send ownerGives from the offer owner to the book-out issuer.
    // The shortfall versus ofrAmt.out is the out-side transfer fee.
    {
        auto const& issuer = book_.out.getIssuer();
        auto const cr =
            offer.send(sb, offer.owner(), issuer, toSTAmount(ownerGives, book_.out), j_);
        if (!isTesSuccess(cr))
            Throw<FlowException>(cr);
        if constexpr (std::is_same_v<TOut, MPTAmount>)
        {
            if (offer.owner() == issuer)
                issuerSelfDebitHookMPT(sb, book_.out.get<MPTIssue>(), ofrAmt.out.value());
        }
    }

    offer.consume(sb, ofrAmt);
}

template <class TIn, class TOut, class TDerived>
std::optional<AMMOffer<TIn, TOut>>
BookStep<TIn, TOut, TDerived>::getAMMOffer(
    ReadView const& view,
    std::optional<Quality> const& clobQuality) const
{
    if (ammLiquidity_)
        return ammLiquidity_->getOffer(view, clobQuality);
    return std::nullopt;
}

template <class TIn, class TOut, class TDerived>
std::optional<std::variant<Quality, AMMOffer<TIn, TOut>>>
BookStep<TIn, TOut, TDerived>::tip(ReadView const& view) const
{
    Sandbox sb(&view, TapNone);
    BookTip bt(sb, book_);
    auto const lobQuality = bt.step(j_) ? std::optional<Quality>(bt.quality()) : std::nullopt;

    // Under fixAMMv1_1, pass the (possibly adjusted) CLOB quality as a
    // threshold when generating the AMM offer, preventing the engine from
    // entering multiple partial-cross iterations against a single LOB offer.
    auto const qualityThreshold = [&]() -> std::optional<Quality> {
        if (view.rules().enabled(fixAMMv1_1) && lobQuality)
            return static_cast<TDerived const*>(this)->qualityThreshold(*lobQuality);
        return std::nullopt;
    }();

    if (auto const ammOffer = getAMMOffer(view, qualityThreshold);
        ammOffer && ((lobQuality && ammOffer->quality() > lobQuality) || !lobQuality))
        return ammOffer;
    return lobQuality;
}

template <class TIn, class TOut, class TDerived>
auto
BookStep<TIn, TOut, TDerived>::tipOfferQuality(ReadView const& view) const
    -> std::optional<std::pair<Quality, OfferType>>
{
    auto const res = tip(view);
    if (!res)
    {
        return std::nullopt;
    }
    if (auto const q = std::get_if<Quality>(&(*res)))
    {
        return std::make_pair(*q, OfferType::Clob);
    }

    return std::make_pair(std::get<AMMOffer<TIn, TOut>>(*res).quality(), OfferType::Amm);
}

template <class TIn, class TOut, class TDerived>
std::optional<QualityFunction>
BookStep<TIn, TOut, TDerived>::tipOfferQualityF(ReadView const& view) const
{
    auto const res = tip(view);
    if (!res)
    {
        return std::nullopt;
    }
    if (auto const q = std::get_if<Quality>(&(*res)))
    {
        return QualityFunction{*q, QualityFunction::CLOBLikeTag{}};
    }

    return std::get<AMMOffer<TIn, TOut>>(*res).getQualityFunc();
}

/** Sums a sorted flat-multiset of typed amounts without floating-point loss.
 *
 *  Amounts are accumulated individually (not converted to a common float
 *  representation first) to avoid the rounding error that would arise from
 *  summing many small IOU values via a running total in the wrong precision.
 *  Returns zero of the element type when `col` is empty.
 *
 *  @param col  A non-empty or empty flat-multiset of `TIn` or `TOut` values.
 *  @return     The sum, or a zero-valued element if `col` is empty.
 */
template <class TCollection>
static auto
sum(TCollection const& col)
{
    using TResult = std::decay_t<decltype(*col.begin())>;
    if (col.empty())
        return TResult{beast::kZERO};
    return std::accumulate(col.begin() + 1, col.end(), *col.begin());
};

template <class TIn, class TOut, class TDerived>
std::pair<TIn, TOut>
BookStep<TIn, TOut, TDerived>::revImp(
    PaymentSandbox& sb,
    ApplyView& afView,
    boost::container::flat_set<uint256>& ofrsToRm,
    TOut const& out)
{
    cache_.reset();

    TAmounts<TIn, TOut> result(beast::kZERO, beast::kZERO);

    auto remainingOut = out;

    boost::container::flat_multiset<TIn> savedIns;
    savedIns.reserve(64);
    boost::container::flat_multiset<TOut> savedOuts;
    savedOuts.reserve(64);

    // Callback for forEachOffer: consumes one offer, accumulates amounts.
    // Returns true to keep consuming, false when output is satisfied.
    auto eachOffer = [&](auto& offer,
                         TAmounts<TIn, TOut> const& ofrAmt,
                         TAmounts<TIn, TOut> const& stpAmt,
                         TOut const& ownerGives,
                         std::uint32_t transferRateIn,
                         std::uint32_t transferRateOut) mutable -> bool {
        if (remainingOut <= beast::kZERO)
            return false;

        if (stpAmt.out <= remainingOut)
        {
            savedIns.insert(stpAmt.in);
            savedOuts.insert(stpAmt.out);
            result = TAmounts<TIn, TOut>(sum(savedIns), sum(savedOuts));
            remainingOut = out - result.out;
            this->consumeOffer(sb, offer, ofrAmt, stpAmt, ownerGives);
            // Always consume: even when remainingOut becomes zero, the offer
            // must be fully consumed before iteration stops.
            return true;
        }

        auto ofrAdjAmt = ofrAmt;
        auto stpAdjAmt = stpAmt;
        auto ownerGivesAdj = ownerGives;
        limitStepOut(
            offer,
            ofrAdjAmt,
            stpAdjAmt,
            ownerGivesAdj,
            transferRateIn,
            transferRateOut,
            remainingOut);
        remainingOut = beast::kZERO;
        savedIns.insert(stpAdjAmt.in);
        savedOuts.insert(remainingOut);
        result.in = sum(savedIns);
        result.out = out;
        this->consumeOffer(sb, offer, ofrAdjAmt, stpAdjAmt, ownerGivesAdj);

        // When stpAmt.out > remainingOut we consumed only part of the offer.
        // However, if two IOU mantissas differ by fewer than ten, their
        // difference is zero, making the offer look fully consumed when it
        // is not.  Check explicitly.
        return offer.fullyConsumed();
    };

    {
        auto const prevStepDebtDir = [&] {
            if (prevStep_)
                return prevStep_->debtDirection(sb, StrandDirection::Reverse);
            return DebtDirection::Issues;
        }();
        auto const r = forEachOffer(sb, afView, prevStepDebtDir, eachOffer);
        boost::container::flat_set<uint256> const toRm = std::move(std::get<0>(r));
        std::uint32_t const offersConsumed = std::get<1>(r);
        offersUsed_ = offersConsumed;
        setUnion(ofrsToRm, toRm);

        if (offersConsumed >= kMAX_OFFERS_TO_CONSUME)
        {
            inactive_ = true;
        }
    }

    switch (remainingOut.signum())
    {
        case -1: {
            // LCOV_EXCL_START
            JLOG(j_.error()) << "BookStep remainingOut < 0 " << to_string(remainingOut);
            UNREACHABLE("xrpl::BookStep::revImp : remaining less than zero");
            cache_.emplace(beast::kZERO, beast::kZERO);
            return {beast::kZERO, beast::kZERO};
            // LCOV_EXCL_STOP
        }
        case 0: {
            // Normalization can produce remainingOut == 0 while result.out
            // still differs from `out`.  Force equality for caching.
            result.out = out;
        }
    }

    cache_.emplace(result.in, result.out);
    return {result.in, result.out};
}

template <class TIn, class TOut, class TDerived>
std::pair<TIn, TOut>
BookStep<TIn, TOut, TDerived>::fwdImp(
    PaymentSandbox& sb,
    ApplyView& afView,
    boost::container::flat_set<uint256>& ofrsToRm,
    TIn const& in)
{
    XRPL_ASSERT(cache_, "xrpl::BookStep::fwdImp : cache is set");

    TAmounts<TIn, TOut> result(beast::kZERO, beast::kZERO);

    auto remainingIn = in;

    boost::container::flat_multiset<TIn> savedIns;
    savedIns.reserve(64);
    boost::container::flat_multiset<TOut> savedOuts;
    savedOuts.reserve(64);

    // Callback for forEachOffer: consumes one offer, accumulates amounts.
    auto eachOffer = [&](auto& offer,
                         TAmounts<TIn, TOut> const& ofrAmt,
                         TAmounts<TIn, TOut> const& stpAmt,
                         TOut const& ownerGives,
                         std::uint32_t transferRateIn,
                         std::uint32_t transferRateOut) mutable -> bool {
        XRPL_ASSERT(cache_, "xrpl::BookStep::fwdImp::eachOffer : cache is set");

        if (remainingIn <= beast::kZERO)
            return false;

        bool processMore = true;
        auto ofrAdjAmt = ofrAmt;
        auto stpAdjAmt = stpAmt;
        auto ownerGivesAdj = ownerGives;

        typename boost::container::flat_multiset<TOut>::const_iterator lastOut;
        if (stpAmt.in <= remainingIn)
        {
            savedIns.insert(stpAmt.in);
            lastOut = savedOuts.insert(stpAmt.out);
            result = TAmounts<TIn, TOut>(sum(savedIns), sum(savedOuts));
            // Consume even if stpAmt.in == remainingIn; the offer may be
            // only partially consumed and must be updated in the ledger.
            processMore = true;
        }
        else
        {
            limitStepIn(
                offer,
                ofrAdjAmt,
                stpAdjAmt,
                ownerGivesAdj,
                transferRateIn,
                transferRateOut,
                remainingIn);
            savedIns.insert(remainingIn);
            lastOut = savedOuts.insert(stpAdjAmt.out);
            result.out = sum(savedOuts);
            result.in = in;

            processMore = false;
        }

        if (result.out > cache_->out && result.in <= cache_->in)
        {
            // Forward pass produced more output than the reverse pass for
            // the same (or less) input — an IOU mantissa normalization
            // artifact.  Recompute using the reverse-cached output as the
            // target and accept the result only when the required input
            // exactly matches the available input.
            auto const lastOutAmt = *lastOut;
            savedOuts.erase(lastOut);
            auto const remainingOut = cache_->out - sum(savedOuts);
            auto ofrAdjAmtRev = ofrAmt;
            auto stpAdjAmtRev = stpAmt;
            auto ownerGivesAdjRev = ownerGives;
            limitStepOut(
                offer,
                ofrAdjAmtRev,
                stpAdjAmtRev,
                ownerGivesAdjRev,
                transferRateIn,
                transferRateOut,
                remainingOut);

            if (stpAdjAmtRev.in == remainingIn)
            {
                result.in = in;
                result.out = cache_->out;

                savedIns.clear();
                savedIns.insert(result.in);
                savedOuts.clear();
                savedOuts.insert(result.out);

                ofrAdjAmt = ofrAdjAmtRev;
                stpAdjAmt.in = remainingIn;
                stpAdjAmt.out = remainingOut;
                ownerGivesAdj = ownerGivesAdjRev;
            }
            else
            {
                // Input mismatch; restore the saved output and let
                // validFwd catch the divergence later.
                savedOuts.insert(lastOutAmt);
            }
        }

        remainingIn = in - result.in;
        this->consumeOffer(sb, offer, ofrAdjAmt, stpAdjAmt, ownerGivesAdj);

        // IOU mantissa subtraction can yield zero when amounts differ by
        // fewer than 10 units, falsely indicating the offer will remain
        // funded.  fullyConsumed() detects this edge case.
        return processMore || offer.fullyConsumed();
    };

    {
        auto const prevStepDebtDir = [&] {
            if (prevStep_)
                return prevStep_->debtDirection(sb, StrandDirection::Forward);
            return DebtDirection::Issues;
        }();
        auto const r = forEachOffer(sb, afView, prevStepDebtDir, eachOffer);
        boost::container::flat_set<uint256> const toRm = std::move(std::get<0>(r));
        std::uint32_t const offersConsumed = std::get<1>(r);
        offersUsed_ = offersConsumed;
        setUnion(ofrsToRm, toRm);

        if (offersConsumed >= kMAX_OFFERS_TO_CONSUME)
        {
            inactive_ = true;
        }
    }

    switch (remainingIn.signum())
    {
        case -1: {
            // LCOV_EXCL_START
            JLOG(j_.error()) << "BookStep remainingIn < 0 " << to_string(remainingIn);
            UNREACHABLE("xrpl::BookStep::fwdImp : remaining less than zero");
            cache_.emplace(beast::kZERO, beast::kZERO);
            return {beast::kZERO, beast::kZERO};
            // LCOV_EXCL_STOP
        }
        case 0: {
            // Normalization can produce remainingIn == 0 while result.in
            // still differs from `in`.  Force equality for caching.
            result.in = in;
        }
    }

    cache_.emplace(result.in, result.out);
    return {result.in, result.out};
}

template <class TIn, class TOut, class TDerived>
std::pair<bool, EitherAmount>
BookStep<TIn, TOut, TDerived>::validFwd(
    PaymentSandbox& sb,
    ApplyView& afView,
    EitherAmount const& in)
{
    if (!cache_)
    {
        JLOG(j_.trace()) << "Expected valid cache in validFwd";
        return {false, EitherAmount(TOut(beast::kZERO))};
    }

    auto const savCache = *cache_;

    try
    {
        boost::container::flat_set<uint256> dummy;
        fwdImp(sb, afView, dummy, get<TIn>(in));  // changes cache
    }
    catch (FlowException const&)
    {
        return {false, EitherAmount(TOut(beast::kZERO))};
    }

    // NOLINTBEGIN(bugprone-unchecked-optional-access) fwdImp sets cache_ on success
    if (!(checkNear(savCache.in, cache_->in) && checkNear(savCache.out, cache_->out)))
    {
        JLOG(j_.warn()) << "Strand re-execute check failed."
                        << " ExpectedIn: " << to_string(savCache.in)
                        << " CachedIn: " << to_string(cache_->in)
                        << " ExpectedOut: " << to_string(savCache.out)
                        << " CachedOut: " << to_string(cache_->out);
        return {false, EitherAmount(cache_->out)};
    }
    return {true, EitherAmount(cache_->out)};
    // NOLINTEND(bugprone-unchecked-optional-access)
}

template <class TIn, class TOut, class TDerived>
TER
BookStep<TIn, TOut, TDerived>::check(StrandContext const& ctx) const
{
    if (book_.in == book_.out)
    {
        JLOG(j_.debug()) << "BookStep: Book with same in and out issuer " << *this;
        return temBAD_PATH;
    }
    if (!isConsistent(book_.in) || !isConsistent(book_.out))
    {
        JLOG(j_.debug()) << "Book: currency is inconsistent with issuer." << *this;
        return temBAD_PATH;
    }

    // Two books with the same output asset would let offers in one step unfund
    // offers in another step — reject as a path loop.
    if (!ctx.seenBookOuts.insert(book_.out).second ||
        (ctx.seenDirectAssets[0].count(book_.out) != 0u))
    {
        JLOG(j_.debug()) << "BookStep: loop detected: " << *this;
        return temBAD_PATH_LOOP;
    }

    if (ctx.seenDirectAssets[1].count(book_.out) != 0u)
    {
        JLOG(j_.debug()) << "BookStep: loop detected: " << *this;
        return temBAD_PATH_LOOP;
    }

    auto issuerExists = [](ReadView const& view, Asset const& iss) -> bool {
        return isXRP(iss.getIssuer()) || view.exists(keylet::account(iss.getIssuer()));
    };

    if (!issuerExists(ctx.view, book_.in) || !issuerExists(ctx.view, book_.out))
    {
        JLOG(j_.debug()) << "BookStep: deleted issuer detected: " << *this;
        return tecNO_ISSUER;
    }

    if (ctx.prevStep != nullptr)
    {
        if (auto const prev = ctx.prevStep->directStepSrcAcct())
        {
            auto const& view = ctx.view;
            auto const& cur = book_.in.getIssuer();

            auto const err = book_.in.visit(
                [&](Issue const& issue) -> std::optional<TER> {
                    auto sle = view.read(keylet::line(*prev, cur, issue.currency));
                    if (!sle)
                        return terNO_LINE;
                    if (((*sle)[sfFlags] & ((cur > *prev) ? lsfHighNoRipple : lsfLowNoRipple)) !=
                        0u)
                        return terNO_RIPPLE;
                    return std::nullopt;
                },
                [&](MPTIssue const& issue) -> std::optional<TER> {
                    // Check if can trade on DEX.
                    if (auto const ter = canTrade(view, book_.in); !isTesSuccess(ter))
                        return ter;
                    if (auto const ter = canTrade(view, book_.out); !isTesSuccess(ter))
                        return ter;
                    return std::nullopt;
                });
            if (err)
                return *err;
        }
    }

    return tesSUCCESS;
}

template <class TIn, class TOut, class TDerived>
Rate
BookStep<TIn, TOut, TDerived>::rate(
    ReadView const& view,
    Asset const& asset,
    AccountID const& dstAccount) const
{
    auto const& issuer = asset.getIssuer();
    if (isXRP(issuer) || issuer == dstAccount)
        return kPARITY_RATE;
    return asset.visit(
        [&](Issue const&) { return transferRate(view, issuer); },
        [&](MPTIssue const& issue) { return transferRate(view, issue.getMptID()); });
};

template <class TIn, class TOut, class TDerived>
bool
BookStep<TIn, TOut, TDerived>::checkMPTDEX(ReadView const& view, AccountID const& owner) const
{
    if (!isTesSuccess(canTrade(view, book_.in)) || !isTesSuccess(canTrade(view, book_.out)))
        return false;

    if (book_.in.holds<MPTIssue>())
    {
        auto ret = [&]() {
            auto const& asset = book_.in;
            // Strand's source is an issuer
            if (!prevStep_)
                return true;
            // Offer's owner is an issuer
            if (asset.getIssuer() == owner)
                return true;
            // The previous step could be MPTEndpointStep with non issuer account or
            // BookStep. Fail both if in asset is locked. In the former case it is holder
            // to locked holder transfer. In the latter case it is not possible to tell if
            // it is issuer to holder or holder to holder transfer.
            if (isFrozen(view, owner, book_.in.get<MPTIssue>()))
                return false;
            // Previous step is BookStep. BookStep only sends if CanTransfer is
            // set and not locked or the offer is owned by an issuer
            if (prevStep_->bookStepBook())
                return true;
            // Previous step is MPTEndpointStep and offer's owner is not an
            // issuer
            return isTesSuccess(canTransfer(view, asset, owner, owner));
        }();
        if (!ret)
            return false;
    }

    if (book_.out.holds<MPTIssue>())
    {
        auto const& asset = book_.out;
        // Last step if the strand's destination is an issuer
        if (strandDeliver_ == asset && strandDst_ == asset.getIssuer())
            return true;
        // Offer's owner is an issuer
        if (asset.getIssuer() == owner)
            return true;

        // Next step is BookStep and offer's owner is not an issuer.
        return isTesSuccess(canTransfer(view, asset, owner, owner));
    }

    return true;
}

//------------------------------------------------------------------------------

namespace test {

/** Helper for unit tests: returns true if `step` is a `BookStep` over the
 *  given `book`.  Performs a `dynamic_cast` to the concrete step type.
 *
 *  @tparam TIn      In-amount type.
 *  @tparam TOut     Out-amount type.
 *  @tparam TDerived Concrete `BookStep` sub-class to cast to.
 *  @param step      Step to test.
 *  @param book      Expected book.
 */
template <class TIn, class TOut, class TDerived>
static bool
equalHelper(Step const& step, xrpl::Book const& book)
{
    if (auto bs = dynamic_cast<BookStep<TIn, TOut, TDerived> const*>(&step))
        return book == bs->book();
    return false;
}

/** Returns true if `step` is a `BookPaymentStep` over `book`.
 *
 *  Dispatches via `std::visit` over the book's in/out amount types to
 *  instantiate the correct template specialization at runtime.  Used
 *  exclusively by unit tests.
 *
 *  @param step  The step to inspect.
 *  @param book  The book to compare against.
 */
bool
bookStepEqual(Step const& step, xrpl::Book const& book)
{
    return std::visit(
        [&]<typename TIn, typename TOut>(TIn const&, TOut const&) {
            using TIn_ = typename TIn::amount_type;
            using TOut_ = typename TOut::amount_type;

            if constexpr (ValidTaker<TIn_, TOut_>)
            {
                return equalHelper<TIn_, TOut_, BookPaymentStep<TIn_, TOut_>>(step, book);
            }
            else
            {
                // LCOV_EXCL_START
                UNREACHABLE("xrpl::bookStepEqual : invalid book step");
                return false;
                // LCOV_EXCL_STOP
            }
        },
        book.in.getAmountType(),
        book.out.getAmountType());
}
}  // namespace test

//------------------------------------------------------------------------------

/** Constructs a `BookPaymentStep` or `BookOfferCrossingStep` for `(TIn, TOut)`,
 *  runs structural validation via `check()`, and returns the result.
 *
 *  @tparam TIn   In-amount type.
 *  @tparam TOut  Out-amount type.
 *  @param ctx    Strand construction context (determines payment vs. crossing).
 *  @param in     Taker-pays asset.
 *  @param out    Taker-gets asset.
 *  @return       `{tesSUCCESS, step}` on success; `{errorCode, nullptr}` on failure.
 */
template <class TIn, class TOut>
static std::pair<TER, std::unique_ptr<Step>>
makeBookStepHelper(StrandContext const& ctx, Asset const& in, Asset const& out)
{
    TER ter = tefINTERNAL;
    std::unique_ptr<Step> r;
    if (ctx.offerCrossing != OfferCrossing::No)
    {
        auto offerCrossingStep = std::make_unique<BookOfferCrossingStep<TIn, TOut>>(ctx, in, out);
        ter = offerCrossingStep->check(ctx);
        r = std::move(offerCrossingStep);
    }
    else
    {
        auto paymentStep = std::make_unique<BookPaymentStep<TIn, TOut>>(ctx, in, out);
        ter = paymentStep->check(ctx);
        r = std::move(paymentStep);
    }
    if (!isTesSuccess(ter))
        return {ter, nullptr};

    return {tesSUCCESS, std::move(r)};
}

/** Creates an IOU→IOU book step.
 *
 *  @param ctx  Strand context (payment or crossing mode, view, flags).
 *  @param in   Taker-pays IOU issue.
 *  @param out  Taker-gets IOU issue.
 *  @return     `{tesSUCCESS, step}` or `{error, nullptr}`.
 */
std::pair<TER, std::unique_ptr<Step>>
makeBookStepIi(StrandContext const& ctx, Issue const& in, Issue const& out)
{
    return makeBookStepHelper<IOUAmount, IOUAmount>(ctx, in, out);
}

/** Creates an IOU→XRP book step.
 *
 *  @param ctx  Strand context.
 *  @param in   Taker-pays IOU issue.
 *  @return     `{tesSUCCESS, step}` or `{error, nullptr}`.
 */
std::pair<TER, std::unique_ptr<Step>>
makeBookStepIx(StrandContext const& ctx, Issue const& in)
{
    return makeBookStepHelper<IOUAmount, XRPAmount>(ctx, in, xrpIssue());
}

/** Creates an XRP→IOU book step.
 *
 *  @param ctx  Strand context.
 *  @param out  Taker-gets IOU issue.
 *  @return     `{tesSUCCESS, step}` or `{error, nullptr}`.
 */
std::pair<TER, std::unique_ptr<Step>>
makeBookStepXi(StrandContext const& ctx, Issue const& out)
{
    return makeBookStepHelper<XRPAmount, IOUAmount>(ctx, xrpIssue(), out);
}

// --- MPT book step factory functions ---

/** Creates an MPT→MPT book step.
 *
 *  @param ctx  Strand context.
 *  @param in   Taker-pays MPT issue.
 *  @param out  Taker-gets MPT issue.
 *  @return     `{tesSUCCESS, step}` or `{error, nullptr}`.
 */
std::pair<TER, std::unique_ptr<Step>>
makeBookStepMm(StrandContext const& ctx, MPTIssue const& in, MPTIssue const& out)
{
    return makeBookStepHelper<MPTAmount, MPTAmount>(ctx, in, out);
}

/** Creates an MPT→IOU book step.
 *
 *  @param ctx  Strand context.
 *  @param in   Taker-pays MPT issue.
 *  @param out  Taker-gets IOU issue.
 *  @return     `{tesSUCCESS, step}` or `{error, nullptr}`.
 */
std::pair<TER, std::unique_ptr<Step>>
makeBookStepMi(StrandContext const& ctx, MPTIssue const& in, Issue const& out)
{
    return makeBookStepHelper<MPTAmount, IOUAmount>(ctx, in, out);
}

/** Creates an IOU→MPT book step.
 *
 *  @param ctx  Strand context.
 *  @param in   Taker-pays IOU issue.
 *  @param out  Taker-gets MPT issue.
 *  @return     `{tesSUCCESS, step}` or `{error, nullptr}`.
 */
std::pair<TER, std::unique_ptr<Step>>
makeBookStepIm(StrandContext const& ctx, Issue const& in, MPTIssue const& out)
{
    return makeBookStepHelper<IOUAmount, MPTAmount>(ctx, in, out);
}

/** Creates an MPT→XRP book step.
 *
 *  @param ctx  Strand context.
 *  @param in   Taker-pays MPT issue.
 *  @return     `{tesSUCCESS, step}` or `{error, nullptr}`.
 */
std::pair<TER, std::unique_ptr<Step>>
makeBookStepMx(StrandContext const& ctx, MPTIssue const& in)
{
    return makeBookStepHelper<MPTAmount, XRPAmount>(ctx, in, xrpIssue());
}

/** Creates an XRP→MPT book step.
 *
 *  @param ctx  Strand context.
 *  @param out  Taker-gets MPT issue.
 *  @return     `{tesSUCCESS, step}` or `{error, nullptr}`.
 */
std::pair<TER, std::unique_ptr<Step>>
makeBookStepXm(StrandContext const& ctx, MPTIssue const& out)
{
    return makeBookStepHelper<XRPAmount, MPTAmount>(ctx, xrpIssue(), out);
}

}  // namespace xrpl
