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

struct VisitData
{
    std::vector<RPCTrustLine> items;
    AccountID const& accountID;
    bool hasPeer;
    AccountID const& raPeerAccount;

    bool ignoreDefault;
    uint32_t foundCount;
};

void
addLine(Json::Value& jsonLines, RPCTrustLine const& line)
{
    STAmount const& saBalance(line.getBalance());
    STAmount const& saLimit(line.getLimit());
    STAmount const& saLimitPeer(line.getLimitPeer());
    Json::Value& jPeer(jsonLines.append(Json::objectValue));

    jPeer[jss::account] = to_string(line.getAccountIDPeer());
    // Amount reported is positive if current account holds other
    // account's IOUs.
    //
    // Amount reported is negative if other account holds current
    // account's IOUs.
    jPeer[jss::balance] = saBalance.getText();
    jPeer[jss::currency] = to_string(saBalance.issue().currency);
    jPeer[jss::limit] = saLimit.getText();
    jPeer[jss::limit_peer] = saLimitPeer.getText();
    jPeer[jss::quality_in] = line.getQualityIn().value;
    jPeer[jss::quality_out] = line.getQualityOut().value;
    if (line.getAuth())
        jPeer[jss::authorized] = true;
    if (line.getAuthPeer())
        jPeer[jss::peer_authorized] = true;
    if (line.getNoRipple() || !line.getDefaultRipple())
        jPeer[jss::no_ripple] = line.getNoRipple();
    if (line.getNoRipplePeer() || !line.getDefaultRipple())
        jPeer[jss::no_ripple_peer] = line.getNoRipplePeer();
    if (line.getFreeze())
        jPeer[jss::freeze] = true;
    if (line.getFreezePeer())
        jPeer[jss::freeze_peer] = true;
}

// {
//   account: <account>|<account_public_key>
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

    std::string strIdent(params[jss::account].asString());
    AccountID accountID;

    if (auto jv = RPC::accountFromString(accountID, strIdent))
    {
        for (auto it = jv.begin(); it != jv.end(); ++it)
            result[it.memberName()] = *it;
        return result;
    }

    if (!ledger->exists(keylet::account(accountID)))
        return rpcError(rpcACT_NOT_FOUND);

    std::string strPeer;
    if (params.isMember(jss::peer))
        strPeer = params[jss::peer].asString();
    auto hasPeer = !strPeer.empty();

    AccountID raPeerAccount;
    if (hasPeer)
    {
        if (auto jv = RPC::accountFromString(raPeerAccount, strPeer))
        {
            for (auto it = jv.begin(); it != jv.end(); ++it)
                result[it.memberName()] = *it;
            return result;
        }
    }

    unsigned int limit;
    if (auto err = readLimitField(limit, RPC::Tuning::accountLines, context))
        return *err;

    if (limit == 0)
        return rpcError(rpcINVALID_PARAMS);

    // this flag allows the requester to ask incoming trustlines in default
    // state be omitted
    bool ignoreDefault = params.isMember(jss::ignore_default) &&
        params[jss::ignore_default].asBool();

    Json::Value& jsonLines(result[jss::lines] = Json::arrayValue);
    VisitData visitData = {
        {}, accountID, hasPeer, raPeerAccount, ignoreDefault, 0};
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
    {
        if (!forEachItemAfter(
                *ledger,
                accountID,
                startAfter,
                startHint,
                limit + 1,
                [&visitData, &count, &marker, &limit, &nextHint](
                    std::shared_ptr<SLE const> const& sleCur) {
                    if (!sleCur)
                    {
                        assert(false);
                        return false;
                    }

                    if (++count == limit)
                    {
                        marker = sleCur->key();
                        nextHint =
                            RPC::getStartHint(sleCur, visitData.accountID);
                    }

                    if (sleCur->getType() != ltRIPPLE_STATE)
                        return true;

                    bool ignore = false;
                    if (visitData.ignoreDefault)
                    {
                        if (sleCur->getFieldAmount(sfLowLimit).getIssuer() ==
                            visitData.accountID)
                            ignore =
                                !(sleCur->getFieldU32(sfFlags) & lsfLowReserve);
                        else
                            ignore = !(
                                sleCur->getFieldU32(sfFlags) & lsfHighReserve);
                    }

                    if (!ignore && count <= limit)
                    {
                        auto const line =
                            RPCTrustLine::makeItem(visitData.accountID, sleCur);

                        if (line &&
                            (!visitData.hasPeer ||
                             visitData.raPeerAccount ==
                                 line->getAccountIDPeer()))
                        {
                            visitData.items.emplace_back(*line);
                        }
                    }

                    return true;
                }))
        {
            return rpcError(rpcINVALID_PARAMS);
        }
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

    result[jss::account] = toBase58(accountID);

    for (auto const& item : visitData.items)
        addLine(jsonLines, item);

    context.loadType = Resource::feeMediumBurdenRPC;
    return result;
}

}  // namespace ripple
