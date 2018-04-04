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

#include <BeastConfig.h>
#include <ripple/app/paths/impl/Steps.h>
#include <ripple/app/paths/Credit.h>
#include <ripple/app/paths/NodeDirectory.h>
#include <ripple/app/tx/impl/OfferStream.h>
#include <ripple/basics/contract.h>
#include <ripple/basics/Log.h>
#include <ripple/ledger/Directory.h>
#include <ripple/ledger/PaymentSandbox.h>
#include <ripple/protocol/Book.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/IOUAmount.h>
#include <ripple/protocol/Quality.h>
#include <ripple/protocol/XRPAmount.h>

#include <boost/container/flat_set.hpp>

#include <numeric>
#include <sstream>

namespace ripple {

template<class TIn, class TOut, class TDerived>
class BookStep : public StepImp<TIn, TOut, BookStep<TIn, TOut, TDerived>>
{
protected:
    static constexpr uint32_t maxOffersToConsume_ = 2000;
    Book book_;
    AccountID strandSrc_;
    AccountID strandDst_;
    // Charge transfer fees when the prev step redeems
    Step const* const prevStep_ = nullptr;
    bool const ownerPaysTransferFee_;
    beast::Journal j_;

    struct Cache
    {
        TIn in;
        TOut out;

        Cache (TIn const& in_, TOut const& out_)
            : in (in_), out (out_)
        {
        }
    };

    boost::optional<Cache> cache_;

public:
    BookStep (StrandContext const& ctx,
        Issue const& in,
        Issue const& out)
        : book_ (in, out)
        , strandSrc_ (ctx.strandSrc)
        , strandDst_ (ctx.strandDst)
        , prevStep_ (ctx.prevStep)
        , ownerPaysTransferFee_ (ctx.ownerPaysTransferFee)
        , j_ (ctx.j)
    {
    }

    Book const& book() const
    {
        return book_;
    };

    boost::optional<EitherAmount>
    cachedIn () const override
    {
        if (!cache_)
            return boost::none;
        return EitherAmount (cache_->in);
    }

    boost::optional<EitherAmount>
    cachedOut () const override
    {
        if (!cache_)
            return boost::none;
        return EitherAmount (cache_->out);
    }

    bool
    redeems (ReadView const& sb, bool fwd) const override
    {
        return !ownerPaysTransferFee_;
    }

    boost::optional<Book>
    bookStepBook () const override
    {
        return book_;
    }

    boost::optional<Quality>
    qualityUpperBound(ReadView const& v, bool& redeems) const override;

    std::pair<TIn, TOut>
    revImp (
        PaymentSandbox& sb,
        ApplyView& afView,
        boost::container::flat_set<uint256>& ofrsToRm,
        TOut const& out);

    std::pair<TIn, TOut>
    fwdImp (
        PaymentSandbox& sb,
        ApplyView& afView,
        boost::container::flat_set<uint256>& ofrsToRm,
        TIn const& in);

    std::pair<bool, EitherAmount>
    validFwd (
        PaymentSandbox& sb,
        ApplyView& afView,
        EitherAmount const& in) override;

    // Check for errors frozen constraints.
    TER check(StrandContext const& ctx) const;

protected:
    std::string logStringImpl (char const* name) const
    {
        std::ostringstream ostr;
        ostr <<
            name << ": " <<
            "\ninIss: " << book_.in.account <<
            "\noutIss: " << book_.out.account <<
            "\ninCur: " << book_.in.currency <<
            "\noutCur: " << book_.out.currency;
        return ostr.str ();
    }

private:
    friend bool operator==(BookStep const& lhs, BookStep const& rhs)
    {
        return lhs.book_ == rhs.book_;
    }

    friend bool operator!=(BookStep const& lhs, BookStep const& rhs)
    {
        return ! (lhs == rhs);
    }

    bool equal (Step const& rhs) const override;

