//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2023 Ripple Labs Inc.

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

#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/misc/AMMUtils.h>
#include <xrpld/ledger/ReadView.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/RPCHelpers.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/AMMCore.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Feature.h>

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
    try
    {
        return issueFromJson(v);
    }
    catch (std::runtime_error const& ex)
    {
        JLOG(j.debug()) << "getIssue " << ex.what();
    }
    return Unexpected(rpcISSUE_MALFORMED);
}

std::string
to_iso8601(NetClock::time_point tp)
{
    // 2000-01-01 00:00:00 UTC is 946684800s from 1970-01-01 00:00:00 UTC
    using namespace std::chrono;
    return date::format(
        "%Y-%Om-%dT%H:%M:%OS%z",
        date::sys_time<system_clock::duration>(
            system_clock::time_point{tp.time_since_epoch() + epoch_offset}));
}

Json::Value
doAMMInfo(RPC::JsonContext& context)
{
    auto const& params(context.params);
    Json::Value result;

    std::shared_ptr<ReadView const> ledger;
    result = RPC::lookupLedger(ledger, context);
    if (!ledger)
        return result;

    struct ValuesFromContextParams
    {
        std::optional<AccountID> accountID;
        Issue issue1;
        Issue issue2;
        std::shared_ptr<SLE const> amm;
    };

    auto getValuesFromContextParams =
        [&]() -> Expected<ValuesFromContextParams, error_code_i> {
        std::optional<AccountID> accountID;
        std::optional<Issue> issue1;
        std::optional<Issue> issue2;
        std::optional<uint256> ammID;

        constexpr auto invalid = [](Json::Value const& params) -> bool {
            return (params.isMember(jss::asset) !=
                    params.isMember(jss::asset2)) ||
                (params.isMember(jss::asset) ==
                 params.isMember(jss::amm_account));
        };

        // NOTE, identical check for apVersion >= 3 below
        if (context.apiVersion < 3 && invalid(params))
            return Unexpected(rpcINVALID_PARAMS);

        if (params.isMember(jss::asset))
        {
            if (auto const i = getIssue(params[jss::asset], context.j))
                issue1 = *i;
            else
                return Unexpected(i.error());
        }

        if (params.isMember(jss::asset2))
        {
            if (auto const i = getIssue(params[jss::asset2], context.j))
                issue2 = *i;
            else
                return Unexpected(i.error());
        }

        if (params.isMember(jss::amm_account))
        {
            auto const id = getAccount(params[jss::amm_account], result);
            if (!id)
                return Unexpected(rpcACT_MALFORMED);
            auto const sle = ledger->read(keylet::account(*id));
            if (!sle)
                return Unexpected(rpcACT_MALFORMED);
            ammID = sle->getFieldH256(sfAMMID);
            if (ammID->isZero())
                return Unexpected(rpcACT_NOT_FOUND);
        }

        if (params.isMember(jss::account))
        {
            accountID = getAccount(params[jss::account], result);
            if (!accountID || !ledger->read(keylet::account(*accountID)))
                return Unexpected(rpcACT_MALFORMED);
        }

        // NOTE, identical check for apVersion < 3 above
        if (context.apiVersion >= 3 && invalid(params))
            return Unexpected(rpcINVALID_PARAMS);

        XRPL_ASSERT(
            (issue1.has_value() == issue2.has_value()) &&
                (issue1.has_value() != ammID.has_value()),
            "ripple::doAMMInfo : issue1 and issue2 do match");

        auto const ammKeylet = [&]() {
            if (issue1 && issue2)
                return keylet::amm(*issue1, *issue2);
            XRPL_ASSERT(ammID, "ripple::doAMMInfo::ammKeylet : ammID is set");
            return keylet::amm(*ammID);
        }();
        auto const amm = ledger->read(ammKeylet);
        if (!amm)
            return Unexpected(rpcACT_NOT_FOUND);
        if (!issue1 && !issue2)
        {
            issue1 = (*amm)[sfAsset].get<Issue>();
            issue2 = (*amm)[sfAsset2].get<Issue>();
        }

        return ValuesFromContextParams{
            accountID, *issue1, *issue2, std::move(amm)};
    };

    auto const r = getValuesFromContextParams();
    if (!r)
    {
        RPC::inject_error(r.error(), result);
        return result;
    }

    auto const& [accountID, issue1, issue2, amm] = *r;

    auto const ammAccountID = amm->getAccountID(sfAccount);

    // provide funds if frozen, specify asset_frozen flag
    auto const [asset1Balance, asset2Balance] = ammPoolHolds(
        *ledger,
        ammAccountID,
        issue1,
        issue2,
        FreezeHandling::fhIGNORE_FREEZE,
        context.j);
    auto const lptAMMBalance = accountID
        ? ammLPHolds(*ledger, *amm, *accountID, context.j)
        : (*amm)[sfLPTokenBalance];

    Json::Value ammResult;
    asset1Balance.setJson(ammResult[jss::amount]);
    asset2Balance.setJson(ammResult[jss::amount2]);
    lptAMMBalance.setJson(ammResult[jss::lp_token]);
    ammResult[jss::trading_fee] = (*amm)[sfTradingFee];
    ammResult[jss::account] = to_string(ammAccountID);
    Json::Value voteSlots(Json::arrayValue);
    if (amm->isFieldPresent(sfVoteSlots))
    {
        for (auto const& voteEntry : amm->getFieldArray(sfVoteSlots))
        {
            Json::Value vote;
            vote[jss::account] = to_string(voteEntry.getAccountID(sfAccount));
            vote[jss::trading_fee] = voteEntry[sfTradingFee];
            vote[jss::vote_weight] = voteEntry[sfVoteWeight];
            voteSlots.append(std::move(vote));
        }
    }
    if (voteSlots.size() > 0)
        ammResult[jss::vote_slots] = std::move(voteSlots);
    XRPL_ASSERT(
        !ledger->rules().enabled(fixInnerObjTemplate) ||
            amm->isFieldPresent(sfAuctionSlot),
        "ripple::doAMMInfo : auction slot is set");
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
            auction[jss::time_interval] =
                timeSlot ? *timeSlot : AUCTION_SLOT_TIME_INTERVALS;
            auctionSlot[sfPrice].setJson(auction[jss::price]);
            auction[jss::discounted_fee] = auctionSlot[sfDiscountedFee];
            auction[jss::account] =
                to_string(auctionSlot.getAccountID(sfAccount));
            auction[jss::expiration] = to_iso8601(NetClock::time_point{
                NetClock::duration{auctionSlot[sfExpiration]}});
            if (auctionSlot.isFieldPresent(sfAuthAccounts))
            {
                Json::Value auth;
                for (auto const& acct :
                     auctionSlot.getFieldArray(sfAuthAccounts))
                {
                    Json::Value jv;
                    jv[jss::account] = to_string(acct.getAccountID(sfAccount));
                    auth.append(jv);
                }
                auction[jss::auth_accounts] = auth;
            }
            ammResult[jss::auction_slot] = std::move(auction);
        }
    }

    if (!isXRP(asset1Balance))
        ammResult[jss::asset_frozen] =
            isFrozen(*ledger, ammAccountID, issue1.currency, issue1.account);
    if (!isXRP(asset2Balance))
        ammResult[jss::asset2_frozen] =
            isFrozen(*ledger, ammAccountID, issue2.currency, issue2.account);

    // Add concentrated liquidity information if feature is enabled and requested
    if (ledger->rules().enabled(featureAMMConcentratedLiquidity) &&
        params.isMember(jss::include_concentrated_liquidity) &&
        params[jss::include_concentrated_liquidity].asBool())
    {
        Json::Value concentratedLiquidity(Json::objectValue);
        
        // Add current tick and price information
        if (amm->isFieldPresent(sfCurrentTick))
        {
            concentratedLiquidity[jss::current_tick] = (*amm)[sfCurrentTick];
        }
        
        if (amm->isFieldPresent(sfSqrtPriceX64))
        {
            concentratedLiquidity[jss::sqrt_price_x64] = (*amm)[sfSqrtPriceX64];
        }
        
        if (amm->isFieldPresent(sfTickSpacing))
        {
            concentratedLiquidity[jss::tick_spacing] = (*amm)[sfTickSpacing];
        }
        
        // Add position information if account is specified
        if (accountID)
        {
            Json::Value positions(Json::arrayValue);
            
            // Find all positions for this account and AMM
            auto const ammID = amm->getFieldH256(sfAMMID);
            auto const root = keylet::ownerDir(*accountID);
            auto const ownerDir = ledger->read(root);
            
            if (ownerDir)
            {
                for (auto const& item : ownerDir->getFieldV256(sfIndexes))
                {
                    auto const sle = ledger->read({ltCONCENTRATED_LIQUIDITY_POSITION, item});
                    if (sle && sle->getFieldH256(sfAMMID) == ammID)
                    {
                        Json::Value position;
                        position[jss::tick_lower] = (*sle)[sfTickLower];
                        position[jss::tick_upper] = (*sle)[sfTickUpper];
                        position[jss::position_nonce] = (*sle)[sfPositionNonce];
                        position[jss::liquidity] = (*sle)[sfLiquidity].getJson(JsonOptions::none);
                        position[jss::fee_growth_inside_0_last_x128] = (*sle)[sfFeeGrowthInside0LastX128].getJson(JsonOptions::none);
                        position[jss::fee_growth_inside_1_last_x128] = (*sle)[sfFeeGrowthInside1LastX128].getJson(JsonOptions::none);
                        position[jss::tokens_owed_0] = (*sle)[sfTokensOwed0].getJson(JsonOptions::none);
                        position[jss::tokens_owed_1] = (*sle)[sfTokensOwed1].getJson(JsonOptions::none);
                        positions.append(std::move(position));
                    }
                }
            }
            
            if (positions.size() > 0)
            {
                concentratedLiquidity[jss::positions] = std::move(positions);
            }
        }
        
        if (!concentratedLiquidity.empty())
        {
            ammResult[jss::concentrated_liquidity] = std::move(concentratedLiquidity);
        }
    }

    result[jss::amm] = std::move(ammResult);
    if (!result.isMember(jss::ledger_index) &&
        !result.isMember(jss::ledger_hash))
        result[jss::ledger_current_index] = ledger->info().seq;
    result[jss::validated] = context.ledgerMaster.isValidated(*ledger);

    return result;
}

}  // namespace ripple
