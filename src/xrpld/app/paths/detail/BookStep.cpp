//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <xrpld/app/misc/AMMUtils.h>
#include <xrpld/app/paths/AMMLiquidity.h>
#include <xrpld/app/paths/AMMOffer.h>
#include <xrpld/app/paths/detail/FlatSets.h>
#include <xrpld/app/paths/detail/Steps.h>
#include <xrpld/app/tx/detail/OfferStream.h>
#include <xrpld/ledger/PaymentSandbox.h>
#include <xrpl/basics/IOUAmount.h>
#include <xrpl/basics/Log.h>
#include <xrpl/basics/XRPAmount.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/Book.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Quality.h>

#include <boost/container/flat_set.hpp>

#include <numeric>
#include <sstream>

namespace ripple {

template <class TIn, class TOut, class TDerived>
class BookStep : public StepImp<TIn, TOut, BookStep<TIn, TOut, TDerived>>
{
protected:
    enum class OfferType { AMM, CLOB };

    uint32_t const maxOffersToConsume_;
    Book book_;
    AccountID strandSrc_;
    AccountID strandDst_;
    // Charge transfer fees when the prev step redeems
    Step const* const prevStep_ = nullptr;
    bool const ownerPaysTransferFee_;
    // Mark as inactive (dry) if too many offers are consumed
    bool inactive_ = false;
    /** Number of offers consumed or partially consumed the last time
        the step ran, including expired and unfunded offers.

        N.B. This this not the total number offers consumed by this step for the
        entire payment, it is only the number the last time it ran. Offers may
        be partially consumed multiple times during a payment.
    */
    std::uint32_t offersUsed_ = 0;
    // If set, AMM liquidity might be available
    // if AMM offer quality is better than CLOB offer
    // quality or there is no CLOB offer.
    std::optional<AMMLiquidity<TIn, TOut>> ammLiquidity_;
    beast::Journal const j_;

    struct Cache
    {
        TIn in;
        TOut out;

        Cache(TIn const& in_, TOut const& out_) : in(in_), out(out_)
        {
        }
    };

    std::optional<Cache> cache_;

    static uint32_t
    getMaxOffersToConsume(StrandContext const& ctx)
    {
        if (ctx.view.rules().enabled(fix1515))
            return 1000;
        return 2000;
    }

public:
    BookStep(StrandContext const& ctx, Issue const& in, Issue const& out)
        : maxOffersToConsume_(getMaxOffersToConsume(ctx))
        , book_(in, out)
        , strandSrc_(ctx.strandSrc)
        , strandDst_(ctx.strandDst)
        , prevStep_(ctx.prevStep)
        , ownerPaysTransferFee_(ctx.ownerPaysTransferFee)
        , j_(ctx.j)
    {
        if (auto const ammSle = ctx.view.read(keylet::amm(in, out));
            ammSle && ammSle->getFieldAmount(sfLPTokenBalance) != beast::zero)
            ammLiquidity_.emplace(
                ctx.view,
                (*ammSle)[sfAccount],
                getTradingFee(ctx.view, *ammSle, ctx.ammContext.account()),
                in,
                out,
                ctx.ammContext,
                ctx.j);
    }

    Book const&
    book() const
    {
        return book_;
    }

    std::optional<EitherAmount>
    cachedIn() const override
    {
        if (!cache_)
            return std::nullopt;
        return EitherAmount(cache_->in);
    }

    std::optional<EitherAmount>
    cachedOut() const override
    {
        if (!cache_)
            return std::nullopt;
        return EitherAmount(cache_->out);
    }

    DebtDirection
    debtDirection(ReadView const& sb, StrandDirection dir) const override
    {
        return ownerPaysTransferFee_ ? DebtDirection::issues
                                     : DebtDirection::redeems;
    }

    std::optional<Book>
    bookStepBook() const override
    {
        return book_;
    }

    std::pair<std::optional<Quality>, DebtDirection>
    qualityUpperBound(ReadView const& v, DebtDirection prevStepDir)
        const override;

    std::pair<std::optional<QualityFunction>, DebtDirection>
    getQualityFunc(ReadView const& v, DebtDirection prevStepDir) const override;

    std::uint32_t
    offersUsed() const override;

    std::pair<TIn, TOut>
    revImp(
        PaymentSandbox& sb,
        ApplyView& afView,
        boost::container::flat_set<uint256>& ofrsToRm,
        TOut const& out);

    std::pair<TIn, TOut>
    fwdImp(
        PaymentSandbox& sb,
        ApplyView& afView,
        boost::container::flat_set<uint256>& ofrsToRm,
        TIn const& in);

    std::pair<bool, EitherAmount>
    validFwd(PaymentSandbox& sb, ApplyView& afView, EitherAmount const& in)
        override;

    // Check for errors frozen constraints.
    TER
    check(StrandContext const& ctx) const;

    bool
    inactive() const override
    {
        return inactive_;
    }

protected:
    std::string
    logStringImpl(char const* name) const
    {
        std::ostringstream ostr;
        ostr << name << ": " << "\ninIss: " << book_.in.account
             << "\noutIss: " << book_.out.account
             << "\ninCur: " << book_.in.currency
             << "\noutCur: " << book_.out.currency;
        return ostr.str();
    }

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

    bool
    equal(Step const& rhs) const override;