    // Iterate through the offers at the best quality in a book.
    // Unfunded offers and bad offers are skipped (and returned).
    // callback is called with the offer SLE, taker pays, taker gets.
    // If callback returns false, don't process any more offers.
    // Return the unfunded and bad offers and the number of offers consumed.
    template <class Callback>
    std::pair<boost::container::flat_set<uint256>, std::uint32_t>
    forEachOffer (
        PaymentSandbox& sb,
        ApplyView& afView,
        bool prevStepRedeems,
        Callback& callback) const;

    void consumeOffer (PaymentSandbox& sb,
        TOffer<TIn, TOut>& offer,
        TAmounts<TIn, TOut> const& ofrAmt,
        TAmounts<TIn, TOut> const& stepAmt,
        TOut const& ownerGives) const;
};

//------------------------------------------------------------------------------

// Flow is used in two different circumstances for transferring funds:
//  o Payments, and
//  o Offer crossing.
// The rules for handling funds in these two cases are almost, but not
// quite, the same.

// Payment BookStep template class (not offer crossing).
template<class TIn, class TOut>
class BookPaymentStep
    : public BookStep<TIn, TOut, BookPaymentStep<TIn, TOut>>
{
public:
    explicit BookPaymentStep() = default;

    using BookStep<TIn, TOut, BookPaymentStep<TIn, TOut>>::BookStep;
    using BookStep<TIn, TOut, BookPaymentStep<TIn, TOut>>::qualityUpperBound;

    // Never limit self cross quality on a payment.
    bool limitSelfCrossQuality (AccountID const&, AccountID const&,
        TOffer<TIn, TOut> const& offer, boost::optional<Quality>&,
        FlowOfferStream<TIn, TOut>&, bool) const
    {
        return false;
    }

    // A payment can look at offers of any quality
    bool checkQualityThreshold(TOffer<TIn, TOut> const& offer) const
    {
        return true;
    }

    // For a payment ofrInRate is always the same as trIn.
    std::uint32_t getOfrInRate (
        Step const*, TOffer<TIn, TOut> const&, std::uint32_t trIn) const
    {
        return trIn;
    }

    // For a payment ofrOutRate is always the same as trOut.
    std::uint32_t getOfrOutRate (Step const*, TOffer<TIn, TOut> const&,
        AccountID const&, std::uint32_t trOut) const
    {
        return trOut;
    }

    Quality
    qualityUpperBound(ReadView const& v,
        Quality const& ofrQ,
        bool prevStepRedeems) const
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
            prevStepRedeems ? rate(this->book_.in.account) : parityRate;
        // Always charge the transfer fee, even if the owner is the issuer
        auto const trOut =
            this->ownerPaysTransferFee_
            ? rate(this->book_.out.account)
            : parityRate;

        Quality const q1{getRate(STAmount(trOut.value), STAmount(trIn.value))};
        return composed_quality(q1, ofrQ);
    }

    std::string logString () const override
    {
        return this->logStringImpl ("BookPaymentStep");
    }
};

