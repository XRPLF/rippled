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
#include <ripple/json/json_value.h>
#include <ripple/ledger/ReadView.h>
#include <ripple/ledger/View.h>
#include <ripple/net/RPCErr.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/jss.h>
#include <ripple/resource/Fees.h>
#include <ripple/rpc/Context.h>
#include <ripple/rpc/impl/RPCHelpers.h>
#include <ripple/rpc/impl/Tuning.h>

namespace ripple {

void
appendOfferJson(std::shared_ptr<SLE const> const& offer, Json::Value& offers)
{
    STAmount dirRate =
        amountFromQuality(getQuality(offer->getFieldH256(sfBookDirectory)));
    Json::Value& obj(offers.append(Json::objectValue));
    offer->getFieldAmount(sfTakerPays).setJson(obj[jss::taker_pays]);
    offer->getFieldAmount(sfTakerGets).setJson(obj[jss::taker_gets]);
    obj[jss::seq] = offer->getFieldU32(sfSequence);
    obj[jss::flags] = offer->getFieldU32(sfFlags);
    obj[jss::quality] = dirRate.getText();
    if (offer->isFieldPresent(sfExpiration))
        obj[jss::expiration] = offer->getFieldU32(sfExpiration);
};

// {
//   account: <account>|<account_public_key>
//   ledger_hash : <ledger>
//   ledger_index : <ledger_index>
//   limit: integer                 // optional
//   marker: opaque                 // optional, resume previous query
// }
Json::Value
doAccountOffers(RPC::JsonContext& context)
{
    auto const& params(context.params);
    if (!params.isMember(jss::account))
        return RPC::missing_field_error(jss::account);

    std::shared_ptr<ReadView const> ledger;
    auto result = RPC::lookupLedger(ledger, context);
    if (!ledger)
        return result;

    std::string strIdent(params[jss::account].asString());
    AccountID accountID;

    if (auto jv = RPC::accountFromString(accountID, strIdent))
    {
        for (auto it = jv.begin(); it != jv.end(); ++it)
            result[it.memberName()] = (*it);

        return result;
    }

    // Get info on account.
    result[jss::account] = context.app.accountIDCache().toBase58(accountID);

    if (!ledger->exists(keylet::account(accountID)))
        return rpcError(rpcACT_NOT_FOUND);

    unsigned int limit;
    if (auto err = readLimitField(limit, RPC::Tuning::accountOffers, context))
        return *err;

    Json::Value& jsonOffers(result[jss::offers] = Json::arrayValue);
    std::vector<std::shared_ptr<SLE const>> offers;
    unsigned int reserve(limit);
    uint256 startAfter;
    std::uint64_t startHint;

    if (params.isMember(jss::marker))
    {
        // We have a start point. Use limit - 1 from the result and use the
        // very last one for the resume.
        Json::Value const& marker(params[jss::marker]);

        if (!marker.isString())
            return RPC::expected_field_error(jss::marker, "string");

        if (!startAfter.parseHex(marker.asString()))
            return rpcError(rpcINVALID_PARAMS);

        auto const sleOffer = ledger->read({ltOFFER, startAfter});

        if (!sleOffer || accountID != sleOffer->getAccountID(sfAccount))
        {
            return rpcError(rpcINVALID_PARAMS);
        }

        startHint = sleOffer->getFieldU64(sfOwnerNode);
        // Caller provided the first offer (startAfter), add it as first result
        appendOfferJson(sleOffer, jsonOffers);
        offers.reserve(reserve);
    }
    else
    {
        startHint = 0;
        // We have no start point, limit should be one higher than requested.
        offers.reserve(++reserve);
    }

    if (!forEachItemAfter(
            *ledger,
            accountID,
            startAfter,
            startHint,
            reserve,
            [&offers](std::shared_ptr<SLE const> const& offer) {
                if (offer->getType() == ltOFFER)
                {
                    offers.emplace_back(offer);
                    return true;
                }

                return false;
            }))
    {
        return rpcError(rpcINVALID_PARAMS);
    }

    if (offers.size() == reserve)
    {
        result[jss::limit] = limit;

        result[jss::marker] = to_string(offers.back()->key());
        offers.pop_back();
    }

    for (auto const& offer : offers)
        appendOfferJson(offer, jsonOffers);

    context.loadType = Resource::feeMediumBurdenRPC;
    return result;
}

}  // namespace ripple