    // Iterate through the offers at the best quality in a book.
    // Unfunded offers and bad offers are skipped (and returned).
    // callback is called with the offer SLE, taker pays, taker gets.
    // If callback returns false, don't process any more offers.
    // Return the unfunded and bad offers and the number of offers consumed.
    template <class Callback>
    std::pair<boost::container::flat_set<uint256>, std::uint32_t>
    forEachOffer(
        PaymentSandbox& sb,
        ApplyView& afView,
        DebtDirection prevStepDebtDir,
        Callback& callback) const;

    // Offer is either TOffer or AMMOffer
    template <template <typename, typename> typename Offer>
    void
    consumeOffer(
        PaymentSandbox& sb,
        Offer<TIn, TOut>& offer,
        TAmounts<TIn, TOut> const& ofrAmt,
        TAmounts<TIn, TOut> const& stepAmt,
        TOut const& ownerGives) const;

    // If clobQuality is available and has a better quality then return nullopt,
    // otherwise if amm liquidity is available return AMM offer adjusted based
    // on clobQuality.
    std::optional<AMMOffer<TIn, TOut>>
    getAMMOffer(ReadView const& view, std::optional<Quality> const& clobQuality)
        const;

    // If seated then it is either order book tip quality or AMMOffer,
    // whichever is a better quality.
    std::optional<std::variant<Quality, AMMOffer<TIn, TOut>>>
    tip(ReadView const& view) const;
    // If seated then it is either AMM or CLOB quality,
    // whichever is a better quality. OfferType is AMM
    // if AMM quality is better.
    std::optional<std::pair<Quality, OfferType>>
    tipOfferQuality(ReadView const& view) const;
    // If seated then it is either AMM or CLOB quality function,
    // whichever is a better quality.
    std::optional<QualityFunction>
    tipOfferQualityF(ReadView const& view) const;
};

//------------------------------------------------------------------------------

// Flow is used in two different circumstances for transferring funds:
//  o Payments, and
//  o Offer crossing.
// The rules for handling funds in these two cases are almost, but not
// quite, the same.

// Payment BookStep template class (not offer crossing).
template <class TIn, class TOut>
class BookPaymentStep : public BookStep<TIn, TOut, BookPaymentStep<TIn, TOut>>
{
public:
    explicit BookPaymentStep() = default;

    using BookStep<TIn, TOut, BookPaymentStep<TIn, TOut>>::BookStep;
    using BookStep<TIn, TOut, BookPaymentStep<TIn, TOut>>::qualityUpperBound;
    using typename BookStep<TIn, TOut, BookPaymentStep<TIn, TOut>>::OfferType;

    // Never limit self cross quality on a payment.
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

    // A payment can look at offers of any quality
    bool
    checkQualityThreshold(Quality const& quality) const
    {
        return true;
    }

    // A payment doesn't use quality threshold (limitQuality)
    // since the strand's quality doesn't directly relate to the step's quality.
    std::optional<Quality>
    qualityThreshold(Quality const& lobQuality) const
    {
        return lobQuality;
    }

    // For a payment ofrInRate is always the same as trIn.
    std::uint32_t
    getOfrInRate(Step const*, AccountID const&, std::uint32_t trIn) const
    {
        return trIn;
    }

    // For a payment ofrOutRate is always the same as trOut.
    std::uint32_t
    getOfrOutRate(
        Step const*,
        AccountID const&,
        AccountID const&,
        std::uint32_t trOut) const
    {
        return trOut;
    }

    Quality
    adjustQualityWithFees(
        ReadView const& v,
        Quality const& ofrQ,
        DebtDirection prevStepDir,
        WaiveTransferFee waiveFee,
        OfferType,
        Rules const&) const
    {
        // Charge the offer owner, not the sender
        // Charge a fee even if the owner is the same as the issuer
        // (the old code does not charge a fee)
        // Calculate amount that goes to the taker and the amount charged the
        // offer owner
        auto rate = [&](AccountID const& id) {
            if (isXRP(id) || id == this->strandDst_)
                return parityRate;
            return transferRate(v, id);
        };

        auto const trIn =
            redeems(prevStepDir) ? rate(this->book_.in.account) : parityRate;
        // Always charge the transfer fee, even if the owner is the issuer,
        // unless the fee is waived
        auto const trOut =
            (this->ownerPaysTransferFee_ && waiveFee == WaiveTransferFee::No)
            ? rate(this->book_.out.account)
            : parityRate;

        Quality const q1{getRate(STAmount(trOut.value), STAmount(trIn.value))};
        return composed_quality(q1, ofrQ);
    }

