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

std::uint16_t
timeSlot(NetClock::time_point const& clock, STObject const& auctionSlot)
{
    using namespace std::chrono;
    std::uint32_t constexpr totalSlotTimeSecs = 24 * 3600;
    std::uint32_t constexpr intervalDuration = totalSlotTimeSecs / 20;
    auto const current =
        duration_cast<seconds>(clock.time_since_epoch()).count();
    if (auctionSlot.isFieldPresent(sfTimeStamp))
    {
        auto const stamp = auctionSlot.getFieldU32(sfTimeStamp);
        auto const diff = current - stamp;
        if (diff < totalSlotTimeSecs)
            return diff / intervalDuration;
    }
    return 0;
}

Json::Value
doAMMInfo(RPC::JsonContext& context)
{
    auto const& params(context.params);
    Json::Value result;
    std::optional<AccountID> accountID;

    uint256 ammHash{};
    STAmount asset1{noIssue()};
    STAmount asset2{noIssue()};
    if (!params.isMember(jss::AMMHash))
    {
        // May provide asset1/asset2 as amounts
        if (!params.isMember(jss::Asset1) || !params.isMember(jss::Asset2))
            return RPC::missing_field_error(jss::AMMHash);
        if (!amountFromJsonNoThrow(asset1, params[jss::Asset1]) ||
            !amountFromJsonNoThrow(asset2, params[jss::Asset2]))
        {
            RPC::inject_error(rpcACT_MALFORMED, result);
            return result;
        }
        ammHash = calcAMMGroupHash(asset1.issue(), asset2.issue());
    }
    else if (!ammHash.parseHex(params[jss::AMMHash].asString()))
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

    auto const amm = getAMMSle(*ledger, ammHash);
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
    lptAMMBalance.setJson(result[jss::LPTokens]);
    result[jss::TradingFee] = amm->getFieldU16(sfTradingFee);
    result[jss::AMMAccount] = to_string(ammAccountID);
    Json::Value voteEntries(Json::arrayValue);
    if (amm->isFieldPresent(sfVoteEntries))
    {
        for (auto const& voteEntry : amm->getFieldArray(sfVoteEntries))
        {
            Json::Value vote;
            vote[jss::FeeVal] = voteEntry.getFieldU32(sfFeeVal);
            vote[jss::VoteWeight] = voteEntry.getFieldU32(sfVoteWeight);
            voteEntries.append(vote);
        }
    }
    if (voteEntries.size() > 0)
        result[jss::VoteEntries] = voteEntries;
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
    if (!params.isMember(jss::AMMHash))
        result[jss::AMMHash] = to_string(ammHash);

    return result;
}

