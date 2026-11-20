#include <xrpld/rpc/handlers/orderbook/AMMInfo.h>

#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/RPCLedgerHelpers.h>
#include <xrpld/rpc/detail/SpecBridge.hpp>

#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/basics/safe_cast.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/helpers/AMMHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/AMMCore.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/Feature.h>  // IWYU pragma: keep
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/jss.h>

#include <date/date.h>
#include <rpcspec/DeferredTypes.hpp>
#include <rpcspec/Errors.hpp>

#include <chrono>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace xrpl::rpc {

namespace {

std::string
toIso8601(NetClock::time_point tp)
{
    // 2000-01-01 00:00:00 UTC is 946684800s from 1970-01-01 00:00:00 UTC
    using namespace std::chrono;
    return date::format(
        "%Y-%Om-%dT%H:%M:%OS%z",
        date::sys_time<system_clock::duration>(
            system_clock::time_point{tp.time_since_epoch() + kEpochOffset}));
}

template <typename T>
bool
isPresent(::rpc::spec::Deferred<T> const& field)
{
    return field.has_value() or field.error() != ::rpc::spec::DeferredError::Missing;
}

}  // namespace

AMMInfoHandler::AMMInfoHandler(JsonContext& context) : context_(context)
{
}

std::expected<AMMInfoHandler::Output, ::rpc::Status>
AMMInfoHandler::process(Input const& input) const
{
    auto const& context = context_.get();

    Output output;
    if (auto const status = getLedger(output.ledger, input.ledger, context);
        status != ::rpc::Status::kOK)
    {
        return std::unexpected{status};
    }
    auto const& ledger = output.ledger;

    auto const fail = [&output](ErrorCodeI code) {
        output.error = code;
        return output;
    };

    bool const invalid = (isPresent(input.asset1) != isPresent(input.asset2)) ||
        (isPresent(input.asset1) == isPresent(input.ammAccount));

    // NOTE, identical check for apVersion >= 3 below
    if (context.apiVersion < 3 && invalid)
        return fail(RpcInvalidParams);

    if (isPresent(input.asset1) && !input.asset1)
        return fail(RpcIssueMalformed);

    if (isPresent(input.asset2) && !input.asset2)
        return fail(RpcIssueMalformed);

    std::optional<UInt256> ammID;
    if (isPresent(input.ammAccount))
    {
        if (!input.ammAccount)
            return fail(RpcActMalformed);
        auto const sle = ledger->read(keylet::account(*input.ammAccount));
        if (!sle)
            return fail(RpcActMalformed);
        ammID = sle->getFieldH256(sfAMMID);
        if (ammID->isZero())
            return fail(RpcActNotFound);
    }

    std::optional<AccountID> accountID;
    if (isPresent(input.accountID))
    {
        if (!input.accountID || !ledger->read(keylet::account(*input.accountID)))
            return fail(RpcActMalformed);
        accountID = *input.accountID;
    }

    // NOTE, identical check for apVersion < 3 above
    if (context.apiVersion >= 3 && invalid)
        return fail(RpcInvalidParams);

    XRPL_ASSERT(
        (input.asset1.has_value() == input.asset2.has_value()) &&
            (input.asset1.has_value() != ammID.has_value()),
        "xrpl::rpc::AMMInfoHandler::process : asset1 and asset2 do match");

    auto const ammKeylet = [&] {
        if (input.asset1 && input.asset2)
            return keylet::amm(*input.asset1, *input.asset2);
        XRPL_ASSERT(ammID, "xrpl::rpc::AMMInfoHandler::process : ammID is set");
        return keylet::amm(*ammID);
    }();
    auto const amm = ledger->read(ammKeylet);
    if (!amm)
        return fail(RpcActNotFound);

    Asset const asset1 = input.asset1 ? *input.asset1 : Asset{(*amm)[sfAsset]};
    Asset const asset2 = input.asset2 ? *input.asset2 : Asset{(*amm)[sfAsset2]};

    auto const ammAccountID = amm->getAccountID(sfAccount);

    // provide funds if frozen, specify asset_frozen flag
    auto const [asset1Balance, asset2Balance] = ammPoolHolds(
        *ledger,
        ammAccountID,
        asset1,
        asset2,
        FreezeHandling::IgnoreFreeze,
        AuthHandling::IgnoreAuth,
        context.j);
    auto const lptAMMBalance =
        accountID ? ammLPHolds(*ledger, *amm, *accountID, context.j) : (*amm)[sfLPTokenBalance];

    json::Value ammResult;
    asset1Balance.setJson(ammResult[jss::amount]);
    asset2Balance.setJson(ammResult[jss::amount2]);
    lptAMMBalance.setJson(ammResult[jss::lp_token]);
    ammResult[jss::trading_fee] = (*amm)[sfTradingFee];
    ammResult[jss::account] = to_string(ammAccountID);
    json::Value voteSlots(json::ValueType::Array);
    if (amm->isFieldPresent(sfVoteSlots))
    {
        for (auto const& voteEntry : amm->getFieldArray(sfVoteSlots))
        {
            json::Value vote;
            vote[jss::account] = to_string(voteEntry.getAccountID(sfAccount));
            vote[jss::trading_fee] = voteEntry[sfTradingFee];
            vote[jss::vote_weight] = voteEntry[sfVoteWeight];
            voteSlots.append(std::move(vote));
        }
    }
    if (voteSlots.size() > 0)
        ammResult[jss::vote_slots] = std::move(voteSlots);
    XRPL_ASSERT(
        amm->isFieldPresent(sfAuctionSlot),
        "xrpl::rpc::AMMInfoHandler::process : auction slot is present");
    if (amm->isFieldPresent(sfAuctionSlot))
    {
        auto const& auctionSlot = safeDowncast<STObject const&>(amm->peekAtField(sfAuctionSlot));
        if (auctionSlot.isFieldPresent(sfAccount))
        {
            json::Value auction;
            auto const timeSlot = ammAuctionTimeSlot(
                ledger->header().parentCloseTime.time_since_epoch().count(), auctionSlot);
            auction[jss::time_interval] = timeSlot ? *timeSlot : kAuctionSlotTimeIntervals;
            auctionSlot[sfPrice].setJson(auction[jss::price]);
            auction[jss::discounted_fee] = auctionSlot[sfDiscountedFee];
            auction[jss::account] = to_string(auctionSlot.getAccountID(sfAccount));
            auction[jss::expiration] =
                toIso8601(NetClock::time_point{NetClock::duration{auctionSlot[sfExpiration]}});
            if (auctionSlot.isFieldPresent(sfAuthAccounts))
            {
                json::Value auth;
                for (auto const& acct : auctionSlot.getFieldArray(sfAuthAccounts))
                {
                    json::Value jv;
                    jv[jss::account] = to_string(acct.getAccountID(sfAccount));
                    auth.append(jv);
                }
                auction[jss::auth_accounts] = auth;
            }
            ammResult[jss::auction_slot] = std::move(auction);
        }
    }

    if (!isXRP(asset1Balance))
    {
        ammResult[jss::asset_frozen] = isFrozen(*ledger, ammAccountID, asset1);
    }
    if (!isXRP(asset2Balance))
    {
        ammResult[jss::asset2_frozen] = isFrozen(*ledger, ammAccountID, asset2);
    }

    output.amm = std::move(ammResult);
    return output;
}

void
AMMInfoHandler::writeResult(json::Value& value, Output const& output) const
{
    injectLedgerFields(*output.ledger, context_.get(), value);

    if (output.error.has_value())
    {
        injectSpecError(value, *output.error);
        return;
    }

    value[jss::amm] = output.amm;
}

}  // namespace xrpl::rpc