// Offer crossing BookStep template class (not a payment).
template<class TIn, class TOut>
class BookOfferCrossingStep
    : public BookStep<TIn, TOut, BookOfferCrossingStep<TIn, TOut>>
{
    using BookStep<TIn, TOut, BookOfferCrossingStep<TIn, TOut>>::qualityUpperBound;
private:
    // Helper function that throws if the optional passed to the constructor
    // is none.
    static Quality getQuality (boost::optional<Quality> const& limitQuality)
    {
        // It's really a programming error if the quality is missing.
        assert (limitQuality);
        if (!limitQuality)
            Throw<FlowException> (tefINTERNAL, "Offer requires quality.");
        return *limitQuality;
    }

public:
    BookOfferCrossingStep (
        StrandContext const& ctx, Issue const& in, Issue const& out)
    : BookStep<TIn, TOut, BookOfferCrossingStep<TIn, TOut>> (ctx, in, out)
    , defaultPath_ (ctx.isDefaultPath)
    , qualityThreshold_ (getQuality (ctx.limitQuality))
    {
    }

    bool limitSelfCrossQuality (AccountID const& strandSrc,
        AccountID const& strandDst, TOffer<TIn, TOut> const& offer,
        boost::optional<Quality>& ofrQ, FlowOfferStream<TIn, TOut>& offers,
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
            offers.permRmOffer (offer.key());

            // If no offers have been attempted yet then it's okay to move to
            // a different quality.
            if (!offerAttempted)
                ofrQ = boost::none;

            // Return true so the current offer will be deleted.
            return true;
        }
        return false;
    }

    // Offer crossing can prune the offers it needs to look at with a
    // quality threshold.
    bool checkQualityThreshold(TOffer<TIn, TOut> const& offer) const
    {
        return !defaultPath_ || offer.quality() >= qualityThreshold_;
    }

    // For offer crossing don't pay the transfer fee if alice is paying alice.
    // A regular (non-offer-crossing) payment does not apply this rule.
    std::uint32_t getOfrInRate (Step const* prevStep,
        TOffer<TIn, TOut> const& offer, std::uint32_t trIn) const
    {
        auto const srcAcct = prevStep ?
            prevStep->directStepSrcAcct() :
            boost::none;

        return                                        // If offer crossing
            srcAcct &&                                // && prevStep is DirectI
            offer.owner() == *srcAcct                 // && src is offer owner
            ? QUALITY_ONE : trIn;                     // then rate = QUALITY_ONE
    }

    // See comment on getOfrInRate().
    std::uint32_t getOfrOutRate (
        Step const* prevStep, TOffer<TIn, TOut> const& offer,
        AccountID const& strandDst, std::uint32_t trOut) const
    {
        return                                        // If offer crossing
            prevStep && prevStep->bookStepBook() &&   // && prevStep is BookStep
            offer.owner() == strandDst                // && dest is offer owner
            ? QUALITY_ONE : trOut;                    // then rate = QUALITY_ONE
    }

    Quality
    qualityUpperBound(ReadView const& v,
        Quality const& ofrQ,
        bool prevStepRedeems) const
    {
        // Offer x-ing does not charge a transfer fee when the offer's owner
        // is the same as the strand dst. It is important that `qualityUpperBound`
        // is an upper bound on the quality (it is used to ignore strands
        // whose quality cannot meet a minimum threshold).  When calculating
        // quality assume no fee is charged, or the estimate will no longer
        // be an upper bound.
        return ofrQ;
    }

    std::string logString () const override
    {
        return this->logStringImpl ("BookOfferCrossingStep");
    }

private:
    bool const defaultPath_;
    Quality const qualityThreshold_;
};

//------------------------------------------------------------------------------

template <class TIn, class TOut, class TDerived>
bool BookStep<TIn, TOut, TDerived>::equal (Step const& rhs) const
{
    if (auto bs = dynamic_cast<BookStep<TIn, TOut, TDerived> const*>(&rhs))
        return book_ == bs->book_;
    return false;
}

template <class TIn, class TOut, class TDerived>
boost::optional<Quality>
BookStep<TIn, TOut, TDerived>::qualityUpperBound(
    ReadView const& v, bool& redeems) const
{
    auto const prevStepRedeems = redeems;
    redeems = this->redeems(v, true);

    // This can be simplified (and sped up) if directories are never empty.
    Sandbox sb(&v, tapNONE);
    BookTip bt(sb, book_);
    if (!bt.step(j_))
        return boost::none;

    return static_cast<TDerived const*>(this)->qualityUpperBound(
        v, bt.quality(), prevStepRedeems);
}

// Adjust the offer amount and step amount subject to the given input limit
template <class TIn, class TOut>
static
void limitStepIn (Quality const& ofrQ,
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
        auto const inLmt = mulRatio (
            stpAmt.in, QUALITY_ONE, transferRateIn, /*roundUp*/ false);
        ofrAmt = ofrQ.ceil_in (ofrAmt, inLmt);
        stpAmt.out = ofrAmt.out;
        ownerGives = mulRatio (
            ofrAmt.out, transferRateOut, QUALITY_ONE, /*roundUp*/ false);
    }
}

