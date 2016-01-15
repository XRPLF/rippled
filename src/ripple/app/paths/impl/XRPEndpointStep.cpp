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
#include <ripple/app/paths/impl/StepChecks.h>
#include <ripple/app/paths/impl/XRPEndpointStep.h>
#include <ripple/ledger/PaymentSandbox.h>
#include <ripple/protocol/IOUAmount.h>
#include <ripple/protocol/Quality.h>


#include <numeric>
#include <sstream>

namespace ripple {

static
XRPAmount
xrpLiquid (ReadView& sb, AccountID const& src)
{
    if (auto sle = sb.read (keylet::account (src)))
    {
        auto const reserve = sb.fees ().accountReserve ((*sle)[sfOwnerCount]);
        auto const balance = (*sle)[sfBalance].xrp ();
        if (balance < reserve)
            return XRPAmount (beast::zero);
        return balance - reserve;
    }
    return XRPAmount (beast::zero);
}


std::pair<XRPAmount, XRPAmount>
XRPEndpointStep::revImp (
    PaymentSandbox& sb,
    ApplyView& afView,
    std::vector<uint256>& ofrsToRm,
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
    std::vector<uint256>& ofrsToRm,
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
        JLOG (j_.trace) << "Expected valid cache in validFwd";
        return {false, EitherAmount (XRPAmount (beast::zero))};
    }

    assert (in.native);

    auto const& xrpIn = in.xrp;
    auto const balance = xrpLiquid (sb, acc_);

    if (!isLast_ && balance < xrpIn)
    {
        JLOG (j_.trace) << "XRPEndpointStep: Strand re-execute check failed."
            << " Insufficient balance: " << to_string (balance)
            << " Requested: " << to_string (xrpIn);
        return {false, EitherAmount (balance)};
    }

    if (xrpIn != *cache_)
    {
        JLOG (j_.trace) << "XRPEndpointStep: Strand re-execute check failed."
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
        JLOG (j_.debug) << "XRPEndpointStep: specified bad account.";
        return temBAD_PATH;
    }

    auto sleAcc = ctx.view.read (keylet::account (acc_));
    if (!sleAcc)
    {
        JLOG (j_.warning) << "XRPEndpointStep: can't send or receive XRPs from "
                             "non-existent account: "
                          << acc_;
        return terNO_ACCOUNT;
    }

    auto& src = isLast_ ? xrpAccount () : acc_;
    auto& dst = isLast_ ? acc_ : xrpAccount();
    auto ter = checkFreeze (ctx.view, src, dst, xrpCurrency ());
    if (ter != tesSUCCESS)
        return ter;

    return tesSUCCESS;
}

std::pair<TER, std::unique_ptr<Step>>
make_XRPEndpointStep (
    StrandContext const& ctx,
    AccountID const& acc)
{
    auto r = std::make_unique<XRPEndpointStep> (acc, ctx.isLast, ctx.logs);
    auto ter = r->check (ctx);
    if (ter != tesSUCCESS)
        return {ter, nullptr};
    return {tesSUCCESS, std::move (r)};
}

} // ripple
