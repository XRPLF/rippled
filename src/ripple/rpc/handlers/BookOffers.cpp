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

#include <ripple/app/main/Application.h>
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/basics/Log.h>
#include <ripple/ledger/ReadView.h>
#include <ripple/net/RPCErr.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/UintTypes.h>
#include <ripple/protocol/jss.h>
#include <ripple/resource/Fees.h>
#include <ripple/rpc/Context.h>
#include <ripple/rpc/impl/RPCHelpers.h>

namespace ripple {

Json::Value
doBookOffers(RPC::JsonContext& context)
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

    if (!context.params.isMember(jss::taker_pays))
        return RPC::missing_field_error(jss::taker_pays);

    if (!context.params.isMember(jss::taker_gets))
        return RPC::missing_field_error(jss::taker_gets);

    Json::Value const& taker_pays = context.params[jss::taker_pays];
    Json::Value const& taker_gets = context.params[jss::taker_gets];

    if (!taker_pays.isObjectOrNull())
        return RPC::object_field_error(jss::taker_pays);

    if (!taker_gets.isObjectOrNull())
        return RPC::object_field_error(jss::taker_gets);

    if (!taker_pays.isMember(jss::currency))
        return RPC::missing_field_error("taker_pays.currency");

    if (!taker_pays[jss::currency].isString())
        return RPC::expected_field_error("taker_pays.currency", "string");

    if (!taker_gets.isMember(jss::currency))
        return RPC::missing_field_error("taker_gets.currency");

    if (!taker_gets[jss::currency].isString())
        return RPC::expected_field_error("taker_gets.currency", "string");

    Currency pay_currency;

    if (!to_currency(pay_currency, taker_pays[jss::currency].asString()))
    {
        JLOG(context.j.info()) << "Bad taker_pays currency.";
        return RPC::make_error(
            rpcSRC_CUR_MALFORMED,
            "Invalid field 'taker_pays.currency', bad currency.");
    }

    Currency get_currency;

    if (!to_currency(get_currency, taker_gets[jss::currency].asString()))
    {
        JLOG(context.j.info()) << "Bad taker_gets currency.";
        return RPC::make_error(
            rpcDST_AMT_MALFORMED,
            "Invalid field 'taker_gets.currency', bad currency.");
    }

    AccountID pay_issuer;

    if (taker_pays.isMember(jss::issuer))
    {
        if (!taker_pays[jss::issuer].isString())
            return RPC::expected_field_error("taker_pays.issuer", "string");

        if (!to_issuer(pay_issuer, taker_pays[jss::issuer].asString()))
            return RPC::make_error(
                rpcSRC_ISR_MALFORMED,
                "Invalid field 'taker_pays.issuer', bad issuer.");

        if (pay_issuer == noAccount())
            return RPC::make_error(
                rpcSRC_ISR_MALFORMED,
                "Invalid field 'taker_pays.issuer', bad issuer account one.");
    }
    else
    {
        pay_issuer = xrpAccount();
    }

    if (isXRP(pay_currency) && !isXRP(pay_issuer))
        return RPC::make_error(
            rpcSRC_ISR_MALFORMED,
            "Unneeded field 'taker_pays.issuer' for "
            "XRP currency specification.");

    if (!isXRP(pay_currency) && isXRP(pay_issuer))
        return RPC::make_error(
            rpcSRC_ISR_MALFORMED,
            "Invalid field 'taker_pays.issuer', expected non-XRP issuer.");

    AccountID get_issuer;

    if (taker_gets.isMember(jss::issuer))
    {
        if (!taker_gets[jss::issuer].isString())
            return RPC::expected_field_error("taker_gets.issuer", "string");

        if (!to_issuer(get_issuer, taker_gets[jss::issuer].asString()))
            return RPC::make_error(
                rpcDST_ISR_MALFORMED,
                "Invalid field 'taker_gets.issuer', bad issuer.");

        if (get_issuer == noAccount())
            return RPC::make_error(
                rpcDST_ISR_MALFORMED,
                "Invalid field 'taker_gets.issuer', bad issuer account one.");
    }
    else
    {
        get_issuer = xrpAccount();
    }

    if (isXRP(get_currency) && !isXRP(get_issuer))
        return RPC::make_error(
            rpcDST_ISR_MALFORMED,
            "Unneeded field 'taker_gets.issuer' for "
            "XRP currency specification.");

    if (!isXRP(get_currency) && isXRP(get_issuer))
        return RPC::make_error(
            rpcDST_ISR_MALFORMED,
            "Invalid field 'taker_gets.issuer', expected non-XRP issuer.");

    boost::optional<AccountID> takerID;
    if (context.params.isMember(jss::taker))
    {
        if (!context.params[jss::taker].isString())
            return RPC::expected_field_error(jss::taker, "string");

        takerID = parseBase58<AccountID>(context.params[jss::taker].asString());
        if (!takerID)
            return RPC::invalid_field_error(jss::taker);
    }

    if (pay_currency == get_currency && pay_issuer == get_issuer)
    {
        JLOG(context.j.info()) << "taker_gets same as taker_pays.";
        return RPC::make_error(rpcBAD_MARKET);
    }

    unsigned int limit;
    if (auto err = readLimitField(limit, RPC::Tuning::bookOffers, context))
        return *err;

    bool const bProof(context.params.isMember(jss::proof));

    Json::Value const jvMarker(
        context.params.isMember(jss::marker) ? context.params[jss::marker]
                                             : Json::Value(Json::nullValue));

    context.netOps.getBookPage(
        lpLedger,
        {{pay_currency, pay_issuer}, {get_currency, get_issuer}},
        takerID ? *takerID : beast::zero,
        bProof,
        limit,
        jvMarker,
        jvResult);

    context.loadType = Resource::feeMediumBurdenRPC;

    return jvResult;
}

}  // namespace ripple
