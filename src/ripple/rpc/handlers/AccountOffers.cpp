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
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/RPCErr.h>
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
//   account: <account>
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

    auto id = parseBase58<AccountID>(params[jss::account].asString());
    if (!id)
    {
        RPC::inject_error(rpcACT_MALFORMED, result);
        return result;
    }
    auto const accountID{std::move(id.value())};

    // Get info on account.
    result[jss::account] = toBase58(accountID);

    if (!ledger->exists(keylet::account(accountID)))
        return rpcError(rpcACT_NOT_FOUND);

    unsigned int limit;
    if (auto err = readLimitField(limit, RPC::Tuning::accountOffers, context))
        return *err;

    if (limit == 0)
        return rpcError(rpcINVALID_PARAMS);

    Json::Value& jsonOffers(result[jss::offers] = Json::arrayValue);
    std::vector<std::shared_ptr<SLE const>> offers;
    uint256 startAfter = beast::zero;
    std::uint64_t startHint = 0;

    if (params.isMember(jss::marker))
    {
        if (!params[jss::marker].isString())
            return RPC::expected_field_error(jss::marker, "string");

        // Marker is composed of a comma separated index and start hint. The
        // former will be read as hex, and the latter using boost lexical cast.
        std::stringstream marker(params[jss::marker].asString());
        std::string value;
        if (!std::getline(marker, value, ','))
            return rpcError(rpcINVALID_PARAMS);

        if (!startAfter.parseHex(value))
            return rpcError(rpcINVALID_PARAMS);

        if (!std::getline(marker, value, ','))
            return rpcError(rpcINVALID_PARAMS);

        try
        {
            startHint = boost::lexical_cast<std::uint64_t>(value);
        }
        catch (boost::bad_lexical_cast&)
        {
            return rpcError(rpcINVALID_PARAMS);
        }

        // We then must check if the object pointed to by the marker is actually
        // owned by the account in the request.
        auto const sle = ledger->read({ltANY, startAfter});

        if (!sle)
            return rpcError(rpcINVALID_PARAMS);

        if (!RPC::isRelatedToAccount(*ledger, sle, accountID))
            return rpcError(rpcINVALID_PARAMS);
    }

    auto count = 0;
    std::optional<uint256> marker = {};
    std::uint64_t nextHint = 0;
    if (!forEachItemAfter(
            *ledger,
            accountID,
            startAfter,
            startHint,
            limit + 1,
            [&offers, &count, &marker, &limit, &nextHint, &accountID](
                std::shared_ptr<SLE const> const& sle) {
                if (!sle)
                {
                    assert(false);
                    return false;
                }

                if (++count == limit)
                {
                    marker = sle->key();
                    nextHint = RPC::getStartHint(sle, accountID);
                }

                if (count <= limit && sle->getType() == ltOFFER)
                {
                    offers.emplace_back(sle);
                }

                return true;
            }))
    {
        return rpcError(rpcINVALID_PARAMS);
    }

    // Both conditions need to be checked because marker is set on the limit-th
    // item, but if there is no item on the limit + 1 iteration, then there is
    // no need to return a marker.
    if (count == limit + 1 && marker)
    {
        result[jss::limit] = limit;
        result[jss::marker] =
            to_string(*marker) + "," + std::to_string(nextHint);
    }

    for (auto const& offer : offers)
        appendOfferJson(offer, jsonOffers);

    context.loadType = Resource::feeMediumBurdenRPC;
    return result;
}

}  // namespace ripple
