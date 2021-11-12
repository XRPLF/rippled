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
#include <ripple/app/paths/RippleState.h>
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
    std::vector<RippleState::pointer> items;
    AccountID const& accountID;
    bool hasPeer;
    AccountID const& raPeerAccount;

    bool ignoreDefault;
    uint32_t foundCount;
    RippleState::pointer lastFound;
};

void
addLine(Json::Value& jsonLines, RippleState const& line)
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

    // this flag allows the requester to ask incoming trustlines in default
    // state be omitted
    bool ignoreDefault = params.isMember(jss::ignore_default) &&
        params[jss::ignore_default].asBool();

    Json::Value& jsonLines(result[jss::lines] = Json::arrayValue);
    VisitData visitData = {
        {}, accountID, hasPeer, raPeerAccount, ignoreDefault, 0, nullptr};
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

        auto const sleLine = ledger->read({ltRIPPLE_STATE, startAfter});

        if (!sleLine)
            return rpcError(rpcINVALID_PARAMS);

        bool isDefault = false;
        if (sleLine->getFieldAmount(sfLowLimit).getIssuer() == accountID)
        {
            startHint = sleLine->getFieldU64(sfLowNode);
            isDefault = !(sleLine->getFieldU32(sfFlags) & lsfLowReserve);
        }
        else if (sleLine->getFieldAmount(sfHighLimit).getIssuer() == accountID)
        {
            startHint = sleLine->getFieldU64(sfHighNode);
            isDefault = !(sleLine->getFieldU32(sfFlags) & lsfHighReserve);
        }
        else
            return rpcError(rpcINVALID_PARAMS);

        // Caller provided the first line (startAfter), add it as first result
        // (but only if it meets inclusion criteria)

        if (isDefault && ignoreDefault)
        {
            // even though we're starting our search here we don't include the
            // first entry in this edge case
            visitData.items.reserve(++reserve);
        }
        else
        {
            auto const line = RippleState::makeItem(accountID, sleLine);
            if (line == nullptr)
                return rpcError(rpcINVALID_PARAMS);

            addLine(jsonLines, *line);
            visitData.items.reserve(reserve);
        }
    }
    else
    {
        startHint = 0;
        // We have no start point, limit should be one higher than requested.
        visitData.items.reserve(++reserve);
    }

    {
        if (!forEachItemAfter(
                *ledger,
                accountID,
                startAfter,
                startHint,
                reserve,
                [&visitData](std::shared_ptr<SLE const> const& sleCur) {
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

                    auto const line =
                        RippleState::makeItem(visitData.accountID, sleCur);
                    if (line != nullptr &&
                        (!visitData.hasPeer ||
                         visitData.raPeerAccount == line->getAccountIDPeer()))
                    {
                        if (!ignore)
                            visitData.items.emplace_back(line);

                        visitData.lastFound = line;
                        visitData.foundCount++;

                        return true;
                    }

                    return false;
                }))
        {
            return rpcError(rpcINVALID_PARAMS);
        }
    }

    // RH Note:
    // If ignore_default flag is present all lines must still be iterated, the
    // flag only suppresses output. It does not change how iteration works. This
    // means the RPC call may return an empty set AND a marker. In this case
    // another query must be made until iteration is complete if a complete set
    // of non-default state lines are required.
    if (visitData.items.size() == reserve || visitData.foundCount >= reserve)
    {
        result[jss::limit] = limit;

        RippleState::pointer line(visitData.lastFound);
        result[jss::marker] = to_string(line->key());
        if (visitData.items.back() == visitData.lastFound)
            visitData.items.pop_back();
    }

    result[jss::account] = context.app.accountIDCache().toBase58(accountID);

    for (auto const& item : visitData.items)
        addLine(jsonLines, *item.get());

    context.loadType = Resource::feeMediumBurdenRPC;
    return result;
}

}  // namespace ripple