// Adjust the offer amount and step amount subject to the given output limit
template <class TIn, class TOut>
static
void limitStepOut (Quality const& ofrQ,
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
        ownerGives = mulRatio (
            stpAmt.out, transferRateOut, QUALITY_ONE, /*roundUp*/ false);
        ofrAmt = ofrQ.ceil_out (ofrAmt, stpAmt.out);
        stpAmt.in = mulRatio (
            ofrAmt.in, transferRateIn, QUALITY_ONE, /*roundUp*/ true);
    }
}

template <class TIn, class TOut, class TDerived>
template <class Callback>
std::pair<boost::container::flat_set<uint256>, std::uint32_t>
BookStep<TIn, TOut, TDerived>::forEachOffer (
    PaymentSandbox& sb,
    ApplyView& afView,
    bool prevStepRedeems,
    Callback& callback) const
{
    // Charge the offer owner, not the sender
    // Charge a fee even if the owner is the same as the issuer
    // (the old code does not charge a fee)
    // Calculate amount that goes to the taker and the amount charged the offer owner
    auto rate = [this, &sb](AccountID const& id)->std::uint32_t
    {
        if (isXRP (id) || id == this->strandDst_)
            return QUALITY_ONE;
        return transferRate (sb, id).value;
    };

    std::uint32_t const trIn = prevStepRedeems
        ? rate (book_.in.account)
        : QUALITY_ONE;
    // Always charge the transfer fee, even if the owner is the issuer
    std::uint32_t const trOut = ownerPaysTransferFee_
        ? rate (book_.out.account)
        : QUALITY_ONE;

    typename FlowOfferStream<TIn, TOut>::StepCounter
        counter (maxOffersToConsume_, j_);

    FlowOfferStream<TIn, TOut> offers (
        sb, afView, book_, sb.parentCloseTime (), counter, j_);

    bool const flowCross = afView.rules().enabled(featureFlowCross);
    bool offerAttempted = false;
    boost::optional<Quality> ofrQ;
    while (offers.step ())
    {
        auto& offer = offers.tip ();

        // Note that offer.quality() returns a (non-optional) Quality.  So
        // ofrQ is always safe to use below this point in the loop.
        if (!ofrQ)
            ofrQ = offer.quality ();
        else if (*ofrQ != offer.quality ())
            break;

        if (static_cast<TDerived const*>(this)->limitSelfCrossQuality (
            strandSrc_, strandDst_, offer, ofrQ, offers, offerAttempted))
                continue;

        // Make sure offer owner has authorization to own IOUs from issuer.
        // An account can always own XRP or their own IOUs.
        if (flowCross &&
            (!isXRP (offer.issueIn().currency)) &&
            (offer.owner() != offer.issueIn().account))
        {
            auto const& issuerID = offer.issueIn().account;
            auto const issuer = afView.read (keylet::account (issuerID));
            if (issuer && ((*issuer)[sfFlags] & lsfRequireAuth))
            {
                // Issuer requires authorization.  See if offer owner has that.
                auto const& ownerID = offer.owner();
                auto const authFlag =
                    issuerID > ownerID ? lsfHighAuth : lsfLowAuth;

                auto const line = afView.read (keylet::line (
                    ownerID, issuerID, offer.issueIn().currency));

                if (!line || (((*line)[sfFlags] & authFlag) == 0))
                {
                    // Offer owner not authorized to hold IOU from issuer.
                    // Remove this offer even if no crossing occurs.
                    offers.permRmOffer (offer.key());
                    if (!offerAttempted)
                        // Change quality only if no previous offers were tried.
                        ofrQ = boost::none;
                    // This continue causes offers.step() to delete the offer.
                    continue;
                }
            }
        }

        if (! static_cast<TDerived const*>(this)->checkQualityThreshold(offer))
            break;

        auto const ofrInRate =
            static_cast<TDerived const*>(this)->getOfrInRate (
                prevStep_, offer, trIn);

        auto const ofrOutRate =
            static_cast<TDerived const*>(this)->getOfrOutRate (
                prevStep_, offer, strandDst_, trOut);

        auto ofrAmt = offer.amount ();
        auto stpAmt = make_Amounts (
            mulRatio (ofrAmt.in, ofrInRate, QUALITY_ONE, /*roundUp*/ true),
            ofrAmt.out);

        // owner pays the transfer fee.
        auto ownerGives =
            mulRatio (ofrAmt.out, ofrOutRate, QUALITY_ONE, /*roundUp*/ false);

        auto const funds =
            (offer.owner () == offer.issueOut ().account)
            ? ownerGives // Offer owner is issuer; they have unlimited funds
            : offers.ownerFunds ();

        if (funds < ownerGives)
        {
            // We already know offer.owner()!=offer.issueOut().account
            ownerGives = funds;
            stpAmt.out = mulRatio (
                ownerGives, QUALITY_ONE, ofrOutRate, /*roundUp*/ false);
            ofrAmt = ofrQ->ceil_out (ofrAmt, stpAmt.out);
            stpAmt.in = mulRatio (
                ofrAmt.in, ofrInRate, QUALITY_ONE, /*roundUp*/ true);
        }

        offerAttempted = true;
        if (!callback (
            offer, ofrAmt, stpAmt, ownerGives, ofrInRate, ofrOutRate))
            break;
    }

    return {offers.permToRemove (), counter.count()};
}

