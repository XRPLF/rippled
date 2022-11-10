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

Expected<Issue, error_code_i>
getIssue(Json::Value const& v, beast::Journal j)
{
    if (!v.isObject())
    {
        JLOG(j.debug())
            << "getIssue must be specified as an 'object' Json value";
        return Unexpected(rpcAMM_ISSUE_MALFORMED);
    }

    Issue issue = xrpIssue();

    Json::Value const& currency = v[jss::currency];
    Json::Value const& issuer = v[jss::issuer];
    if (!to_currency(issue.currency, currency.asString()))
    {
        JLOG(j.debug()) << "getIssue, invalid currency";
        return Unexpected(rpcAMM_ISSUE_MALFORMED);
    }

    if (isXRP(issue.currency))
    {
        if (!issuer.isNull())
        {
            JLOG(j.debug()) << "getIssue, XRP should not have issuer";
            return Unexpected(rpcAMM_ISSUE_MALFORMED);
        }
        return issue;
    }

    if (!issuer.isString() || !to_issuer(issue.account, issuer.asString()))
    {
        JLOG(j.debug()) << "getIssue, invalid issuer";
        return Unexpected(rpcAMM_ISSUE_MALFORMED);
    }

    return issue;
}

Json::Value
doAMMInfo(RPC::JsonContext& context)
{
    auto const& params(context.params);
    Json::Value result;
    std::optional<AccountID> accountID;

    Issue issue1;
    Issue issue2;

    if (!params.isMember(jss::asset) || !params.isMember(jss::asset2))
    {
        RPC::inject_error(rpcINVALID_PARAMS, result);
        return result;
    }

    if (auto const i = getIssue(params[jss::asset], context.j); !i)
    {
        RPC::inject_error(i.error(), result);
        return result;
    }
    else
        issue1 = *i;
    if (auto const i = getIssue(params[jss::asset2], context.j); !i)
    {
        RPC::inject_error(i.error(), result);
        return result;
    }
    else
        issue2 = *i;

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

    auto const ammKeylet = keylet::amm(issue1, issue2);
    auto const amm = ledger->read(ammKeylet);
    if (!amm)
        return rpcError(rpcACT_NOT_FOUND);

    auto const ammAccountID = amm->getAccountID(sfAMMAccount);

    auto const [asset1Balance, asset2Balance] =
        ammPoolHolds(*ledger, ammAccountID, issue1, issue2, context.j);
    auto const lptAMMBalance = accountID
        ? ammLPHolds(*ledger, *amm, *accountID, context.j)
        : (*amm)[sfLPTokenBalance];

    asset1Balance.setJson(result[jss::Amount]);
    asset2Balance.setJson(result[jss::Amount2]);
    lptAMMBalance.setJson(result[jss::LPToken]);
    result[jss::TradingFee] = (*amm)[sfTradingFee];
    result[jss::AMMAccount] = to_string(ammAccountID);
    Json::Value voteSlots(Json::arrayValue);
    if (amm->isFieldPresent(sfVoteSlots))
    {
        for (auto const& voteEntry : amm->getFieldArray(sfVoteSlots))
        {
            Json::Value vote;
            vote[jss::Account] = to_string(voteEntry.getAccountID(sfAccount));
            vote[jss::TradingFee] = voteEntry[sfTradingFee];
            vote[jss::VoteWeight] = voteEntry[sfVoteWeight];
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
            auto const timeSlot = ammAuctionTimeSlot(
                ledger->info().parentCloseTime.time_since_epoch().count(),
                auctionSlot);
            auction[jss::TimeInterval] = timeSlot ? *timeSlot : 0;
            auctionSlot[sfPrice].setJson(auction[jss::Price]);
            auction[jss::DiscountedFee] = auctionSlot[sfDiscountedFee];
            auction[jss::Account] =
                to_string(auctionSlot.getAccountID(sfAccount));
            auction[jss::Expiration] = auctionSlot[sfExpiration];
            if (auctionSlot.isFieldPresent(sfAuthAccounts))
            {
                Json::Value auth;
                for (auto const& acct :
                     auctionSlot.getFieldArray(sfAuthAccounts))
                {
                    Json::Value jv;
                    jv[jss::Account] = to_string(acct.getAccountID(sfAccount));
                    auth.append(jv);
                }
                auction = auth[jss::AuthAccounts];
            }
            result[jss::AuctionSlot] = auction;
        }
    }
    result[jss::AMMID] = to_string(ammKeylet.key);

    Json::Value ammResult;
    ammResult[jss::amm] = result;

    return ammResult;
}

}  // namespace ripple
