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
#include <ripple/app/paths/Credit.h>
#include <ripple/app/paths/NodeDirectory.h>
#include <ripple/app/paths/impl/BookStep.h>
#include <ripple/app/tx/impl/OfferStream.h>
#include <ripple/basics/contract.h>
#include <ripple/ledger/Directory.h>
#include <ripple/ledger/PaymentSandbox.h>
#include <ripple/protocol/IOUAmount.h>
#include <ripple/protocol/Quality.h>
#include <ripple/protocol/XRPAmount.h>

#include <numeric>
#include <sstream>

namespace ripple {

// Adjust the offer amount and step amount subject to the given input limit
template <class TIn, class TOut>
void limitStepIn (Quality const& ofrQ,
    TAmounts<TIn, TOut>& ofrAmt,
    TAmounts<TIn, TOut>& stpAmt,
    std::uint32_t transferRateIn,
    TIn const& limit)
{
    if (limit < stpAmt.in)
    {
        stpAmt.in = limit;
        auto const inLmt = mulRatio (
            stpAmt.in, QUALITY_ONE, transferRateIn, /*roundUp*/ false);
        ofrAmt = ofrQ.ceil_in (ofrAmt, inLmt);
        stpAmt.out = ofrAmt.out;
    }
}

// Adjust the offer amount and step amount subject to the given output limit
template <class TIn, class TOut>
void limitStepOut (Quality const& ofrQ,
    TAmounts<TIn, TOut>& ofrAmt,
    TAmounts<TIn, TOut>& stpAmt,
    std::uint32_t transferRateIn,
    TOut const& limit)
{
    if (limit < stpAmt.out)
    {
        stpAmt.out = limit;
        ofrAmt = ofrQ.ceil_out (ofrAmt, limit);
        stpAmt.in = mulRatio (
            ofrAmt.in, transferRateIn, QUALITY_ONE, /*roundUp*/ true);
    }
}

// Iterate through the offers at the best quality in a book
// Unfunded offers and bad offers are skipped (and returned)
// TakerGets/Taker pays reflects funding
// callback is called with the offer SLE, taker pays, taker gets
// If callback returns false, don't process any more offers
template <class TAmtIn, class TAmtOut, class Callback>
std::vector<uint256> forEachOffer (
    PaymentSandbox& sb,
    ApplyView& afView,
    Book const& book,
    AccountID const& src,
    AccountID const& dst,
    Callback& callback,
    Logs& l)
{
    auto j = l.journal ("Flow");
    auto transferRate = [&](AccountID const& id)->std::uint32_t
    {
        if (isXRP (id) || id == src || id == dst)
            return QUALITY_ONE;
        return rippleTransferRate (sb, id);
    };

    std::uint32_t const trIn = transferRate (book.in.account);

    typename FlowOfferStream<TAmtIn, TAmtOut>::StepCounter counter (1000, j);
    FlowOfferStream<TAmtIn, TAmtOut> offers (
        sb, afView, book, sb.parentCloseTime (), counter, j);

    boost::optional<Quality> ofrQ;
    while (offers.step (l))
    {
        auto& offer = offers.tip ();
        if (!ofrQ)
            ofrQ = offer.quality ();
        else if (*ofrQ != offer.quality ())
            break;

        auto const funds = offers.ownerFunds ();
        auto ofrAmt = offer.amount ();
        auto stpAmt = make_Amounts (
            mulRatio (ofrAmt.in, trIn, QUALITY_ONE, /*roundUp*/ true),
            ofrAmt.out);

        if (funds < stpAmt.out)
            limitStepOut (*ofrQ, ofrAmt, stpAmt, trIn, funds);

        if (!callback (offer, ofrAmt, stpAmt, trIn))
            break;
    }

    return offers.toRemove ();
}

template <class TIn, class TOut>
void BookStep<TIn, TOut>::consumeOffer (
    PaymentSandbox& sb,
    TOffer<TIn, TOut>& offer,
    TAmounts<TIn, TOut> const& ofrAmt,
    TAmounts<TIn, TOut> const& stepAmt) const
{
    auto viewJ = l_.journal ("View");

    // The offer owner gets the ofrAmt. The difference between ofrAmt and stepAmt
    // is a transfer fee that goes to book_.in.account
    auto const dr = accountSend (
        sb, book_.in.account, offer.owner (), toSTAmount (ofrAmt.in, book_.in), viewJ);
    if (dr != tesSUCCESS)
        Throw<StepError> (dr);

    auto const cr = accountSend (
        sb, offer.owner (), book_.out.account, toSTAmount (stepAmt.out, book_.out), viewJ);
    if (cr != tesSUCCESS)
        Throw<StepError> (cr);

    offer.consume (sb, ofrAmt);
}

template<class TCollection>
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
    std::vector<uint256>& ofrsToRm,
    TOut const& out)
{
    cache_.reset ();

    TAmounts<TIn, TOut> result (beast::zero, beast::zero);

    auto remainingOut = out;

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
            std::uint32_t transferRateIn) mutable -> bool
    {
        if (remainingOut <= beast::zero)
            return false;

        if (stpAmt.out <= remainingOut)
        {
            savedIns.insert(stpAmt.in);
            savedOuts.insert(stpAmt.out);
            result = TAmounts<TIn, TOut>(sum (savedIns), sum(savedOuts));
            remainingOut = out - result.out;
            this->consumeOffer (sb, offer, ofrAmt, stpAmt);
            // return true b/c even if the payment is satisfied,
            // we need to consume the offer
            return true;
        }
        else
        {
            auto ofrAdjAmt = ofrAmt;
            auto stpAdjAmt = stpAmt;
            limitStepOut (
                offer.quality (), ofrAdjAmt, stpAdjAmt, transferRateIn, remainingOut);
            remainingOut = beast::zero;
            savedIns.insert (stpAdjAmt.in);
            savedOuts.insert (remainingOut);
            result.in = sum(savedIns);
            result.out = out;
            this->consumeOffer (sb, offer, ofrAdjAmt, stpAdjAmt);
            return false;
        }
    };

    {
        auto const toRm = forEachOffer<TIn, TOut> (
            sb, afView, book_, strandSrc_, strandDst_, eachOffer, l_);
        ofrsToRm.reserve (ofrsToRm.size () + toRm.size ());
        for (auto& o : toRm)
            ofrsToRm.emplace_back (std::move (o));
    }

    if (remainingOut < beast::zero)
    {
        // something went very wrong
        assert (0);
        cache_.emplace (beast::zero, beast::zero);
        return {beast::zero, beast::zero};
    }

    cache_.emplace (result.in, result.out);
    return {result.in, result.out};
}