template <class TIn, class TOut, class TDerived>
void BookStep<TIn, TOut, TDerived>::consumeOffer (
    PaymentSandbox& sb,
    TOffer<TIn, TOut>& offer,
    TAmounts<TIn, TOut> const& ofrAmt,
    TAmounts<TIn, TOut> const& stepAmt,
    TOut const& ownerGives) const
{
    // The offer owner gets the ofrAmt. The difference between ofrAmt and stepAmt
    // is a transfer fee that goes to book_.in.account
    {
        auto const dr = accountSend (sb, book_.in.account, offer.owner (),
            toSTAmount (ofrAmt.in, book_.in), j_);
        if (dr != tesSUCCESS)
            Throw<FlowException> (dr);
    }

    // The offer owner pays `ownerGives`. The difference between ownerGives and
    // stepAmt is a transfer fee that goes to book_.out.account
    {
        auto const cr = accountSend (sb, offer.owner (), book_.out.account,
            toSTAmount (ownerGives, book_.out), j_);
        if (cr != tesSUCCESS)
            Throw<FlowException> (cr);
    }

    offer.consume (sb, ofrAmt);
}

template<class TCollection>
static
auto sum (TCollection const& col)
{
    using TResult = std::decay_t<decltype (*col.begin ())>;
    if (col.empty ())
        return TResult{beast::zero};
    return std::accumulate (col.begin () + 1, col.end (), *col.begin ());
};

