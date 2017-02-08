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
#include <ripple/app/paths/impl/AmountSpec.h>
#include <ripple/app/paths/impl/Steps.h>
#include <ripple/app/paths/impl/StepChecks.h>
#include <ripple/basics/Log.h>
#include <ripple/ledger/PaymentSandbox.h>
#include <ripple/protocol/IOUAmount.h>
#include <ripple/protocol/Quality.h>
#include <ripple/protocol/XRPAmount.h>

#include <boost/container/flat_set.hpp>

#include <numeric>
#include <sstream>

namespace ripple {

class XRPEndpointStep : public StepImp<XRPAmount, XRPAmount, XRPEndpointStep>
{
  private:
    AccountID acc_;
    bool isLast_;
    beast::Journal j_;

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
        AccountID const& acc,
        bool isLast,
        beast::Journal j)
            :acc_(acc)
            , isLast_(isLast)
            , j_ (j) {}

    AccountID const& acc () const
    {
        return acc_;
    };

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

private:
    friend bool operator==(XRPEndpointStep const& lhs, XRPEndpointStep const& rhs);

    friend bool operator!=(XRPEndpointStep const& lhs, XRPEndpointStep const& rhs)
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

    std::string logString () const override
    {
        std::ostringstream ostr;
        ostr <<
            "XRPEndpointStep: " <<
            "\nAcc: " << acc_;
        return ostr.str ();
    }
};

inline bool operator==(XRPEndpointStep const& lhs, XRPEndpointStep const& rhs)
{
    return lhs.acc_ == rhs.acc_ && lhs.isLast_ == rhs.isLast_;
}

static
XRPAmount
xrpLiquid (ReadView& sb, AccountID const& src)
{
    return accountHolds(
        sb, src, xrpCurrency(), xrpAccount(), fhIGNORE_FREEZE, {}).xrp();
}


std::pair<XRPAmount, XRPAmount>
XRPEndpointStep::revImp (
    PaymentSandbox& sb,
    ApplyView& afView,
    boost::container::flat_set<uint256>& ofrsToRm,
    XRPAmount const& out)
{
    auto const balance = xrpLiquid (sb, acc_);
    auto const result = isLast_ ? out : std::min (balance, out);

    auto& sender = isLast_ ? xrpAccount() : acc_;
    auto& receiver = isLast_ ? acc_ : xrpAccount();
    auto ter   = accountSend (sb, sender, receiver, toSTAmount (result), j_);
    if (ter != tesSUCCESS)
        return {XRPAmount{beast::zero}, XRPAmount{beast::zero}};

    cache_.emplace (result);
    return {result, result};
}

std::pair<XRPAmount, XRPAmount>
XRPEndpointStep::fwdImp (
    PaymentSandbox& sb,
    ApplyView& afView,
    boost::container::flat_set<uint256>& ofrsToRm,
    XRPAmount const& in)
{
    assert (cache_);
    auto const balance = xrpLiquid (sb, acc_);
    auto const result = isLast_ ? in : std::min (balance, in);

    auto& sender = isLast_ ? xrpAccount() : acc_;
    auto& receiver = isLast_ ? acc_ : xrpAccount();
    auto ter   = accountSend (sb, sender, receiver, toSTAmount (result), j_);
    if (ter != tesSUCCESS)
        return {XRPAmount{beast::zero}, XRPAmount{beast::zero}};

    cache_.emplace (result);
    return {result, result};
}

std::pair<bool, EitherAmount>
XRPEndpointStep::validFwd (
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
    auto const balance = xrpLiquid (sb, acc_);

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

TER
XRPEndpointStep::check (StrandContext const& ctx) const
{
    if (!acc_)
    {
        JLOG (j_.debug()) << "XRPEndpointStep: specified bad account.";
        return temBAD_PATH;
    }

    auto sleAcc = ctx.view.read (keylet::account (acc_));
    if (!sleAcc)
    {
        JLOG (j_.warn()) << "XRPEndpointStep: can't send or receive XRPs from "
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
    if (auto xs = dynamic_cast<XRPEndpointStep const*> (&step))
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
    auto r = std::make_unique<XRPEndpointStep> (acc, ctx.isLast, ctx.j);
    auto ter = r->check (ctx);
    if (ter != tesSUCCESS)
        return {ter, nullptr};
    return {tesSUCCESS, std::move (r)};
}

} // ripple
