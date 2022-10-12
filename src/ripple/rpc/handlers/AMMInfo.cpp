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

std::optional<Issue>
getIssue(
    Json::Value const& params,
    std::string const& field,
    Json::Value& result)
{
    if (!params.isMember(field))
    {
        result = RPC::missing_field_error(field);
        return std::nullopt;
    }

    if (!params[field][jss::currency])
    {
        result = RPC::missing_field_error(field + ".currency");
        return std::nullopt;
    }

    if (!params[field][jss::currency].isString())
    {
        result = RPC::expected_field_error(field + ".currency", "string");
        return std::nullopt;
    }

    Currency currency;

    if (!to_currency(currency, params[field][jss::currency].asString()))
    {
        result = RPC::make_error(
            rpcAMM_CUR_MALFORMED,
            std::string(
                "Invalid field '" + field + ".currency', bad currency."));
        return std::nullopt;
    }

    AccountID issuer;

    if (params[field].isMember(jss::issuer))
    {
        if (!params[field][jss::issuer].isString())
        {
            result = RPC::expected_field_error(field + ".issuer", "string");
            return std::nullopt;
        }

        if (!to_issuer(issuer, params[field][jss::issuer].asString()))
        {
            result = RPC::make_error(
                rpcAMM_ISR_MALFORMED,
                std::string("Invalid field '") + field +
                    ".issuer', bad issuer");
            return std::nullopt;
        }

        if (issuer == noAccount())
        {
            result = RPC::make_error(
                rpcSRC_ISR_MALFORMED,
                std::string("Invalid field '") + field +
                    ".issuer', bad issuer account one");
            return std::nullopt;
        }
    }
    else
    {
        issuer = xrpAccount();
    }

    if (isXRP(currency) && !isXRP(issuer))
    {
        result = RPC::make_error(
            rpcAMM_ISR_MALFORMED,
            std::string("Unneeded field '") + field +
                ".issuer' for "
                "XRP currency specification.");
        return std::nullopt;
    }

    if (!isXRP(currency) && isXRP(issuer))
    {
        result = RPC::make_error(
            rpcAMM_ISR_MALFORMED,
            std::string("Invalid field '") + field +
                ".issuer', expected non-XRP issuer.");
        return std::nullopt;
    }

    return {{currency, issuer}};
}

Json::Value
doAMMInfo(RPC::JsonContext& context)
{
    auto const& params(context.params);
    Json::Value result;
    std::optional<AccountID> accountID;

    uint256 ammID{};
    Issue token1Issue{noIssue()};
    Issue token2Issue{noIssue()};
    if (!params.isMember(jss::amm_id))
    {
        // May provide issue1 and issue2
        if (auto const i = getIssue(params, jss::asset1.c_str(), result); !i)
            return result;
        else
            token1Issue = *i;
        if (auto const i = getIssue(params, jss::asset2.c_str(), result); !i)
            return result;
        else
            token2Issue = *i;
        ammID = calcAMMGroupHash(token1Issue, token2Issue);
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

    auto const amm = ledger->read(keylet::amm(ammID));
    if (!amm)
        return rpcError(rpcACT_NOT_FOUND);

    auto const [issue1, issue2] = [&]() {
        if (token1Issue == noIssue())
            return getTokensIssue(*amm);
        return std::make_pair(token1Issue, token2Issue);
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