template<class TIn, class TOut, class TDerived>
std::pair<TIn, TOut>
BookStep<TIn, TOut, TDerived>::revImp (
    PaymentSandbox& sb,
    ApplyView& afView,
    boost::container::flat_set<uint256>& ofrsToRm,
    TOut const& out)
{
    cache_.reset ();

    TAmounts<TIn, TOut> result (beast::zero, beast::zero);

    auto remainingOut = out;

    boost::container::flat_multiset<TIn> savedIns;
    savedIns.reserve(64);
    boost::container::flat_multiset<TOut> savedOuts;
    savedOuts.reserve(64);

    /* amt fed will be adjusted by owner funds (and may differ from the offer's
      amounts - tho always <=)
      Return true to continue to receive offers, false to stop receiving offers.
    */
    auto eachOffer =
        [&](TOffer<TIn, TOut>& offer,
            TAmounts<TIn, TOut> const& ofrAmt,
            TAmounts<TIn, TOut> const& stpAmt,
            TOut const& ownerGives,
            std::uint32_t transferRateIn,
            std::uint32_t transferRateOut) mutable -> bool
    {
        if (remainingOut <= beast::zero)
            return false;

        if (stpAmt.out <= remainingOut)
        {
            savedIns.insert(stpAmt.in);
            savedOuts.insert(stpAmt.out);
            result = TAmounts<TIn, TOut>(sum (savedIns), sum(savedOuts));
            remainingOut = out - result.out;
            this->consumeOffer (sb, offer, ofrAmt, stpAmt, ownerGives);
            // return true b/c even if the payment is satisfied,
            // we need to consume the offer
            return true;
        }
        else
        {
            auto ofrAdjAmt = ofrAmt;
            auto stpAdjAmt = stpAmt;
            auto ownerGivesAdj = ownerGives;
            limitStepOut (offer.quality (), ofrAdjAmt, stpAdjAmt, ownerGivesAdj,
                transferRateIn, transferRateOut, remainingOut);
            remainingOut = beast::zero;
            savedIns.insert (stpAdjAmt.in);
            savedOuts.insert (remainingOut);
            result.in = sum(savedIns);
            result.out = out;
            this->consumeOffer (sb, offer, ofrAdjAmt, stpAdjAmt, ownerGivesAdj);

            // When the mantissas of two iou amounts differ by less than ten, then
            // subtracting them leaves a result of zero. This can cause the check for
            // (stpAmt.out > remainingOut) to incorrectly think an offer will be funded
            // after subtracting remainingIn.
            if (fix1298(sb.parentCloseTime()))
                return offer.fully_consumed();
            else
                return false;
        }
    };

    {
        auto const prevStepRedeems = prevStep_ && prevStep_->redeems (sb, false);
        auto const r = forEachOffer (sb, afView, prevStepRedeems, eachOffer);
        boost::container::flat_set<uint256> toRm = std::move(std::get<0>(r));
        std::uint32_t const offersConsumed = std::get<1>(r);
        ofrsToRm.insert (boost::container::ordered_unique_range_t{},
            toRm.begin (), toRm.end ());

        if (offersConsumed >= maxOffersToConsume_)
        {
            // Too many iterations, mark this strand as dry
            cache_.emplace (beast::zero, beast::zero);
            return {beast::zero, beast::zero};
        }
    }

    switch(remainingOut.signum())
    {
        case -1:
        {
            // something went very wrong
            JLOG (j_.error ())
                << "BookStep remainingOut < 0 " << to_string (remainingOut);
            assert (0);
            cache_.emplace (beast::zero, beast::zero);
            return {beast::zero, beast::zero};
        }
        case 0:
        {
            // due to normalization, remainingOut can be zero without
            // result.out == out. Force result.out == out for this case
            result.out = out;
        }
    }

    cache_.emplace (result.in, result.out);
    return {result.in, result.out};
}

