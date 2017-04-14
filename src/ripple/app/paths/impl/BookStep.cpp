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

template<class TIn, class TOut>
class TOffer;

template<class TIn, class TOut>
struct TAmounts;

template<class TIn, class TOut>
class BookStep : public StepImp<TIn, TOut, BookStep<TIn, TOut>>
{
private:
    static constexpr uint32_t maxOffersToConsume_ = 2000;
    Book book_;
    AccountID strandSrc_;
    AccountID strandDst_;
    // Charge transfer fees whan the prev step redeems
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
    BookStep (Issue const& in,
        Issue const& out,
        AccountID const& strandSrc,
        AccountID const& strandDst,
        Step const* prevStep,
        bool ownerPaysTransferFee,
        beast::Journal j)
        : book_ (in, out)
        , strandSrc_ (strandSrc)
        , strandDst_ (strandDst)
        , prevStep_ (prevStep)
        , ownerPaysTransferFee_ (ownerPaysTransferFee)
        , j_ (j)
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

    void consumeOffer (PaymentSandbox& sb,
        TOffer<TIn, TOut>& offer,
        TAmounts<TIn, TOut> const& ofrAmt,
        TAmounts<TIn, TOut> const& stepAmt,
        TOut const& ownerGives) const;

    std::string logString () const override
    {
        std::ostringstream ostr;
        ostr <<
            "BookStep" <<
            "\ninIss: " << book_.in.account <<
            "\noutIss: " << book_.out.account <<
            "\ninCur: " << book_.in.currency <<
            "\noutCur: " << book_.out.currency;
        return ostr.str ();
    }
};