template<class TIn, class TOut>
std::pair<TIn, TOut>
BookStep<TIn, TOut>::fwdImp (
    PaymentSandbox& sb,
    ApplyView& afView,
    std::vector<uint256>& ofrsToRm,
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
            std::uint32_t transferRateIn) mutable -> bool
    {
        if (remainingIn <= beast::zero)
            return false;

        if (stpAmt.in <= remainingIn)
        {
            savedIns.insert(stpAmt.in);
            savedOuts.insert(stpAmt.out);
            result = TAmounts<TIn, TOut>(sum (savedIns), sum(savedOuts));
            remainingIn = in - result.in;
            this->consumeOffer (sb, offer, ofrAmt, stpAmt);
            // return true b/c even if the payment is satisfied,
            // we need to consume the offer
            return true;
        }
        else
        {
            auto ofrAdjAmt = ofrAmt;
            auto stpAdjAmt = stpAmt;
            limitStepIn (
                offer.quality (), ofrAdjAmt, stpAdjAmt, transferRateIn, remainingIn);
            savedIns.insert (remainingIn);
            savedOuts.insert (stpAdjAmt.out);
            remainingIn = beast::zero;
            result.out = sum (savedOuts);
            result.in = in;
            this->consumeOffer (sb, offer, ofrAdjAmt, stpAdjAmt);
            return false;
        }
    };

    {
        auto const toRm = forEachOffer<TIn, TOut> (
            sb, afView, book_, strandSrc_, strandDst_, eachOffer, l_);
        ofrsToRm.reserve (ofrsToRm.size () + toRm.size ());
        for (auto& o : toRm)
            ofrsToRm.emplace_back (std::move (o));
    }

    if (remainingIn < beast::zero)
    {
        // something went very wrong
        assert (0);
        cache_.emplace (beast::zero, beast::zero);
        return {beast::zero, beast::zero};
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
        JLOG (j_.trace) << "Expected valid cache in validFwd";
        return {false, EitherAmount (TOut (beast::zero))};
    }


    auto const savCache = *cache_;

    try
    {
        std::vector<uint256> dummy;
        fwdImp (sb, afView, dummy, get<TIn> (in));  // changes cache
    }
    catch (StepError const&)
    {
        return {false, EitherAmount (TOut (beast::zero))};
    }

    if (!(checkEqual (savCache.in, cache_->in) &&
            checkEqual (savCache.out, cache_->out)))
    {
        JLOG (j_.trace) <<
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
        JLOG (j_.debug) << "BookStep: Book with same in and out issuer " << *this;
        return temBAD_PATH;
    }
    if (!isConsistent (book_.in) || !isConsistent (book_.out))
    {
        JLOG (j_.debug) << "Book: currency is inconsistent with issuer." << *this;
        return temBAD_PATH;
    }
    if (!ctx.seenBooks.insert (book_).second)
    {
        JLOG (j_.debug) << "BookStep: loop detected: " << *this;
        return temBAD_PATH_LOOP;
    }
    return tesSUCCESS;
}

//------------------------------------------------------------------------------

template <class TIn, class TOut>
std::pair<TER, std::unique_ptr<Step>>
make_BookStepHelper (
    StrandContext const& ctx,
    Issue const& in,
    Issue const& out)
{
    auto r = std::make_unique<BookStep<TIn, TOut>> (
        in, out, ctx.strandSrc, ctx.strandDst, ctx.logs);
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