    std::string
    logString() const override
    {
        return this->logStringImpl("BookPaymentStep");
    }
};

// Offer crossing BookStep template class (not a payment).
template <class TIn, class TOut>
class BookOfferCrossingStep
    : public BookStep<TIn, TOut, BookOfferCrossingStep<TIn, TOut>>
{
    using BookStep<TIn, TOut, BookOfferCrossingStep<TIn, TOut>>::
        qualityUpperBound;
    using typename BookStep<TIn, TOut, BookOfferCrossingStep<TIn, TOut>>::
        OfferType;

private:
    // Helper function that throws if the optional passed to the constructor
    // is none.
    static Quality
    getQuality(std::optional<Quality> const& limitQuality)
    {
        // It's really a programming error if the quality is missing.
        ASSERT(
            limitQuality.has_value(),
            "ripple::BookOfferCrossingStep::getQuality : nonzero quality");
        if (!limitQuality)
            Throw<FlowException>(tefINTERNAL, "Offer requires quality.");
        return *limitQuality;
    }

public:
    BookOfferCrossingStep(
        StrandContext const& ctx,
        Issue const& in,
        Issue const& out)
        : BookStep<TIn, TOut, BookOfferCrossingStep<TIn, TOut>>(ctx, in, out)
        , defaultPath_(ctx.isDefaultPath)
        , qualityThreshold_(getQuality(ctx.limitQuality))
    {
    }

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
        // This method supports some correct but slightly surprising
        // behavior in offer crossing.  The scenario:
        //
        //  o alice has already created one or more offers.
        //  o alice creates another offer that can be directly crossed (not
        //    autobridged) by one or more of her previously created offer(s).
        //
        // What does the offer crossing do?
        //
        //  o The offer crossing could go ahead and cross the offers leaving
        //    either one reduced offer (partial crossing) or zero offers
        //    (exact crossing) in the ledger.  We don't do this.  And, really,
        //    the offer creator probably didn't want us to.
        //
        //  o We could skip over the self offer in the book and only cross
        //    offers that are not our own.  This would make a lot of sense,
        //    but we don't do it.  Part of the rationale is that we can only
        //    operate on the tip of the order book.  We can't leave an offer
        //    behind -- it would sit on the tip and block access to other
        //    offers.
        //
        //  o We could delete the self-crossable offer(s) off the tip of the
        //    book and continue with offer crossing.  That's what we do.
        //
        // To support this scenario offer crossing has a special rule.  If:
        //   a. We're offer crossing using default path (no autobridging), and
        //   b. The offer's quality is at least as good as our quality, and
        //   c. We're about to cross one of our own offers, then
        //   d. Delete the old offer from the ledger.
        if (defaultPath_ && offer.quality() >= qualityThreshold_ &&
            strandSrc == offer.owner() && strandDst == offer.owner())
        {
            // Remove this offer even if no crossing occurs.
            if (auto const key = offer.key())
                offers.permRmOffer(*key);

            // If no offers have been attempted yet then it's okay to move to
            // a different quality.
            if (!offerAttempted)
                ofrQ = std::nullopt;

            // Return true so the current offer will be deleted.
            return true;
        }
        return false;
    }

    // Offer crossing can prune the offers it needs to look at with a
    // quality threshold.
    bool
    checkQualityThreshold(Quality const& quality) const
    {
        return !defaultPath_ || quality >= qualityThreshold_;
    }

    // Return quality threshold or nullopt to use when generating AMM offer.
    // AMM synthetic offer is generated to match LOB offer quality.
    // If LOB tip offer quality is less than qualityThreshold
    // then generated AMM offer quality is also less than qualityThreshold and
    // the offer is not crossed even though AMM might generate a better quality
    // offer. To address this, if qualityThreshold is greater than lobQuality
    // then don't use quality to generate the AMM offer. The limit out value
    // generates the maximum AMM offer in this case, which matches
    // the quality threshold. This only applies to single path scenario.
    // Multi-path AMM offers work the same as LOB offers.
    std::optional<Quality>
    qualityThreshold(Quality const& lobQuality) const
    {
        if (this->ammLiquidity_ && !this->ammLiquidity_->multiPath() &&
            qualityThreshold_ > lobQuality)
            return std::nullopt;
        return lobQuality;
    }

    // For offer crossing don't pay the transfer fee if alice is paying alice.
    // A regular (non-offer-crossing) payment does not apply this rule.
    std::uint32_t
    getOfrInRate(
        Step const* prevStep,
        AccountID const& owner,
        std::uint32_t trIn) const
    {
        auto const srcAcct =
            prevStep ? prevStep->directStepSrcAcct() : std::nullopt;

        return owner == srcAcct  // If offer crossing && prevStep is DirectI
            ? QUALITY_ONE        // && src is offer owner
            : trIn;              // then rate = QUALITY_ONE
    }

    // See comment on getOfrInRate().
    std::uint32_t
    getOfrOutRate(
        Step const* prevStep,
        AccountID const& owner,
        AccountID const& strandDst,
        std::uint32_t trOut) const
    {
        return                                       // If offer crossing
            prevStep && prevStep->bookStepBook() &&  // && prevStep is BookStep
                owner == strandDst                   // && dest is offer owner
            ? QUALITY_ONE
            : trOut;  // then rate = QUALITY_ONE
    }