template<class TIn, class TOut, class TDerived>
std::pair<TIn, TOut>
BookStep<TIn, TOut, TDerived>::fwdImp (
    PaymentSandbox& sb,
    ApplyView& afView,
    boost::container::flat_set<uint256>& ofrsToRm,
    TIn const& in)
{
    assert(cache_);

    TAmounts<TIn, TOut> result (beast::zero, beast::zero);

    auto remainingIn = in;

    boost::container::flat_multiset<TIn> savedIns;
    savedIns.reserve(64);
    boost::container::flat_multiset<TOut> savedOuts;
    savedOuts.reserve(64);

    // amt fed will be adjusted by owner funds (and may differ from the offer's
    // amounts - tho always <=)
    auto eachOffer =
        [&](TOffer<TIn, TOut>& offer,
            TAmounts<TIn, TOut> const& ofrAmt,
            TAmounts<TIn, TOut> const& stpAmt,
            TOut const& ownerGives,
            std::uint32_t transferRateIn,
            std::uint32_t transferRateOut) mutable -> bool
    {
        assert(cache_);

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
            result = TAmounts<TIn, TOut>(sum (savedIns), sum(savedOuts));
            // consume the offer even if stepAmt.in == remainingIn
            processMore = true;
        }
        else
        {
            limitStepIn (offer.quality (), ofrAdjAmt, stpAdjAmt, ownerGivesAdj,
                transferRateIn, transferRateOut, remainingIn);
            savedIns.insert (remainingIn);
            lastOut = savedOuts.insert (stpAdjAmt.out);
            result.out = sum (savedOuts);
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
            auto const remainingOut = cache_->out - sum (savedOuts);
            auto ofrAdjAmtRev = ofrAmt;
            auto stpAdjAmtRev = stpAmt;
            auto ownerGivesAdjRev = ownerGives;
            limitStepOut (offer.quality (), ofrAdjAmtRev, stpAdjAmtRev,
                ownerGivesAdjRev, transferRateIn, transferRateOut,
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
                // This is (likely) a problem case, and wil be caught
                // with later checks
                savedOuts.insert (lastOutAmt);
            }
        }

        remainingIn = in - result.in;
        this->consumeOffer (sb, offer, ofrAdjAmt, stpAdjAmt, ownerGivesAdj);

        // When the mantissas of two iou amounts differ by less than ten, then
        // subtracting them leaves a result of zero. This can cause the check for
        // (stpAmt.in > remainingIn) to incorrectly think an offer will be funded
        // after subtracting remainingIn.
        if (fix1298(sb.parentCloseTime()))
            processMore = processMore || offer.fully_consumed();

        return processMore;
    };

    {
        auto const prevStepRedeems = prevStep_ && prevStep_->redeems (sb, true);
        auto const r = forEachOffer (sb, afView, prevStepRedeems, eachOffer);
        boost::container::flat_set<uint256> toRm = std::move(std::get<0>(r));
        std::uint32_t const offersConsumed = std::get<1>(r);
        ofrsToRm.insert (boost::container::ordered_unique_range_t{},
            toRm.begin (), toRm.end ());

        if (offersConsumed >= maxOffersToConsume_)
        {
            // Too many iterations, mark this strand as dry
            cache_.emplace (beast::zero, beast::zero);
            return {beast::zero, beast::zero};
        }
    }

    switch(remainingIn.signum())
    {
        case -1:
        {
            // something went very wrong
            JLOG (j_.error ())
                << "BookStep remainingIn < 0 " << to_string (remainingIn);
            assert (0);
            cache_.emplace (beast::zero, beast::zero);
            return {beast::zero, beast::zero};
        }
        case 0:
        {
            // due to normalization, remainingIn can be zero without
            // result.in == in. Force result.in == in for this case
            result.in = in;
        }
    }

    cache_.emplace (result.in, result.out);
    return {result.in, result.out};
}

template<class TIn, class TOut, class TDerived>
std::pair<bool, EitherAmount>
BookStep<TIn, TOut, TDerived>::validFwd (
    PaymentSandbox& sb,
    ApplyView& afView,
    EitherAmount const& in)
{
    if (!cache_)
    {
        JLOG (j_.trace()) << "Expected valid cache in validFwd";
        return {false, EitherAmount (TOut (beast::zero))};
    }

    auto const savCache = *cache_;

    try
    {
        boost::container::flat_set<uint256> dummy;
        fwdImp (sb, afView, dummy, get<TIn> (in));  // changes cache
    }
    catch (FlowException const&)
    {
        return {false, EitherAmount (TOut (beast::zero))};
    }

    if (!(checkNear (savCache.in, cache_->in) &&
            checkNear (savCache.out, cache_->out)))
    {
        JLOG (j_.error()) <<
            "Strand re-execute check failed." <<
            " ExpectedIn: " << to_string (savCache.in) <<
            " CachedIn: " << to_string (cache_->in) <<
            " ExpectedOut: " << to_string (savCache.out) <<
            " CachedOut: " << to_string (cache_->out);
        return {false, EitherAmount (cache_->out)};
    }
    return {true, EitherAmount (cache_->out)};
}