template <class TIn, class TOut>
bool BookStep<TIn, TOut>::equal (Step const& rhs) const
{
    if (auto bs = dynamic_cast<BookStep<TIn, TOut> const*>(&rhs))
        return book_ == bs->book_;
    return false;
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

/* Iterate through the offers at the best quality in a book.
   Unfunded offers and bad offers are skipped (and returned).
   TakerGets/Taker pays reflects funding.
   callback is called with the offer SLE, taker pays, taker gets.
   If callback returns false, don't process any more offers.
   Return the unfunded and bad offers and the number of offers consumed.
*/
template <class TAmtIn, class TAmtOut, class Callback>
static
std::pair<boost::container::flat_set<uint256>, std::uint32_t>
forEachOffer (
    PaymentSandbox& sb,
    ApplyView& afView,
    Book const& book,
    AccountID const& src,
    AccountID const& dst,
    bool prevStepRedeems,
    bool ownerPaysTransferFee,
    Callback& callback,
    std::uint32_t limit,
    beast::Journal j)
{
    // Charge the offer owner, not the sender
    // Charge a fee even if the owner is the same as the issuer
    // (the old code does not charge a fee)
    // Calculate amount that goes to the taker and the amount charged the offer owner
    auto rate = [&](AccountID const& id)->std::uint32_t
    {
        if (isXRP (id) || id == dst)
            return QUALITY_ONE;
        return transferRate (sb, id).value;
    };

    std::uint32_t const trIn = prevStepRedeems
        ? rate (book.in.account)
        : QUALITY_ONE;
    // Always charge the transfer fee, even if the owner is the issuer
    std::uint32_t const trOut = ownerPaysTransferFee
        ? rate (book.out.account)
        : QUALITY_ONE;

    typename FlowOfferStream<TAmtIn, TAmtOut>::StepCounter counter (limit, j);
    FlowOfferStream<TAmtIn, TAmtOut> offers (
        sb, afView, book, sb.parentCloseTime (), counter, j);

    boost::optional<Quality> ofrQ;
    while (offers.step ())
    {
        auto& offer = offers.tip ();
        if (!ofrQ)
            ofrQ = offer.quality ();
        else if (*ofrQ != offer.quality ())
            break;

        auto ofrAmt = offer.amount ();
        auto stpAmt = make_Amounts (
            mulRatio (ofrAmt.in, trIn, QUALITY_ONE, /*roundUp*/ true),
            ofrAmt.out);
        // owner pays the transfer fee
        auto ownerGives =
            mulRatio (ofrAmt.out, trOut, QUALITY_ONE, /*roundUp*/ false);

        auto const funds =
            (offer.owner () == offer.issueOut ().account)
            ? ownerGives // Offer owner is issuer; they have unlimited funds
            : offers.ownerFunds ();

        if (funds < ownerGives)
        {
            // We already know offer.owner()!=offer.issueOut().account
            ownerGives = funds;
            stpAmt.out = mulRatio (
                ownerGives, QUALITY_ONE, trOut, /*roundUp*/ false);
            ofrAmt = ofrQ->ceil_out (ofrAmt, stpAmt.out);
            stpAmt.in = mulRatio (
                ofrAmt.in, trIn, QUALITY_ONE, /*roundUp*/ true);
        }

        if (!callback (offer, ofrAmt, stpAmt, ownerGives, trIn, trOut))
            break;
    }

    return {offers.permToRemove (), counter.count()};
}

template <class TIn, class TOut>
void BookStep<TIn, TOut>::consumeOffer (
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

template<class TIn, class TOut>
std::pair<TIn, TOut>
BookStep<TIn, TOut>::revImp (
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
            if (amendmentRIPD1298(sb.parentCloseTime()))
                return offer.fully_consumed();
            else
                return false;
        }
    };

    {
        auto const prevStepRedeems = prevStep_ && prevStep_->redeems (sb, false);
        auto const r = forEachOffer<TIn, TOut> (sb, afView, book_, strandSrc_,
            strandDst_, prevStepRedeems, ownerPaysTransferFee_, eachOffer,
            maxOffersToConsume_, j_);
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

template<class TIn, class TOut>
std::pair<TIn, TOut>
BookStep<TIn, TOut>::fwdImp (
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
        if (amendmentRIPD1298(sb.parentCloseTime()))
            processMore = processMore || offer.fully_consumed();

        return processMore;
    };

    {
        auto const prevStepRedeems = prevStep_ && prevStep_->redeems (sb, true);
        auto const r = forEachOffer<TIn, TOut> (sb, afView, book_, strandSrc_,
            strandDst_, prevStepRedeems, ownerPaysTransferFee_, eachOffer,
            maxOffersToConsume_, j_);
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

template<class TIn, class TOut>
std::pair<bool, EitherAmount>
BookStep<TIn, TOut>::validFwd (
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

template<class TIn, class TOut>
TER
BookStep<TIn, TOut>::check(StrandContext const& ctx) const
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

    if (amendmentRIPD1443(ctx.view.info().parentCloseTime))
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

template <class TIn, class TOut>
static
bool equalHelper (Step const& step, ripple::Book const& book)
{
    if (auto bs = dynamic_cast<BookStep<TIn, TOut> const*> (&step))
        return book == bs->book ();
    return false;
}

bool bookStepEqual (Step const& step, ripple::Book const& book)
{
    bool const inXRP = isXRP (book.in.currency);
    bool const outXRP = isXRP (book.out.currency);
    if (inXRP && outXRP)
        return equalHelper<XRPAmount, XRPAmount> (step, book);
    if (inXRP && !outXRP)
        return equalHelper<XRPAmount, IOUAmount> (step, book);
    if (!inXRP && outXRP)
        return equalHelper<IOUAmount, XRPAmount> (step, book);
    if (!inXRP && !outXRP)
        return equalHelper<IOUAmount, IOUAmount> (step, book);
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
    auto r = std::make_unique<BookStep<TIn, TOut>> (
        in, out, ctx.strandSrc, ctx.strandDst, ctx.prevStep,
        ctx.ownerPaysTransferFee, ctx.j);
    auto ter = r->check (ctx);
    if (ter != tesSUCCESS)
        return {ter, nullptr};

    return {tesSUCCESS, std::move (r)};
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
    Issue out;
    return make_BookStepHelper<IOUAmount, XRPAmount> (ctx, in, out);
}

std::pair<TER, std::unique_ptr<Step>>
make_BookStepXI (
    StrandContext const& ctx,
    Issue const& out)
{
    Issue in;
    return make_BookStepHelper<XRPAmount, IOUAmount> (ctx, in, out);
}

} // ripple
