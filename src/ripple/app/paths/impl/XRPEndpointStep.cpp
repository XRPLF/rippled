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

#include <ripple/app/paths/Credit.h>
#include <ripple/app/paths/impl/AmountSpec.h>
#include <ripple/app/paths/impl/Steps.h>
#include <ripple/app/paths/impl/StepChecks.h>
#include <ripple/basics/IOUAmount.h>
#include <ripple/basics/Log.h>
#include <ripple/basics/XRPAmount.h>
#include <ripple/ledger/PaymentSandbox.h>
#include <ripple/protocol/Quality.h>

#include <boost/container/flat_set.hpp>

#include <numeric>
#include <sstream>

namespace ripple {

template <class TDerived>
class XRPEndpointStep : public StepImp<
    XRPAmount, XRPAmount, XRPEndpointStep<TDerived>>
{
private:
    AccountID acc_;
    bool const isLast_;
    beast::Journal const j_;

    // Since this step will always be an endpoint in a strand
    // (either the first or last step) the same cache is used
    // for cachedIn and cachedOut and only one will ever be used
    boost::optional<XRPAmount> cache_;

    boost::optional<EitherAmount>
    cached () const
    {
        if (!cache_)
            return boost::none;
        return EitherAmount (*cache_);
    }

public:
    XRPEndpointStep (
        StrandContext const& ctx,
        AccountID const& acc)
            : acc_(acc)
            , isLast_(ctx.isLast)
            , j_ (ctx.j) {}

    AccountID const& acc () const
    {
        return acc_;
    }

    boost::optional<std::pair<AccountID,AccountID>>
    directStepAccts () const override
    {
        if (isLast_)
            return std::make_pair(xrpAccount(), acc_);
        return std::make_pair(acc_, xrpAccount());
    }

    boost::optional<EitherAmount>
    cachedIn () const override
    {
        return cached ();
    }

    boost::optional<EitherAmount>
    cachedOut () const override
    {
        return cached ();
    }

    DebtDirection
    debtDirection(ReadView const& sb, StrandDirection dir) const override
    {
        return DebtDirection::issues;
    }

    boost::optional<Quality>
    qualityUpperBound(ReadView const& v, DebtDirection& dir) const override;

    std::pair<XRPAmount, XRPAmount>
    revImp (
        PaymentSandbox& sb,
        ApplyView& afView,
        boost::container::flat_set<uint256>& ofrsToRm,
        XRPAmount const& out);

    std::pair<XRPAmount, XRPAmount>
    fwdImp (
        PaymentSandbox& sb,
        ApplyView& afView,
        boost::container::flat_set<uint256>& ofrsToRm,
        XRPAmount const& in);

    std::pair<bool, EitherAmount>
    validFwd (
        PaymentSandbox& sb,
        ApplyView& afView,
        EitherAmount const& in) override;

    // Check for errors and violations of frozen constraints.
    TER check (StrandContext const& ctx) const;

protected:
    XRPAmount
    xrpLiquidImpl (ReadView& sb, std::int32_t reserveReduction) const
    {
        return ripple::xrpLiquid (sb, acc_, reserveReduction, j_);
    }

    std::string logStringImpl (char const* name) const
    {
        std::ostringstream ostr;
        ostr <<
            name << ": " <<
            "\nAcc: " << acc_;
        return ostr.str ();
    }

private:
    template <class P>
    friend bool operator==(
        XRPEndpointStep<P> const& lhs,
        XRPEndpointStep<P> const& rhs);

    friend bool operator!=(
        XRPEndpointStep const& lhs,
        XRPEndpointStep const& rhs)
    {
        return ! (lhs == rhs);
    }

    bool equal (Step const& rhs) const override
    {
        if (auto ds = dynamic_cast<XRPEndpointStep const*> (&rhs))
        {
            return *this == *ds;
        }
        return false;
    }
};

//------------------------------------------------------------------------------

// Flow is used in two different circumstances for transferring funds:
//  o Payments, and
//  o Offer crossing.
// The rules for handling funds in these two cases are almost, but not
// quite, the same.

// Payment XRPEndpointStep class (not offer crossing).
class XRPEndpointPaymentStep : public XRPEndpointStep<XRPEndpointPaymentStep>
{
public:
    using XRPEndpointStep<XRPEndpointPaymentStep>::XRPEndpointStep;

    XRPAmount
    xrpLiquid (ReadView& sb) const
    {
        return xrpLiquidImpl (sb, 0);;
    }

