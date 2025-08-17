//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2024 Ripple Labs Inc.

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

#ifndef RIPPLE_APP_PATHS_DETAIL_AMMCONLIQUIDITYSTEP_H_INCLUDED
#define RIPPLE_APP_PATHS_DETAIL_AMMCONLIQUIDITYSTEP_H_INCLUDED

#include <xrpld/app/paths/AMMConLiquidityPool.h>
#include <xrpld/app/paths/AMMConLiquidityOffer.h>
#include <xrpld/app/paths/detail/Steps.h>
#include <xrpld/app/tx/detail/OfferStream.h>
#include <xrpld/ledger/PaymentSandbox.h>

#include <xrpl/basics/Log.h>
#include <xrpl/protocol/Book.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/IOUAmount.h>
#include <xrpl/protocol/Quality.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/protocol/STAmount.h>
#include <functional>

#include <boost/container/flat_set.hpp>

namespace ripple {

template <typename TIn, typename TOut>
using TAmounts = std::pair<TIn, TOut>;

template <class TIn, class TOut, class TDerived>
class AMMConLiquidityStep : public StepImp<TIn, TOut, AMMConLiquidityStep<TIn, TOut, TDerived>>
{
protected:
    enum class OfferType { ConcentratedLiquidity, CLOB };

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
    */
    std::uint32_t offersUsed_ = 0;
    // If set, concentrated liquidity might be available
    // if concentrated liquidity offer quality is better than CLOB offer
    // quality or there is no CLOB offer.
    std::optional<AMMConLiquidityPool<TIn, TOut>> ammConLiquidity_;
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
    AMMConLiquidityStep(StrandContext const& ctx, Issue const& in, Issue const& out)
        : maxOffersToConsume_(getMaxOffersToConsume(ctx))
        , book_(in, out, ctx.domainID)
        , strandSrc_(ctx.strandSrc)
        , strandDst_(ctx.strandDst)
        , prevStep_(ctx.prevStep)
        , ownerPaysTransferFee_(ctx.ownerPaysTransferFee)
        , j_(ctx.j)
    {
        // Check if concentrated liquidity is available for this asset pair
        if (auto const ammSle = ctx.view.read(keylet::amm(in, out));
            ammSle && ammSle->getFieldAmount(sfLPTokenBalance) != beast::zero)
        {
            // Check if concentrated liquidity feature is enabled
            if (ctx.view.rules().enabled(featureAMMConcentratedLiquidity))
            {
                ammConLiquidity_.emplace(
                    ctx.view,
                    (*ammSle)[sfAccount],
                    getTradingFee(ctx.view, *ammSle, ctx.ammContext.account()),
                    in,
                    out,
                    ctx.ammContext,
                    ctx.j);
            }
        }
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

    std::optional<AMMConLiquidityOffer<TIn, TOut>>
    getAMMConLiquidityOffer(
        ReadView const& view,
        std::optional<Quality> const& clobQuality) const;

    template <template <typename, typename> typename Offer>
    void
    consumeOffer(
        PaymentSandbox& sb,
        Offer<TIn, TOut>& offer,
        TAmounts<TIn, TOut> const& ofrAmt,
        TAmounts<TIn, TOut> const& stepAmt,
        TOut const& ownerGives) const;

    template <template <typename, typename> typename Offer>
    bool
    execOffer(
        PaymentSandbox& sb,
        Offer<TIn, TOut>& offer,
        TAmounts<TIn, TOut> const& ofrAmt,
        TAmounts<TIn, TOut> const& stepAmt,
        TOut const& ownerGives,
        std::function<bool(
            Offer<TIn, TOut>&,
            TAmounts<TIn, TOut> const&,
            TAmounts<TIn, TOut> const&,
            TOut const&,
            std::uint32_t,
            std::uint32_t)> const& callback) const;
};

}  // namespace ripple

#endif  // RIPPLE_APP_PATHS_DETAIL_AMMCONLIQUIDITYSTEP_H_INCLUDED
