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

#include <BeastConfig.h>
#include <ripple/app/main/Application.h>
#include <ripple/ledger/CachedView.h>
#include <ripple/rpc/impl/Tuning.h>

namespace ripple {

// {
//   account: <account>|<account_public_key>
//   account_index: <number>        // optional, defaults to 0.
//   ledger_hash : <ledger>
//   ledger_index : <ledger_index>
//   limit: integer                 // optional
//   marker: opaque                 // optional, resume previous query
// }
Json::Value doAccountOffers (RPC::Context& context)
{
    auto const& params (context.params);
    if (! params.isMember (jss::account))
        return RPC::missing_field_error (jss::account);

    Ledger::pointer ledger;
    Json::Value result (RPC::lookupLedger (params, ledger, context.netOps));
    if (! ledger)
        return result;

    std::string strIdent (params[jss::account].asString ());
    bool bIndex (params.isMember (jss::account_index));
    int const iIndex (bIndex ? params[jss::account_index].asUInt () : 0);
    AccountID accountID;

    Json::Value const jv = RPC::accountFromString (
        accountID, bIndex, strIdent, iIndex, false);
    if (jv)
    {
        for (Json::Value::const_iterator it (jv.begin ()); it != jv.end (); ++it)
            result[it.memberName ()] = it.key ();

        return result;
    }

    // Get info on account.
    result[jss::account] = getApp().accountIDCache().toBase58 (accountID);

    if (bIndex)
        result[jss::account_index] = iIndex;

    if (! ledger->exists(keylet::account (accountID)))
        return rpcError (rpcACT_NOT_FOUND);

    unsigned int limit;
    if (params.isMember (jss::limit))
    {
        auto const& jvLimit (params[jss::limit]);
        if (! jvLimit.isIntegral ())
            return RPC::expected_field_error (jss::limit, "unsigned integer");

        limit = jvLimit.isUInt () ? jvLimit.asUInt () :
            std::max (0, jvLimit.asInt ());

        if (context.role != Role::ADMIN)
        {
            limit = std::max (RPC::Tuning::minOffersPerRequest,
                std::min (limit, RPC::Tuning::maxOffersPerRequest));
        }
    }
    else
    {
        limit = RPC::Tuning::defaultOffersPerRequest;
    }

    Json::Value& jsonOffers (result[jss::offers] = Json::arrayValue);
    std::vector <std::shared_ptr<SLE const>> offers;
    unsigned int reserve (limit);
    uint256 startAfter;
    std::uint64_t startHint;

    if (params.isMember(jss::marker))
    {
        // We have a start point. Use limit - 1 from the result and use the
        // very last one for the resume.
        Json::Value const& marker (params[jss::marker]);

        if (! marker.isString ())
            return RPC::expected_field_error (jss::marker, "string");

        startAfter.SetHex (marker.asString ());
        auto const sleOffer = cachedRead (*ledger, startAfter,
            getApp().getSLECache());

        if (sleOffer == nullptr ||
            sleOffer->getType () != ltOFFER ||
            accountID != sleOffer->getAccountID (sfAccount))
        {
            return rpcError (rpcINVALID_PARAMS);
        }

        startHint = sleOffer->getFieldU64(sfOwnerNode);

        // Caller provided the first offer (startAfter), add it as first result
        Json::Value& obj (jsonOffers.append (Json::objectValue));
        sleOffer->getFieldAmount (sfTakerPays).setJson (obj[jss::taker_pays]);
        sleOffer->getFieldAmount (sfTakerGets).setJson (obj[jss::taker_gets]);
        obj[jss::seq] = sleOffer->getFieldU32 (sfSequence);
        obj[jss::flags] = sleOffer->getFieldU32 (sfFlags);

        offers.reserve (reserve);
    }
    else
    {
        startHint = 0;
        // We have no start point, limit should be one higher than requested.
        offers.reserve (++reserve);
    }

    {
        CachedView const view(
            *ledger, getApp().getSLECache());
        if (! forEachItemAfter(*ledger, accountID,
                startAfter, startHint, reserve,
            [&offers](std::shared_ptr<SLE const> const& offer)
            {
                if (offer->getType () == ltOFFER)
                {
                    offers.emplace_back (offer);
                    return true;
                }

                return false;
            }))
        {
            return rpcError (rpcINVALID_PARAMS);
        }
    }

    if (offers.size () == reserve)
    {
        result[jss::limit] = limit;

        result[jss::marker] = to_string (offers.back ()->getIndex ());
        offers.pop_back ();
    }

    for (auto const& offer : offers)
    {
        Json::Value& obj (jsonOffers.append (Json::objectValue));
        offer->getFieldAmount (sfTakerPays).setJson (obj[jss::taker_pays]);
        offer->getFieldAmount (sfTakerGets).setJson (obj[jss::taker_gets]);
        obj[jss::seq] = offer->getFieldU32 (sfSequence);
        obj[jss::flags] = offer->getFieldU32 (sfFlags);
    }

    context.loadType = Resource::feeMediumBurdenRPC;
    return result;
}

} // ripple