template<class TIn, class TOut, class TDerived>
TER
BookStep<TIn, TOut, TDerived>::check(StrandContext const& ctx) const
{
    if (book_.in == book_.out)
    {
        JLOG (j_.debug()) << "BookStep: Book with same in and out issuer " << *this;
        return temBAD_PATH;
    }
    if (!isConsistent (book_.in) || !isConsistent (book_.out))
    {
        JLOG (j_.debug()) << "Book: currency is inconsistent with issuer." << *this;
        return temBAD_PATH;
    }

    // Do not allow two books to output the same issue. This may cause offers on
    // one step to unfund offers in another step.
    if (!ctx.seenBookOuts.insert (book_.out).second ||
        ctx.seenDirectIssues[0].count (book_.out))
    {
        JLOG (j_.debug()) << "BookStep: loop detected: " << *this;
        return temBAD_PATH_LOOP;
    }

    if (ctx.view.rules().enabled(fix1373) &&
        ctx.seenDirectIssues[1].count(book_.out))
    {
        JLOG(j_.debug()) << "BookStep: loop detected: " << *this;
        return temBAD_PATH_LOOP;
    }

    if (fix1443(ctx.view.info().parentCloseTime))
    {
        if (ctx.prevStep)
        {
            if (auto const prev = ctx.prevStep->directStepSrcAcct())
            {
                auto const& view = ctx.view;
                auto const& cur = book_.in.account;

                auto sle =
                    view.read(keylet::line(*prev, cur, book_.in.currency));
                if (!sle)
                    return terNO_LINE;
                if ((*sle)[sfFlags] &
                    ((cur > *prev) ? lsfHighNoRipple : lsfLowNoRipple))
                    return terNO_RIPPLE;
            }
        }
    }

    return tesSUCCESS;
}

//------------------------------------------------------------------------------

namespace test
{
// Needed for testing

template <class TIn, class TOut, class TDerived>
static
bool equalHelper (Step const& step, ripple::Book const& book)
{
    if (auto bs = dynamic_cast<BookStep<TIn, TOut, TDerived> const*> (&step))
        return book == bs->book ();
    return false;
}

bool bookStepEqual (Step const& step, ripple::Book const& book)
{
    bool const inXRP = isXRP (book.in.currency);
    bool const outXRP = isXRP (book.out.currency);
    if (inXRP && outXRP)
        return equalHelper<XRPAmount, XRPAmount,
            BookPaymentStep<XRPAmount, XRPAmount>> (step, book);
    if (inXRP && !outXRP)
        return equalHelper<XRPAmount, IOUAmount,
            BookPaymentStep<XRPAmount, IOUAmount>> (step, book);
    if (!inXRP && outXRP)
        return equalHelper<IOUAmount, XRPAmount,
            BookPaymentStep<IOUAmount, XRPAmount>> (step, book);
    if (!inXRP && !outXRP)
        return equalHelper<IOUAmount, IOUAmount,
            BookPaymentStep<IOUAmount, IOUAmount>> (step, book);
    return false;
}
}

//------------------------------------------------------------------------------

template <class TIn, class TOut>
static
std::pair<TER, std::unique_ptr<Step>>
make_BookStepHelper (
    StrandContext const& ctx,
    Issue const& in,
    Issue const& out)
{
    TER ter = tefINTERNAL;
    std::unique_ptr<Step> r;
    if (ctx.offerCrossing)
    {
        auto offerCrossingStep =
            std::make_unique<BookOfferCrossingStep<TIn, TOut>> (ctx, in, out);
        ter = offerCrossingStep->check (ctx);
        r = std::move (offerCrossingStep);
    }
    else // payment
    {
        auto paymentStep =
            std::make_unique<BookPaymentStep<TIn, TOut>> (ctx, in, out);
        ter = paymentStep->check (ctx);
        r = std::move (paymentStep);
    }
    if (ter != tesSUCCESS)
        return {ter, nullptr};

    return {tesSUCCESS, std::move(r)};
}

std::pair<TER, std::unique_ptr<Step>>
make_BookStepII (
    StrandContext const& ctx,
    Issue const& in,
    Issue const& out)
{
    return make_BookStepHelper<IOUAmount, IOUAmount> (ctx, in, out);
}

std::pair<TER, std::unique_ptr<Step>>
make_BookStepIX (
    StrandContext const& ctx,
    Issue const& in)
{
    return make_BookStepHelper<IOUAmount, XRPAmount> (ctx, in, xrpIssue());
}

std::pair<TER, std::unique_ptr<Step>>
make_BookStepXI (
    StrandContext const& ctx,
    Issue const& out)
{
    return make_BookStepHelper<XRPAmount, IOUAmount> (ctx, xrpIssue(), out);
}

} // ripple