std::pair<org::xrpl::rpc::v1::GetAmmInfoResponse, grpc::Status>
doAmmInfoGrpc(RPC::GRPCContext<org::xrpl::rpc::v1::GetAmmInfoRequest>& context)
{
    // Return values
    org::xrpl::rpc::v1::GetAmmInfoResponse result;
    grpc::Status status = grpc::Status::OK;

    // input
    org::xrpl::rpc::v1::GetAmmInfoRequest& params = context.params;

    std::shared_ptr<ReadView const> ledger;
    auto lgrStatus = RPC::ledgerFromRequest(ledger, context);
    if (lgrStatus || !ledger)
    {
        grpc::Status errorStatus;
        if (lgrStatus.toErrorCode() == rpcINVALID_PARAMS)
        {
            errorStatus = grpc::Status(
                grpc::StatusCode::INVALID_ARGUMENT, lgrStatus.message());
        }
        else
        {
            errorStatus =
                grpc::Status(grpc::StatusCode::NOT_FOUND, lgrStatus.message());
        }
        return {result, errorStatus};
    }

    // decode AMM hash
    uint256 ammHash;
    Issue issue1{noIssue()};
    Issue issue2{noIssue()};
    if (!params.has_ammhash())
    {
        if (!params.has_asset1() || !params.has_asset2())
            return {
                result,
                grpc::Status(
                    grpc::StatusCode::NOT_FOUND, "Missing field ammHash.")};
        auto getIssue = [](auto const& v) {
            if (v.has_xrp_amount())
                return xrpIssue();
            auto const iou = v.issued_currency_amount();
            auto const account =
                RPC::accountFromStringStrict(iou.issuer().address());
            if (!account)
                return noIssue();
            Currency currency = to_currency(iou.currency().name());
            return Issue(currency, *account);
        };
        issue1 = getIssue(params.asset1().value());
        issue2 = getIssue(params.asset2().value());
        if (issue1 == noIssue() || issue2 == noIssue())
            return {
                result,
                grpc::Status(
                    grpc::StatusCode::NOT_FOUND, "Account malformed.")};
        ammHash = calcAMMGroupHash(issue1, issue2);
    }
    else if (!ammHash.parseHex(params.ammhash().value()))
        return {
            result,
            grpc::Status(grpc::StatusCode::NOT_FOUND, "Account malformed.")};

    // decode LPT account
    std::optional<AccountID> accountID = {};
    if (params.has_account())
    {
        accountID = [&]() -> std::optional<AccountID> {
            std::string strIdent = params.account().value().address();
            AccountID account;
            error_code_i code =
                RPC::accountFromStringWithCode(account, strIdent, false);
            if (code == rpcSUCCESS)
                return std::optional<AccountID>(account);
            return std::optional<AccountID>{};
        }();
        if (!accountID.has_value() ||
            !ledger->read(keylet::account(*accountID)))
            return {
                result,
                grpc::Status{
                    grpc::StatusCode::INVALID_ARGUMENT, "Account malformed."}};
    }

    auto const amm = getAMMSle(*ledger, ammHash);
    if (!amm)
        return {
            result,
            grpc::Status(grpc::StatusCode::NOT_FOUND, "Account not found.")};

    if (issue1 == noIssue())
        std::tie(issue1, issue2) = getTokensIssue(*amm);

    auto const ammAccountID = amm->getAccountID(sfAMMAccount);

    auto const [asset1Balance, asset2Balance] =
        ammPoolHolds(*ledger, ammAccountID, issue1, issue2, context.j);
    auto const lptAMMBalance = accountID
        ? lpHolds(*ledger, ammAccountID, *accountID, context.j)
        : amm->getFieldAmount(sfLPTokenBalance);

    auto asset1 = result.mutable_asset1();
    ripple::RPC::convert(*asset1, asset1Balance);
    auto asset2 = result.mutable_asset2();
    ripple::RPC::convert(*asset2, asset2Balance);
    auto tokens = result.mutable_tokens();
    ripple::RPC::convert(*tokens, lptAMMBalance);
    result.mutable_trading_fee()->set_value(amm->getFieldU16(sfTradingFee));
    *result.mutable_ammaccount()->mutable_value()->mutable_address() =
        toBase58(ammAccountID);
    if (!params.has_ammhash())
        *result.mutable_ammhash()->mutable_value() = to_string(ammHash);
    if (amm->isFieldPresent(sfVoteEntries))
    {
        for (auto const& voteEntry : amm->getFieldArray(sfVoteEntries))
        {
            auto& vote_entries = *result.add_vote_entries();
            vote_entries.mutable_fee_val()->set_value(
                voteEntry.getFieldU32(sfFeeVal));
            vote_entries.mutable_vote_weight()->set_value(
                voteEntry.getFieldU32(sfVoteWeight));
        }
    }
    if (amm->isFieldPresent(sfAuctionSlot))
    {
        auto const& auctionSlot =
            static_cast<STObject const&>(amm->peekAtField(sfAuctionSlot));
        if (auctionSlot.isFieldPresent(sfAccount))
        {
            auto& auction_slot = *result.mutable_auction_slot();
            auction_slot.set_time_interval(
                timeSlot(ledger->info().parentCloseTime, auctionSlot));
            auction_slot.mutable_discounted_fee()->set_value(
                auctionSlot.getFieldU32(sfDiscountedFee));
            ripple::RPC::convert(
                *auction_slot.mutable_price(),
                auctionSlot.getFieldAmount(sfPrice));
        }
    }

    result.set_ledger_index(ledger->info().seq);
    result.set_validated(
        RPC::isValidated(context.ledgerMaster, *ledger, context.app));

    return {result, status};
}

}  // namespace ripple