    std::string logString () const override
    {
        return logStringImpl ("XRPEndpointPaymentStep");
    }
};

// Offer crossing XRPEndpointStep class (not a payment).
class XRPEndpointOfferCrossingStep :
    public XRPEndpointStep<XRPEndpointOfferCrossingStep>
{
private:

    // For historical reasons, offer crossing is allowed to dig further
    // into the XRP reserve than an ordinary payment.  (I believe it's
    // because the trust line was created after the XRP was removed.)
    // Return how much the reserve should be reduced.
    //
    // Note that reduced reserve only happens if the trust line does not
    // currently exist.
    static std::int32_t computeReserveReduction (
        StrandContext const& ctx, AccountID const& acc)
    {
        if (ctx.isFirst &&
            !ctx.view.read (keylet::line (acc, ctx.strandDeliver)))
                return -1;
        return 0;
    }

public:
    XRPEndpointOfferCrossingStep (
        StrandContext const& ctx, AccountID const& acc)
    : XRPEndpointStep<XRPEndpointOfferCrossingStep> (ctx, acc)
    , reserveReduction_ (computeReserveReduction (ctx, acc))
    {
    }

    XRPAmount
    xrpLiquid (ReadView& sb) const
    {
        return xrpLiquidImpl (sb, reserveReduction_);
    }

    std::string logString () const override
    {
        return logStringImpl ("XRPEndpointOfferCrossingStep");
    }

private:
    std::int32_t const reserveReduction_;
};

//------------------------------------------------------------------------------

template <class TDerived>
inline bool operator==(XRPEndpointStep<TDerived> const& lhs,
    XRPEndpointStep<TDerived> const& rhs)
{
    return lhs.acc_ == rhs.acc_ && lhs.isLast_ == rhs.isLast_;
}

template <class TDerived>
boost::optional<Quality>
XRPEndpointStep<TDerived>::qualityUpperBound(
    ReadView const& v, DebtDirection& dir) const
{
    dir = this->debtDirection(v, StrandDirection::forward);
    return Quality{STAmount::uRateOne};
}


template <class TDerived>
std::pair<XRPAmount, XRPAmount>
XRPEndpointStep<TDerived>::revImp (
    PaymentSandbox& sb,
    ApplyView& afView,
    boost::container::flat_set<uint256>& ofrsToRm,
    XRPAmount const& out)
{
    auto const balance = static_cast<TDerived const*>(this)->xrpLiquid (sb);

    auto const result = isLast_ ? out : std::min (balance, out);

    auto& sender = isLast_ ? xrpAccount() : acc_;
    auto& receiver = isLast_ ? acc_ : xrpAccount();
    auto ter   = accountSend (sb, sender, receiver, toSTAmount (result), j_);
    if (ter != tesSUCCESS)
        return {XRPAmount{beast::zero}, XRPAmount{beast::zero}};

    cache_.emplace (result);
    return {result, result};
}

template <class TDerived>
std::pair<XRPAmount, XRPAmount>
XRPEndpointStep<TDerived>::fwdImp (
    PaymentSandbox& sb,
    ApplyView& afView,
    boost::container::flat_set<uint256>& ofrsToRm,
    XRPAmount const& in)
{
    assert (cache_);
    auto const balance = static_cast<TDerived const*>(this)->xrpLiquid (sb);

    auto const result = isLast_ ? in : std::min (balance, in);

    auto& sender = isLast_ ? xrpAccount() : acc_;
    auto& receiver = isLast_ ? acc_ : xrpAccount();
    auto ter   = accountSend (sb, sender, receiver, toSTAmount (result), j_);
    if (ter != tesSUCCESS)
        return {XRPAmount{beast::zero}, XRPAmount{beast::zero}};

    cache_.emplace (result);
    return {result, result};
}

template <class TDerived>
std::pair<bool, EitherAmount>
XRPEndpointStep<TDerived>::validFwd (
    PaymentSandbox& sb,
    ApplyView& afView,
    EitherAmount const& in)
{
    if (!cache_)
    {
        JLOG (j_.error()) << "Expected valid cache in validFwd";
        return {false, EitherAmount (XRPAmount (beast::zero))};
    }

    assert (in.native);

    auto const& xrpIn = in.xrp;
    auto const balance = static_cast<TDerived const*>(this)->xrpLiquid (sb);

    if (!isLast_ && balance < xrpIn)
    {
        JLOG (j_.error()) << "XRPEndpointStep: Strand re-execute check failed."
            << " Insufficient balance: " << to_string (balance)
            << " Requested: " << to_string (xrpIn);
        return {false, EitherAmount (balance)};
    }

    if (xrpIn != *cache_)
    {
        JLOG (j_.error()) << "XRPEndpointStep: Strand re-execute check failed."
            << " ExpectedIn: " << to_string (*cache_)
            << " CachedIn: " << to_string (xrpIn);
    }
    return {true, in};
}

template <class TDerived>
TER
XRPEndpointStep<TDerived>::check (StrandContext const& ctx) const
{
    if (!acc_)
    {
        JLOG (j_.debug()) << "XRPEndpointStep: specified bad account.";
        return temBAD_PATH;
    }

    auto sleAcc = ctx.view.read (keylet::account (acc_));
    if (!sleAcc)
    {
        JLOG (j_.warn()) << "XRPEndpointStep: can't send or receive XRP from "
                             "non-existent account: "
                          << acc_;
        return terNO_ACCOUNT;
    }

    if (!ctx.isFirst && !ctx.isLast)
    {
        return temBAD_PATH;
    }

    auto& src = isLast_ ? xrpAccount () : acc_;
    auto& dst = isLast_ ? acc_ : xrpAccount();
    auto ter = checkFreeze (ctx.view, src, dst, xrpCurrency ());
    if (ter != tesSUCCESS)
        return ter;

    return tesSUCCESS;
}

//------------------------------------------------------------------------------

namespace test
{
// Needed for testing
bool xrpEndpointStepEqual (Step const& step, AccountID const& acc)
{
    if (auto xs =
        dynamic_cast<XRPEndpointStep<XRPEndpointPaymentStep> const*> (&step))
    {
        return xs->acc () == acc;
    }
    return false;
}
}

//------------------------------------------------------------------------------

std::pair<TER, std::unique_ptr<Step>>
make_XRPEndpointStep (
    StrandContext const& ctx,
    AccountID const& acc)
{
    TER ter = tefINTERNAL;
    std::unique_ptr<Step> r;
    if (ctx.offerCrossing)
    {
        auto offerCrossingStep =
            std::make_unique<XRPEndpointOfferCrossingStep> (ctx, acc);
        ter = offerCrossingStep->check (ctx);
        r = std::move (offerCrossingStep);
    }
    else // payment
    {
        auto paymentStep =
            std::make_unique<XRPEndpointPaymentStep> (ctx, acc);
        ter = paymentStep->check (ctx);
        r = std::move (paymentStep);
    }
    if (ter != tesSUCCESS)
        return {ter, nullptr};

    return {tesSUCCESS, std::move (r)};
}

} // ripple