    Quality
    adjustQualityWithFees(
        ReadView const& v,
        Quality const& ofrQ,
        DebtDirection prevStepDir,
        WaiveTransferFee waiveFee,
        OfferType offerType,
        Rules const& rules) const
    {
        // Offer x-ing does not charge a transfer fee when the offer's owner
        // is the same as the strand dst. It is important that
        // `qualityUpperBound` is an upper bound on the quality (it is used to
        // ignore strands whose quality cannot meet a minimum threshold).  When
        // calculating quality assume no fee is charged, or the estimate will no
        // longer be an upper bound.

        // Single path AMM offer has to factor in the transfer in rate
        // when calculating the upper bound quality and the quality function
        // because single path AMM's offer quality is not constant.
        if (!rules.enabled(fixAMMv1_1))
            return ofrQ;
        else if (
            offerType == OfferType::CLOB ||
            (this->ammLiquidity_ && this->ammLiquidity_->multiPath()))
            return ofrQ;

        auto rate = [&](AccountID const& id) {
            if (isXRP(id) || id == this->strandDst_)
                return parityRate;
            return transferRate(v, id);
        };

        auto const trIn =
            redeems(prevStepDir) ? rate(this->book_.in.account) : parityRate;
        // AMM doesn't pay the transfer fee on the out amount
        auto const trOut = parityRate;

        Quality const q1{getRate(STAmount(trOut.value), STAmount(trIn.value))};
        return composed_quality(q1, ofrQ);
    }

    std::string
    logString() const override
    {
        return this->logStringImpl("BookOfferCrossingStep");
    }

private:
    bool const defaultPath_;
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
BookStep<TIn, TOut, TDerived>::qualityUpperBound(
    ReadView const& v,
    DebtDirection prevStepDir) const
{
    auto const dir = this->debtDirection(v, StrandDirection::forward);

    std::optional<std::pair<Quality, OfferType>> const res = tipOfferQuality(v);
    if (!res)
        return {std::nullopt, dir};

    auto const waiveFee = (std::get<OfferType>(*res) == OfferType::AMM)
        ? WaiveTransferFee::Yes
        : WaiveTransferFee::No;

    Quality const q = static_cast<TDerived const*>(this)->adjustQualityWithFees(
        v,
        std::get<Quality>(*res),
        prevStepDir,
        waiveFee,
        std::get<OfferType>(*res),
        v.rules());
    return {q, dir};
}

template <class TIn, class TOut, class TDerived>
std::pair<std::optional<QualityFunction>, DebtDirection>
BookStep<TIn, TOut, TDerived>::getQualityFunc(
    ReadView const& v,
    DebtDirection prevStepDir) const
{
    auto const dir = this->debtDirection(v, StrandDirection::forward);

    std::optional<QualityFunction> const res = tipOfferQualityF(v);
    if (!res)
        return {std::nullopt, dir};

    // AMM
    if (!res->isConst())
    {
        auto static const qOne = Quality{STAmount::uRateOne};
        auto const q =
            static_cast<TDerived const*>(this)->adjustQualityWithFees(
                v,
                qOne,
                prevStepDir,
                WaiveTransferFee::Yes,
                OfferType::AMM,
                v.rules());
        if (q == qOne)
            return {res, dir};
        QualityFunction qf{q, QualityFunction::CLOBLikeTag{}};
        qf.combine(*res);
        return {qf, dir};
    }

    // CLOB
    Quality const q = static_cast<TDerived const*>(this)->adjustQualityWithFees(
        v,
        *(res->quality()),
        prevStepDir,
        WaiveTransferFee::No,
        OfferType::CLOB,
        v.rules());
    return {QualityFunction{q, QualityFunction::CLOBLikeTag{}}, dir};
}

template <class TIn, class TOut, class TDerived>
std::uint32_t
BookStep<TIn, TOut, TDerived>::offersUsed() const
{
    return offersUsed_;
}

// Adjust the offer amount and step amount subject to the given input limit
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
        auto const inLmt =
            mulRatio(stpAmt.in, QUALITY_ONE, transferRateIn, /*roundUp*/ false);
        // It turns out we can prevent order book blocking by (strictly)
        // rounding down the ceil_in() result.  By rounding down we guarantee
        // that the quality of an offer left in the ledger is as good or
        // better than the quality of the containing order book page.
        //
        // This adjustment changes transaction outcomes, so it must be made
        // under an amendment.
        ofrAmt = offer.limitIn(ofrAmt, inLmt, /* roundUp */ false);
        stpAmt.out = ofrAmt.out;
        ownerGives = mulRatio(
            ofrAmt.out, transferRateOut, QUALITY_ONE, /*roundUp*/ false);
    }
}

