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
#include <ripple/app/paths/TrustLine.h>
#include <ripple/ledger/ReadView.h>
#include <ripple/net/RPCErr.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/jss.h>
#include <ripple/resource/Fees.h>
#include <ripple/rpc/Context.h>
#include <ripple/rpc/impl/RPCHelpers.h>
#include <ripple/rpc/impl/Tuning.h>

namespace ripple {

static Json::Value
addLine(RPCTrustLine const& tl)
{
    Json::Value ret(Json::objectValue);

    ret[jss::account] = to_string(tl.peerAccount());
    // Amount reported is positive if current account holds other
    // account's IOUs.
    //
    // Amount reported is negative if other account holds current
    // account's IOUs.
    ret[jss::balance] = tl.getBalance().getText();
    ret[jss::currency] = to_string(tl.currency());
    ret[jss::limit] = tl.getLimit().getText();
    ret[jss::limit_peer] = tl.getLimitPeer().getText();
    ret[jss::quality_in] = tl.getQualityIn().value;
    ret[jss::quality_out] = tl.getQualityOut().value;
    if (tl.getAuth())
        ret[jss::authorized] = true;
    if (tl.getAuthPeer())
        ret[jss::peer_authorized] = true;
    if (tl.getNoRipple())
        ret[jss::no_ripple] = true;
    if (tl.getNoRipplePeer())
        ret[jss::no_ripple_peer] = true;
    if (tl.getFreeze())
        ret[jss::freeze] = true;
    if (tl.getFreezePeer())
        ret[jss::freeze_peer] = true;

    return ret;
}

// {
//   account: <account>
//   ledger_hash : <ledger>
//   ledger_index : <ledger_index>
//   limit: integer                 // optional
//   marker: opaque                 // optional, resume previous query
//   ignore_default: bool           // do not return lines in default state (on
//   this account's side)
// }
Json::Value
doAccountLines(RPC::JsonContext& context)
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

    if (!ledger->exists(keylet::account(accountID)))
        return rpcError(rpcACT_NOT_FOUND);

    std::string strPeer;
    if (params.isMember(jss::peer))
        strPeer = params[jss::peer].asString();

    auto const peerAcct = [&]() -> std::optional<AccountID> {
        return strPeer.empty() ? std::nullopt : parseBase58<AccountID>(strPeer);
    }();
    if (!strPeer.empty() && !peerAcct)
    {
        RPC::inject_error(rpcACT_MALFORMED, result);
        return result;
    }

    unsigned int limit;
    if (auto err = readLimitField(limit, RPC::Tuning::accountLines, context))
        return *err;

    if (limit == 0)
        return rpcError(rpcINVALID_PARAMS);

    uint256 startAfter = beast::zero;
    std::uint64_t startHint = 0;

    // Avoid reallocations when possible.
    std::vector<RPCTrustLine> items;
    items.reserve(limit + 1);

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

    unsigned int count = 0;
    std::optional<uint256> marker = {};
    std::uint64_t nextHint = 0;
    Json::Value lines(Json::arrayValue);

    if (!forEachItemAfter(
            *ledger,
            accountID,
            startAfter,
            startHint,
            limit + 1,
            [ignoreDefault = params.isMember(jss::ignore_default) &&
                 params[jss::ignore_default].asBool(),
             &accountID,
             &peerAcct,
             &lines,
             &count,
             &marker,
             &limit,
             &nextHint](std::shared_ptr<SLE const> const& sle) {
                assert(sle);

                if (!sle)
                    return false;

                if (++count == limit)
                {
                    marker = sle->key();
                    nextHint = RPC::getStartHint(sle, accountID);
                }

                if (count <= limit && (sle->getType() == ltRIPPLE_STATE))
                {
                    RPCTrustLine const tl(sle, accountID);

                    if ((!ignoreDefault || tl.paidReserve()) &&
                        (!peerAcct || peerAcct.value() == tl.peerAccount()))
                    {
                        lines.append(addLine(tl));
                    }
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

    result[jss::lines] = std::move(lines);
    result[jss::account] = toBase58(accountID);

    context.loadType = Resource::feeMediumBurdenRPC;
    return result;
}

}  // namespace ripple
