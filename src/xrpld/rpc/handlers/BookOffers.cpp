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
#include <xrpld/rpc/BookChanges.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/RPCHelpers.h>

#include <xrpl/basics/Log.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/resource/Fees.h>

#include <boost/format.hpp>

namespace ripple {

std::optional<Json::Value>
validateTakerJSON(Json::Value const& taker, Json::StaticString const& name)
{
    if (!taker.isMember(jss::currency) && !taker.isMember(jss::mpt_issuance_id))
        return RPC::missing_field_error(
            (boost::format("%s.currency") % name.c_str()).str());

    if (taker.isMember(jss::mpt_issuance_id) &&
        (taker.isMember(jss::currency) || taker.isMember(jss::issuer)))
        return RPC::invalid_field_error(name.c_str());

    if ((taker.isMember(jss::currency) && !taker[jss::currency].isString()) ||
        (taker.isMember(jss::mpt_issuance_id) &&
         !taker[jss::mpt_issuance_id].isString()))
        return RPC::expected_field_error(
            (boost::format("%s.currency") % name.c_str()).str(), "string");

    return std::nullopt;
}

std::optional<Json::Value>
parseTakerAssetJSON(
    Asset& asset,
    Json::Value const& taker,
    Json::StaticString const& name,
    beast::Journal j)
{
    auto const assetError = [&]() {
        if (name == jss::taker_pays)
            return rpcSRC_CUR_MALFORMED;
        return rpcDST_AMT_MALFORMED;
    }();

    if (taker.isMember(jss::currency))
    {
        Issue issue = xrpIssue();

        if (!to_currency(issue.currency, taker[jss::currency].asString()))
        {
            JLOG(j.info()) << boost::format("Bad %s currency.") % name.c_str();
            return RPC::make_error(
                assetError,
                (boost::format("Invalid field '%s.currency', bad currency.") %
                 name.c_str())
                    .str());
        }
        asset = issue;
    }
    else if (taker.isMember(jss::mpt_issuance_id))
    {
        MPTID mptid;
        if (!mptid.parseHex(taker[jss::mpt_issuance_id].asString()))
            return RPC::make_error(
                assetError,
                (boost::format("Invalid field '%s.mpt_issuance_id'") %
                 name.c_str())
                    .str());
        asset = mptid;
    }

    return std::nullopt;
}

std::optional<Json::Value>
parseTakerIssuerJSON(
    Asset& asset,
    Json::Value const& taker,
    Json::StaticString const& name,
    beast::Journal j)
{
    auto const issuerError = [&]() {
        if (name == jss::taker_pays)
            return rpcSRC_ISR_MALFORMED;
        return rpcDST_ISR_MALFORMED;
    }();

    if (taker.isMember(jss::currency))
    {
        Issue& issue = asset.get<Issue>();

        if (taker.isMember(jss::issuer))
        {
            if (!taker[jss::issuer].isString())
                return RPC::expected_field_error(
                    (boost::format("%s.issuer") % name.c_str()).str(),
                    "string");

            if (!to_issuer(issue.account, taker[jss::issuer].asString()))
                return RPC::make_error(
                    issuerError,
                    (boost::format("Invalid field '%s.issuer', bad issuer.") %
                     name.c_str())
                        .str());

            if (issue.account == noAccount())
                return RPC::make_error(
                    issuerError,
                    (boost::format(
                         "Invalid field '%s.issuer', bad issuer account one.") %
                     name.c_str())
                        .str());
        }
        else
        {
            issue.account = xrpAccount();
        }

        if (isXRP(issue.currency) && !isXRP(issue.account))
            return RPC::make_error(
                issuerError,
                (boost::format("Unneeded field '%s.issuer' for XRP currency "
                               "specification.") %
                 name.c_str())
                    .str());

        if (!isXRP(issue.currency) && isXRP(issue.account))
            return RPC::make_error(
                issuerError,
                (boost::format(
                     "Invalid field '%s.issuer', expected non-XRP issuer.") %
                 name.c_str())
                    .str());
    }

    return std::nullopt;
}

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

    if (auto const err = validateTakerJSON(taker_pays, jss::taker_pays))
        return *err;

    if (auto const err = validateTakerJSON(taker_gets, jss::taker_gets))
        return *err;

    Book book;

    if (auto const err = parseTakerAssetJSON(
            book.in, taker_pays, jss::taker_pays, context.j))
        return *err;

    if (auto const err = parseTakerAssetJSON(
            book.out, taker_gets, jss::taker_gets, context.j))
        return *err;

    if (auto const err = parseTakerIssuerJSON(
            book.in, taker_pays, jss::taker_pays, context.j))
        return *err;

    if (auto const err = parseTakerIssuerJSON(
            book.out, taker_gets, jss::taker_gets, context.j))
        return *err;

    std::optional<AccountID> takerID;
    if (context.params.isMember(jss::taker))
    {
        if (!context.params[jss::taker].isString())
            return RPC::expected_field_error(jss::taker, "string");

        takerID = parseBase58<AccountID>(context.params[jss::taker].asString());
        if (!takerID)
            return RPC::invalid_field_error(jss::taker);
    }

    if (book.in == book.out)
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
        book,
        takerID ? *takerID : beast::zero,
        bProof,
        limit,
        jvMarker,
        jvResult);

    context.loadType = Resource::feeMediumBurdenRPC;

    return jvResult;
}

Json::Value
doBookChanges(RPC::JsonContext& context)
{
    std::shared_ptr<ReadView const> ledger;

    Json::Value result = RPC::lookupLedger(ledger, context);
    if (ledger == nullptr)
        return result;

    return RPC::computeBookChanges(ledger);
}

}  // namespace ripple