// Adjust the offer amount and step amount subject to the given output limit
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
        ownerGives = mulRatio(
            stpAmt.out, transferRateOut, QUALITY_ONE, /*roundUp*/ false);
        ofrAmt = offer.limitOut(
            ofrAmt,
            stpAmt.out,
            /*roundUp*/ true);
        stpAmt.in =
            mulRatio(ofrAmt.in, transferRateIn, QUALITY_ONE, /*roundUp*/ true);
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
    // Charge the offer owner, not the sender
    // Charge a fee even if the owner is the same as the issuer
    // (the old code does not charge a fee)
    // Calculate amount that goes to the taker and the amount charged the offer
    // owner
    auto rate = [this, &sb](AccountID const& id) -> std::uint32_t {
        if (isXRP(id) || id == this->strandDst_)
            return QUALITY_ONE;
        return transferRate(sb, id).value;
    };

    std::uint32_t const trIn =
        redeems(prevStepDir) ? rate(book_.in.account) : QUALITY_ONE;
    // Always charge the transfer fee, even if the owner is the issuer
    std::uint32_t const trOut =
        ownerPaysTransferFee_ ? rate(book_.out.account) : QUALITY_ONE;

    typename FlowOfferStream<TIn, TOut>::StepCounter counter(
        maxOffersToConsume_, j_);

    FlowOfferStream<TIn, TOut> offers(
        sb, afView, book_, sb.parentCloseTime(), counter, j_);

    bool const flowCross = afView.rules().enabled(featureFlowCross);
    bool offerAttempted = false;
    std::optional<Quality> ofrQ;
    auto execOffer = [&](auto& offer) {
        // Note that offer.quality() returns a (non-optional) Quality.  So
        // ofrQ is always safe to use below this point in the lambda.
        if (!ofrQ)
            ofrQ = offer.quality();
        else if (*ofrQ != offer.quality())
            return false;

        if (static_cast<TDerived const*>(this)->limitSelfCrossQuality(
                strandSrc_, strandDst_, offer, ofrQ, offers, offerAttempted))
            return true;

        // Make sure offer owner has authorization to own IOUs from issuer.
        // An account can always own XRP or their own IOUs.
        if (flowCross && (!isXRP(offer.issueIn().currency)) &&
            (offer.owner() != offer.issueIn().account))
        {
            auto const& issuerID = offer.issueIn().account;
            auto const issuer = afView.read(keylet::account(issuerID));
            if (issuer && ((*issuer)[sfFlags] & lsfRequireAuth))
            {
                // Issuer requires authorization.  See if offer owner has that.
                auto const& ownerID = offer.owner();
                auto const authFlag =
                    issuerID > ownerID ? lsfHighAuth : lsfLowAuth;

                auto const line = afView.read(
                    keylet::line(ownerID, issuerID, offer.issueIn().currency));

                if (!line || (((*line)[sfFlags] & authFlag) == 0))
                {
                    // Offer owner not authorized to hold IOU from issuer.
                    // Remove this offer even if no crossing occurs.
                    if (auto const key = offer.key())
                        offers.permRmOffer(*key);
                    if (!offerAttempted)
                        // Change quality only if no previous offers were tried.
                        ofrQ = std::nullopt;
                    // Returning true causes offers.step() to delete the offer.
                    return true;
                }
            }
        }

        if (!static_cast<TDerived const*>(this)->checkQualityThreshold(
                offer.quality()))
            return false;

        auto const [ofrInRate, ofrOutRate] = offer.adjustRates(
            static_cast<TDerived const*>(this)->getOfrInRate(
                prevStep_, offer.owner(), trIn),
            static_cast<TDerived const*>(this)->getOfrOutRate(
                prevStep_, offer.owner(), strandDst_, trOut));

        auto ofrAmt = offer.amount();
        TAmounts stpAmt{
            mulRatio(ofrAmt.in, ofrInRate, QUALITY_ONE, /*roundUp*/ true),
            ofrAmt.out};

        // owner pays the transfer fee.
        auto ownerGives =
            mulRatio(ofrAmt.out, ofrOutRate, QUALITY_ONE, /*roundUp*/ false);

        auto const funds = offer.isFunded()
            ? ownerGives  // Offer owner is issuer; they have unlimited funds
            : offers.ownerFunds();

        // Only if CLOB offer
        if (funds < ownerGives)
        {
            // We already know offer.owner()!=offer.issueOut().account
            ownerGives = funds;
            stpAmt.out = mulRatio(
                ownerGives, QUALITY_ONE, ofrOutRate, /*roundUp*/ false);

            // It turns out we can prevent order book blocking by (strictly)
            // rounding down the ceil_out() result.  This adjustment changes
            // transaction outcomes, so it must be made under an amendment.
            ofrAmt = offer.limitOut(ofrAmt, stpAmt.out, /*roundUp*/ false);

            stpAmt.in =
                mulRatio(ofrAmt.in, ofrInRate, QUALITY_ONE, /*roundUp*/ true);
        }

        offerAttempted = true;
        return callback(
            offer, ofrAmt, stpAmt, ownerGives, ofrInRate, ofrOutRate);
    };

    // At any payment engine iteration, AMM offer can only be consumed once.
    auto tryAMM = [&](std::optional<Quality> const& lobQuality) -> bool {
        // If offer crossing then use either LOB quality or nullopt
        // to prevent AMM being blocked by a lower quality LOB.
        auto const qualityThreshold = [&]() -> std::optional<Quality> {
            if (sb.rules().enabled(fixAMMv1_1) && lobQuality)
                return static_cast<TDerived const*>(this)->qualityThreshold(
                    *lobQuality);
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
        // Might have AMM offer if there are no LOB offers.
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
        // purposely written as separate if statements so we get logging even
        // when the amendment isn't active.
        if (sb.rules().enabled(fixAMMOverflowOffer))
        {
            Throw<FlowException>(
                tecINVARIANT_FAILED, "AMM pool product invariant failed.");
        }
    }

    // The offer owner gets the ofrAmt. The difference between ofrAmt and
    // stepAmt is a transfer fee that goes to book_.in.account
    {
        auto const dr = offer.send(
            sb,
            book_.in.account,
            offer.owner(),
            toSTAmount(ofrAmt.in, book_.in),
            j_);
        if (dr != tesSUCCESS)
            Throw<FlowException>(dr);
    }

    // The offer owner pays `ownerGives`. The difference between ownerGives and
    // stepAmt is a transfer fee that goes to book_.out.account
    {
        auto const cr = offer.send(
            sb,
            offer.owner(),
            book_.out.account,
            toSTAmount(ownerGives, book_.out),
            j_);
        if (cr != tesSUCCESS)
            Throw<FlowException>(cr);
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
    // This can be simplified (and sped up) if directories are never empty.
    Sandbox sb(&view, tapNONE);
    BookTip bt(sb, book_);
    auto const lobQuality =
        bt.step(j_) ? std::optional<Quality>(bt.quality()) : std::nullopt;
    // Multi-path offer generates an offer with the quality
    // calculated from the offer size and the quality is constant in this case.
    // Single path offer quality changes with the offer size. Spot price quality
    // (SPQ) can't be used in this case as the upper bound quality because
    // even if SPQ quality is better than LOB quality, it might not be possible
    // to generate AMM offer at or better quality than LOB quality. Another
    // factor to consider is limit quality on offer crossing. If LOB quality
    // is greater than limit quality then use LOB quality when generating AMM
    // offer, otherwise don't use quality threshold when generating AMM offer.
    // AMM or LOB offer, whether multi-path or single path then can be selected
    // based on the best offer quality. Using the quality to generate AMM offer
    // in this case also prevents the payment engine from going into multiple
    // iterations to cross a LOB offer. This happens when AMM changes
    // the out amount at the start of iteration to match the limitQuality
    // on offer crossing but AMM can't generate the offer at this quality,
    // as the result a LOB offer is partially crossed, and it might take a few
    // iterations to fully cross the offer.
    auto const qualityThreshold = [&]() -> std::optional<Quality> {
        if (view.rules().enabled(fixAMMv1_1) && lobQuality)
            return static_cast<TDerived const*>(this)->qualityThreshold(
                *lobQuality);
        return std::nullopt;
    }();
    // AMM quality is better or no LOB offer
    if (auto const ammOffer = getAMMOffer(view, qualityThreshold); ammOffer &&
        ((lobQuality && ammOffer->quality() > lobQuality) || !lobQuality))
        return ammOffer;
    // LOB quality is better or nullopt
    return lobQuality;
}

template <class TIn, class TOut, class TDerived>
auto
BookStep<TIn, TOut, TDerived>::tipOfferQuality(ReadView const& view) const
    -> std::optional<std::pair<Quality, OfferType>>
{
    if (auto const res = tip(view); !res)
        return std::nullopt;
    else if (auto const q = std::get_if<Quality>(&(*res)))
        return std::make_pair(*q, OfferType::CLOB);
    else
        return std::make_pair(
            std::get<AMMOffer<TIn, TOut>>(*res).quality(), OfferType::AMM);
}

template <class TIn, class TOut, class TDerived>
std::optional<QualityFunction>
BookStep<TIn, TOut, TDerived>::tipOfferQualityF(ReadView const& view) const
{
    if (auto const res = tip(view); !res)
        return std::nullopt;
    else if (auto const q = std::get_if<Quality>(&(*res)))
        return QualityFunction{*q, QualityFunction::CLOBLikeTag{}};
    else
        return std::get<AMMOffer<TIn, TOut>>(*res).getQualityFunc();
}

template <class TCollection>
static auto
sum(TCollection const& col)
{
    using TResult = std::decay_t<decltype(*col.begin())>;
    if (col.empty())
        return TResult{beast::zero};
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

    TAmounts<TIn, TOut> result(beast::zero, beast::zero);

    auto remainingOut = out;

    boost::container::flat_multiset<TIn> savedIns;
    savedIns.reserve(64);
    boost::container::flat_multiset<TOut> savedOuts;
    savedOuts.reserve(64);

    /* amt fed will be adjusted by owner funds (and may differ from the offer's
      amounts - tho always <=)
      Return true to continue to receive offers, false to stop receiving offers.
    */
    auto eachOffer = [&](auto& offer,
                         TAmounts<TIn, TOut> const& ofrAmt,
                         TAmounts<TIn, TOut> const& stpAmt,
                         TOut const& ownerGives,
                         std::uint32_t transferRateIn,
                         std::uint32_t transferRateOut) mutable -> bool {
        if (remainingOut <= beast::zero)
            return false;

        if (stpAmt.out <= remainingOut)
        {
            savedIns.insert(stpAmt.in);
            savedOuts.insert(stpAmt.out);
            result = TAmounts<TIn, TOut>(sum(savedIns), sum(savedOuts));
            remainingOut = out - result.out;
            this->consumeOffer(sb, offer, ofrAmt, stpAmt, ownerGives);
            // return true b/c even if the payment is satisfied,
            // we need to consume the offer
            return true;
        }
        else
        {
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
            remainingOut = beast::zero;
            savedIns.insert(stpAdjAmt.in);
            savedOuts.insert(remainingOut);
            result.in = sum(savedIns);
            result.out = out;
            this->consumeOffer(sb, offer, ofrAdjAmt, stpAdjAmt, ownerGivesAdj);

            // Explicitly check whether the offer is funded.  Given that we have
            // (stpAmt.out > remainingOut), it's natural to assume the offer
            // will still be funded after consuming remainingOut but that is
            // not always the case.  If the mantissas of two IOU amounts differ
            // by less than ten, then subtracting them leaves a zero.
            return offer.fully_consumed();
        }
    };

    {
        auto const prevStepDebtDir = [&] {
            if (prevStep_)
                return prevStep_->debtDirection(sb, StrandDirection::reverse);
            return DebtDirection::issues;
        }();
        auto const r = forEachOffer(sb, afView, prevStepDebtDir, eachOffer);
        boost::container::flat_set<uint256> toRm = std::move(std::get<0>(r));
        std::uint32_t const offersConsumed = std::get<1>(r);
        offersUsed_ = offersConsumed;
        SetUnion(ofrsToRm, toRm);

        if (offersConsumed >= maxOffersToConsume_)
        {
            // Too many iterations, mark this strand as inactive
            if (!afView.rules().enabled(fix1515))
            {
                // Don't use the liquidity
                cache_.emplace(beast::zero, beast::zero);
                return {beast::zero, beast::zero};
            }

            // Use the liquidity, but use this to mark the strand as inactive so
            // it's not used further
            inactive_ = true;
        }
    }

    switch (remainingOut.signum())
    {
        case -1: {
            // something went very wrong
            JLOG(j_.error())
                << "BookStep remainingOut < 0 " << to_string(remainingOut);
            UNREACHABLE("ripple::BookStep::revImp : remaining less than zero");
            cache_.emplace(beast::zero, beast::zero);
            return {beast::zero, beast::zero};
        }
        case 0: {
            // due to normalization, remainingOut can be zero without
            // result.out == out. Force result.out == out for this case
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
    ASSERT(cache_.has_value(), "ripple::BookStep::fwdImp : cache is set");

    TAmounts<TIn, TOut> result(beast::zero, beast::zero);

    auto remainingIn = in;

    boost::container::flat_multiset<TIn> savedIns;
    savedIns.reserve(64);
    boost::container::flat_multiset<TOut> savedOuts;
    savedOuts.reserve(64);

    // amt fed will be adjusted by owner funds (and may differ from the offer's
    // amounts - tho always <=)
    auto eachOffer = [&](auto& offer,
                         TAmounts<TIn, TOut> const& ofrAmt,
                         TAmounts<TIn, TOut> const& stpAmt,
                         TOut const& ownerGives,
                         std::uint32_t transferRateIn,
                         std::uint32_t transferRateOut) mutable -> bool {
        ASSERT(
            cache_.has_value(),
            "ripple::BookStep::fwdImp::eachOffer : cache is set");

        if (remainingIn <= beast::zero)
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
            // consume the offer even if stepAmt.in == remainingIn
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
            // The step produced more output in the forward pass than the
            // reverse pass while consuming the same input (or less). If we
            // compute the input required to produce the cached output
            // (produced in the reverse step) and the input is equal to
            // the input consumed in the forward step, then consume the
            // input provided in the forward step and produce the output
            // requested from the reverse step.
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
                // This is (likely) a problem case, and will be caught
                // with later checks
                savedOuts.insert(lastOutAmt);
            }
        }

        remainingIn = in - result.in;
        this->consumeOffer(sb, offer, ofrAdjAmt, stpAdjAmt, ownerGivesAdj);

        // When the mantissas of two iou amounts differ by less than ten, then
        // subtracting them leaves a result of zero. This can cause the check
        // for (stpAmt.in > remainingIn) to incorrectly think an offer will be
        // funded after subtracting remainingIn.
        return processMore || offer.fully_consumed();
    };

    {
        auto const prevStepDebtDir = [&] {
            if (prevStep_)
                return prevStep_->debtDirection(sb, StrandDirection::forward);
            return DebtDirection::issues;
        }();
        auto const r = forEachOffer(sb, afView, prevStepDebtDir, eachOffer);
        boost::container::flat_set<uint256> toRm = std::move(std::get<0>(r));
        std::uint32_t const offersConsumed = std::get<1>(r);
        offersUsed_ = offersConsumed;
        SetUnion(ofrsToRm, toRm);

        if (offersConsumed >= maxOffersToConsume_)
        {
            // Too many iterations, mark this strand as inactive (dry)
            if (!afView.rules().enabled(fix1515))
            {
                // Don't use the liquidity
                cache_.emplace(beast::zero, beast::zero);
                return {beast::zero, beast::zero};
            }

            // Use the liquidity, but use this to mark the strand as inactive so
            // it's not used further
            inactive_ = true;
        }
    }

    switch (remainingIn.signum())
    {
        case -1: {
            // something went very wrong
            JLOG(j_.error())
                << "BookStep remainingIn < 0 " << to_string(remainingIn);
            UNREACHABLE("ripple::BookStep::fwdImp : remaining less than zero");
            cache_.emplace(beast::zero, beast::zero);
            return {beast::zero, beast::zero};
        }
        case 0: {
            // due to normalization, remainingIn can be zero without
            // result.in == in. Force result.in == in for this case
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
        return {false, EitherAmount(TOut(beast::zero))};
    }

    auto const savCache = *cache_;

    try
    {
        boost::container::flat_set<uint256> dummy;
        fwdImp(sb, afView, dummy, get<TIn>(in));  // changes cache
    }
    catch (FlowException const&)
    {
        return {false, EitherAmount(TOut(beast::zero))};
    }

    if (!(checkNear(savCache.in, cache_->in) &&
          checkNear(savCache.out, cache_->out)))
    {
        JLOG(j_.warn()) << "Strand re-execute check failed."
                        << " ExpectedIn: " << to_string(savCache.in)
                        << " CachedIn: " << to_string(cache_->in)
                        << " ExpectedOut: " << to_string(savCache.out)
                        << " CachedOut: " << to_string(cache_->out);
        return {false, EitherAmount(cache_->out)};
    }
    return {true, EitherAmount(cache_->out)};
}

template <class TIn, class TOut, class TDerived>
TER
BookStep<TIn, TOut, TDerived>::check(StrandContext const& ctx) const
{
    if (book_.in == book_.out)
    {
        JLOG(j_.debug()) << "BookStep: Book with same in and out issuer "
                         << *this;
        return temBAD_PATH;
    }
    if (!isConsistent(book_.in) || !isConsistent(book_.out))
    {
        JLOG(j_.debug()) << "Book: currency is inconsistent with issuer."
                         << *this;
        return temBAD_PATH;
    }

    // Do not allow two books to output the same issue. This may cause offers on
    // one step to unfund offers in another step.
    if (!ctx.seenBookOuts.insert(book_.out).second ||
        ctx.seenDirectIssues[0].count(book_.out))
    {
        JLOG(j_.debug()) << "BookStep: loop detected: " << *this;
        return temBAD_PATH_LOOP;
    }

    if (ctx.seenDirectIssues[1].count(book_.out))
    {
        JLOG(j_.debug()) << "BookStep: loop detected: " << *this;
        return temBAD_PATH_LOOP;
    }

    auto issuerExists = [](ReadView const& view, Issue const& iss) -> bool {
        return isXRP(iss.account) || view.read(keylet::account(iss.account));
    };

    if (!issuerExists(ctx.view, book_.in) || !issuerExists(ctx.view, book_.out))
    {
        JLOG(j_.debug()) << "BookStep: deleted issuer detected: " << *this;
        return tecNO_ISSUER;
    }

    if (ctx.prevStep)
    {
        if (auto const prev = ctx.prevStep->directStepSrcAcct())
        {
            auto const& view = ctx.view;
            auto const& cur = book_.in.account;

            auto sle = view.read(keylet::line(*prev, cur, book_.in.currency));
            if (!sle)
                return terNO_LINE;
            if ((*sle)[sfFlags] &
                ((cur > *prev) ? lsfHighNoRipple : lsfLowNoRipple))
                return terNO_RIPPLE;
        }
    }

    return tesSUCCESS;
}

//------------------------------------------------------------------------------

namespace test {
// Needed for testing

template <class TIn, class TOut, class TDerived>
static bool
equalHelper(Step const& step, ripple::Book const& book)
{
    if (auto bs = dynamic_cast<BookStep<TIn, TOut, TDerived> const*>(&step))
        return book == bs->book();
    return false;
}

bool
bookStepEqual(Step const& step, ripple::Book const& book)
{
    bool const inXRP = isXRP(book.in.currency);
    bool const outXRP = isXRP(book.out.currency);
    if (inXRP && outXRP)
    {
        UNREACHABLE("ripple::test::bookStepEqual : no XRP to XRP book step");
        return false;  // no such thing as xrp/xrp book step
    }
    if (inXRP && !outXRP)
        return equalHelper<
            XRPAmount,
            IOUAmount,
            BookPaymentStep<XRPAmount, IOUAmount>>(step, book);
    if (!inXRP && outXRP)
        return equalHelper<
            IOUAmount,
            XRPAmount,
            BookPaymentStep<IOUAmount, XRPAmount>>(step, book);
    if (!inXRP && !outXRP)
        return equalHelper<
            IOUAmount,
            IOUAmount,
            BookPaymentStep<IOUAmount, IOUAmount>>(step, book);
    return false;
}
}  // namespace test

//------------------------------------------------------------------------------

template <class TIn, class TOut>
static std::pair<TER, std::unique_ptr<Step>>
make_BookStepHelper(StrandContext const& ctx, Issue const& in, Issue const& out)
{
    TER ter = tefINTERNAL;
    std::unique_ptr<Step> r;
    if (ctx.offerCrossing)
    {
        auto offerCrossingStep =
            std::make_unique<BookOfferCrossingStep<TIn, TOut>>(ctx, in, out);
        ter = offerCrossingStep->check(ctx);
        r = std::move(offerCrossingStep);
    }
    else  // payment
    {
        auto paymentStep =
            std::make_unique<BookPaymentStep<TIn, TOut>>(ctx, in, out);
        ter = paymentStep->check(ctx);
        r = std::move(paymentStep);
    }
    if (ter != tesSUCCESS)
        return {ter, nullptr};

    return {tesSUCCESS, std::move(r)};
}

std::pair<TER, std::unique_ptr<Step>>
make_BookStepII(StrandContext const& ctx, Issue const& in, Issue const& out)
{
    return make_BookStepHelper<IOUAmount, IOUAmount>(ctx, in, out);
}

std::pair<TER, std::unique_ptr<Step>>
make_BookStepIX(StrandContext const& ctx, Issue const& in)
{
    return make_BookStepHelper<IOUAmount, XRPAmount>(ctx, in, xrpIssue());
}

std::pair<TER, std::unique_ptr<Step>>
make_BookStepXI(StrandContext const& ctx, Issue const& out)
{
    return make_BookStepHelper<XRPAmount, IOUAmount>(ctx, xrpIssue(), out);
}

}  // namespace ripple
