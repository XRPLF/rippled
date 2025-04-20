//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2014 Ripple Labs Inc.

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

#include <xrpld/app/main/Application.h>
#include <xrpld/app/misc/NetworkOPs.h>
#include <xrpld/ledger/ReadView.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/RPCHelpers.h>

#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/resource/Fees.h>

namespace ripple {

Json::Value
doOptionBookOffers(RPC::JsonContext& context)
{
    // VFALCO TODO Here is a terrible place for this kind of business
    //             logic. It needs to be moved elsewhere and documented,
    //             and encapsulated into a function.
    if (context.app.getJobQueue().getJobCountGE(jtCLIENT) > 200)
        return rpcError(rpcTOO_BUSY);

    std::shared_ptr<ReadView const> lpLedger;
    auto jvResult = RPC::lookupLedger(lpLedger, context);

    if (!lpLedger)
        return jvResult;

    if (!context.params.isMember(jss::strike_price))
        return RPC::missing_field_error(jss::strike_price);

    if (!context.params.isMember(jss::asset))
        return RPC::missing_field_error(jss::asset);

    Json::Value const& strike_price = context.params[jss::strike_price];

    if (!strike_price.isObjectOrNull())
        return RPC::object_field_error(jss::strike_price);

    STAmount st_strike_price;
    if (strike_price.isMember(jss::value))
    {
        if (!strike_price[jss::value].isString())
            return RPC::expected_field_error("strike_price.value", "string");

        if (!amountFromJsonNoThrow(st_strike_price, strike_price))
        {
            return RPC::invalid_field_error(jss::value);
        }
    }

    Json::Value const& asset = context.params[jss::asset];
    if (!asset.isObjectOrNull())
        return RPC::object_field_error(jss::asset);

    Currency currency;

    if (!to_currency(currency, asset[jss::currency].asString()))
    {
        JLOG(context.j.info()) << "Bad asset currency.";
        return RPC::make_error(
            rpcSRC_CUR_MALFORMED,
            "Invalid field 'asset.currency', bad currency.");
    }

    AccountID issuer;

    if (asset.isMember(jss::issuer))
    {
        if (!asset[jss::issuer].isString())
            return RPC::expected_field_error("asset.issuer", "string");

        if (!to_issuer(issuer, asset[jss::issuer].asString()))
            return RPC::make_error(
                rpcSRC_ISR_MALFORMED,
                "Invalid field 'asset.issuer', bad issuer.");

        if (issuer == noAccount())
            return RPC::make_error(
                rpcSRC_ISR_MALFORMED,
                "Invalid field 'asset.issuer', bad issuer account one.");
    }
    else
    {
        issuer = xrpAccount();
    }

    if (isXRP(currency) && !isXRP(issuer))
        return RPC::make_error(
            rpcSRC_ISR_MALFORMED,
            "Unneeded field 'taker_pays.issuer' for "
            "XRP currency specification.");

    if (!isXRP(currency) && isXRP(issuer))
        return RPC::make_error(
            rpcSRC_ISR_MALFORMED,
            "Invalid field 'taker_pays.issuer', expected non-XRP issuer.");

    std::optional<std::uint32_t> expiration;
    if (context.params.isMember(jss::expiration))
    {
        if (!context.params[jss::expiration].isString())
            return RPC::expected_field_error(jss::expiration, "string");

        expiration = context.params[jss::expiration].asInt();
        if (!expiration)
            return RPC::invalid_field_error(jss::expiration);
    }
    unsigned int limit;
    if (auto err = readLimitField(limit, RPC::Tuning::bookOffers, context))
        return *err;

    Json::Value const jvMarker(
        context.params.isMember(jss::marker) ? context.params[jss::marker]
                                             : Json::Value(Json::nullValue));

    std::int64_t const strike =
        static_cast<std::int64_t>(Number(st_strike_price));
    context.netOps.getOptionBookPage(
        lpLedger,
        Issue(currency, issuer),
        strike,
        expiration ? *expiration : 0,
        limit,
        jvMarker,
        jvResult);

    context.loadType = Resource::feeMediumBurdenRPC;

    return jvResult;
}

}  // namespace ripple
