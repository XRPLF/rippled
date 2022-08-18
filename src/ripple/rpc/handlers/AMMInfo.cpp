//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2022 Ripple Labs Inc.

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
#include <ripple/app/misc/AMM.h>
#include <ripple/json/json_value.h>
#include <ripple/ledger/ReadView.h>
#include <ripple/net/RPCErr.h>
#include <ripple/protocol/Issue.h>
#include <ripple/rpc/Context.h>
#include <ripple/rpc/impl/GRPCHelpers.h>
#include <ripple/rpc/impl/RPCHelpers.h>
#include <grpcpp/support/status.h>

namespace ripple {

std::optional<AccountID>
getAccount(Json::Value const& v, Json::Value& result)
{
    std::string strIdent(v.asString());
    AccountID accountID;

    if (auto jv = RPC::accountFromString(accountID, strIdent))
    {
        for (auto it = jv.begin(); it != jv.end(); ++it)
            result[it.memberName()] = (*it);

        return std::nullopt;
    }
    return std::optional<AccountID>(accountID);
}

Json::Value
doAMMInfo(RPC::JsonContext& context)
{
    auto const& params(context.params);
    Json::Value result;
    std::optional<AccountID> accountID;

    uint256 ammID{};
    STAmount asset1{noIssue()};
    STAmount asset2{noIssue()};
    if (!params.isMember(jss::amm_id))
    {
        // May provide asset1/asset2 as amounts
        if (!params.isMember(jss::asset1) || !params.isMember(jss::asset2))
            return RPC::missing_field_error(jss::amm_id);
        if (!amountFromJsonNoThrow(asset1, params[jss::asset1]) ||
            !amountFromJsonNoThrow(asset2, params[jss::asset2]))
        {
            RPC::inject_error(rpcACT_MALFORMED, result);
            return result;
        }
        ammID = calcAMMGroupHash(asset1.issue(), asset2.issue());
    }
    else if (!ammID.parseHex(params[jss::amm_id].asString()))
    {
        RPC::inject_error(rpcACT_MALFORMED, result);
        return result;
    }

    std::shared_ptr<ReadView const> ledger;
    result = RPC::lookupLedger(ledger, context);
    if (!ledger)
        return result;

    if (params.isMember(jss::account))
    {
        accountID = getAccount(params[jss::account], result);
        if (!accountID || !ledger->read(keylet::account(*accountID)))
        {
            RPC::inject_error(rpcACT_MALFORMED, result);
            return result;
        }
    }

    auto const amm = getAMMSle(*ledger, ammID);
    if (!amm)
        return rpcError(rpcACT_NOT_FOUND);

    auto const [issue1, issue2] = [&]() {
        if (asset1.issue() == noIssue())
            return getTokensIssue(*amm);
        return std::make_pair(asset1.issue(), asset2.issue());
    }();

    auto const ammAccountID = amm->getAccountID(sfAMMAccount);

    auto const [asset1Balance, asset2Balance] =
        ammPoolHolds(*ledger, ammAccountID, issue1, issue2, context.j);
    auto const lptAMMBalance = accountID
        ? lpHolds(*ledger, ammAccountID, *accountID, context.j)
        : amm->getFieldAmount(sfLPTokenBalance);

    asset1Balance.setJson(result[jss::Asset1]);
    asset2Balance.setJson(result[jss::Asset2]);
    lptAMMBalance.setJson(result[jss::LPToken]);
    result[jss::TradingFee] = amm->getFieldU16(sfTradingFee);
    result[jss::AMMAccount] = to_string(ammAccountID);
    Json::Value voteSlots(Json::arrayValue);
    if (amm->isFieldPresent(sfVoteSlots))
    {
        for (auto const& voteEntry : amm->getFieldArray(sfVoteSlots))
        {
            Json::Value vote;
            vote[jss::FeeVal] = voteEntry.getFieldU32(sfFeeVal);
            vote[jss::VoteWeight] = voteEntry.getFieldU32(sfVoteWeight);
            voteSlots.append(vote);
        }
    }
    if (voteSlots.size() > 0)
        result[jss::VoteSlots] = voteSlots;
    if (amm->isFieldPresent(sfAuctionSlot))
    {
        auto const& auctionSlot =
            static_cast<STObject const&>(amm->peekAtField(sfAuctionSlot));
        if (auctionSlot.isFieldPresent(sfAccount))
        {
            Json::Value auction;
            auction[jss::TimeInterval] =
                timeSlot(ledger->info().parentCloseTime, auctionSlot);
            auctionSlot.getFieldAmount(sfPrice).setJson(auction[jss::Price]);
            auction[jss::DiscountedFee] =
                auctionSlot.getFieldU32(sfDiscountedFee);
            result[jss::AuctionSlot] = auction;
        }
    }
    if (!params.isMember(jss::amm_id))
        result[jss::AMMID] = to_string(ammID);

    return result;
}

}  // namespace ripple
