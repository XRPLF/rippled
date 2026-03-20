#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/RPCLedgerHelpers.h>

#include <xrpl/json/json_value.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/View.h>
#include <xrpl/protocol/CLAMMCore.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/tx/transactors/dex/CLAMMHelpers.h>

#include <cmath>
#include <cstdio>
#include <string>

namespace xrpl {

namespace {

// Helper to convert base_uint<128> field value to hex string for JSON.
std::string
uint128FieldToHex(SLE const& sle, SField const& field)
{
    return to_string(sle.getFieldH128(field));
}

// Helper to resolve a CLAMM pool SLE from request params.
// Supports "pool_id" (hex hash) or "asset" + "asset2" + "fee_tier".
std::shared_ptr<SLE const>
resolveCLAMMPool(
    ReadView const& ledger,
    Json::Value const& params,
    Json::Value& result)
{
    // Method 1: by pool_id
    if (params.isMember("pool_id"))
    {
        uint256 poolID;
        if (!poolID.parseHex(params["pool_id"].asString()))
        {
            result[jss::error] = "invalidParams";
            result[jss::error_message] = "Invalid pool_id";
            return nullptr;
        }
        if (auto sle = ledger.read(keylet::clamm(poolID)))
            return sle;
    }
    // Method 2: by asset + asset2 + fee_tier
    else if (params.isMember("asset") && params.isMember("asset2") &&
             params.isMember("fee_tier"))
    {
        try
        {
            auto const asset = issueFromJson(params["asset"]);
            auto const asset2 = issueFromJson(params["asset2"]);
            auto const feeTierRaw = params["fee_tier"].asUInt();

            if (feeTierRaw > CLAMM_MAX_FEE_TIER)
            {
                result[jss::error] = "invalidParams";
                result[jss::error_message] = "Invalid fee_tier";
                return nullptr;
            }

            auto const feeTier = static_cast<std::uint8_t>(feeTierRaw);

            if (!isValidCLAMMFeeTier(feeTier))
            {
                result[jss::error] = "invalidParams";
                result[jss::error_message] = "Invalid fee_tier";
                return nullptr;
            }

            auto const poolKeylet = keylet::clamm(
                Asset(asset), Asset(asset2), feeTier);
            if (auto sle = ledger.read(poolKeylet))
                return sle;
        }
        catch (...)
        {
            result[jss::error] = "invalidParams";
            result[jss::error_message] = "Invalid asset specification";
            return nullptr;
        }
    }
    else
    {
        result[jss::error] = "invalidParams";
        result[jss::error_message] =
            "Must provide pool_id or (asset, asset2, fee_tier)";
        return nullptr;
    }

    result[jss::error] = "actNotFound";
    result[jss::error_message] = "CLAMM pool not found";
    return nullptr;
}

}  // namespace

Json::Value
doCLAMMInfo(RPC::JsonContext& context)
{
    auto const& params(context.params);
    Json::Value result;

    std::shared_ptr<ReadView const> ledger;
    result = RPC::lookupLedger(ledger, context);
    if (!ledger)
        return result;

    auto sleClamm = resolveCLAMMPool(*ledger, params, result);
    if (!sleClamm)
        return result;

    auto const& clammRef = *sleClamm;
    auto const ammAccountID = clammRef.getAccountID(sfAccount);

    Json::Value poolInfo;
    poolInfo[jss::account] = toBase58(ammAccountID);
    poolInfo["pool_id"] = to_string(sleClamm->key());
    poolInfo["asset"] =
        to_json(std::as_const(clammRef)[sfAsset].get<Issue>());
    poolInfo["asset2"] =
        to_json(std::as_const(clammRef)[sfAsset2].get<Issue>());
    poolInfo["fee_tier"] = clammRef.getFieldU8(sfFeeTier);
    poolInfo["tick_spacing"] = clammRef.getFieldU16(sfTickSpacing);
    poolInfo["trading_fee"] = clammRef.getFieldU16(sfTradingFee);
    poolInfo["current_tick"] = clammRef.getFieldI32(sfCurrentTick);
    poolInfo["sqrt_price"] = uint128FieldToHex(clammRef, sfSqrtPrice);
    poolInfo["liquidity"] =
        uint128FieldToHex(clammRef, sfLiquidityAmount);
    poolInfo["fee_growth_global0"] =
        uint128FieldToHex(clammRef, sfFeeGrowthGlobal0);
    poolInfo["fee_growth_global1"] =
        uint128FieldToHex(clammRef, sfFeeGrowthGlobal1);
    poolInfo["protocol_fees0"] =
        std::to_string(clammRef.getFieldU64(sfProtocolFees0));
    poolInfo["protocol_fees1"] =
        std::to_string(clammRef.getFieldU64(sfProtocolFees1));

    // Pool token balances
    auto const& clammRefConst = std::as_const(clammRef);
    auto const issue0 = clammRefConst[sfAsset].get<Issue>();
    auto const issue1 = clammRefConst[sfAsset2].get<Issue>();

    // Check if pool assets are frozen via trust lines
    bool poolFrozen = false;
    for (auto const& issue : {issue0, issue1})
    {
        if (!isXRP(issue) &&
            isFrozen(*ledger, ammAccountID, issue))
        {
            poolFrozen = true;
            break;
        }
    }
    if (poolFrozen)
        poolInfo["frozen"] = true;
    auto const balance0 = accountHolds(
        *ledger, ammAccountID, issue0,
        FreezeHandling::fhZERO_IF_FROZEN, context.j);
    auto const balance1 = accountHolds(
        *ledger, ammAccountID, issue1,
        FreezeHandling::fhZERO_IF_FROZEN, context.j);
    poolInfo["amount"] = balance0.getJson(JsonOptions::none);
    poolInfo["amount2"] = balance1.getJson(JsonOptions::none);

    // Vote slots
    if (clammRef.isFieldPresent(sfVoteSlots))
    {
        Json::Value voteSlots(Json::arrayValue);
        for (auto const& entry : clammRef.getFieldArray(sfVoteSlots))
        {
            Json::Value vote;
            vote[jss::account] =
                toBase58(entry.getAccountID(sfAccount));
            vote[jss::trading_fee] =
                entry[~sfTradingFee].value_or(0);
            vote[jss::vote_weight] = entry[sfVoteWeight];
            voteSlots.append(std::move(vote));
        }
        poolInfo["vote_slots"] = std::move(voteSlots);
    }

    // Auction slot
    if (clammRef.isFieldPresent(sfAuctionSlot))
    {
        auto const& auctionSlot = static_cast<STObject const&>(
            clammRef.peekAtField(sfAuctionSlot));
        if (auctionSlot.isFieldPresent(sfAccount))
        {
            Json::Value auction;
            auction[jss::account] =
                toBase58(auctionSlot.getAccountID(sfAccount));
            auction[jss::expiration] =
                auctionSlot[~sfExpiration].value_or(0u);
            auction["discounted_fee"] =
                auctionSlot[~sfDiscountedFee].value_or(0);
            if (auctionSlot.isFieldPresent(sfPrice))
                auction["price"] =
                    auctionSlot.getFieldAmount(sfPrice)
                        .getJson(JsonOptions::none);
            if (auctionSlot.isFieldPresent(sfAuthAccounts))
            {
                Json::Value auth(Json::arrayValue);
                for (auto const& acct :
                     auctionSlot.getFieldArray(sfAuthAccounts))
                {
                    Json::Value jv;
                    jv[jss::account] =
                        toBase58(acct.getAccountID(sfAccount));
                    auth.append(std::move(jv));
                }
                auction["auth_accounts"] = std::move(auth);
            }
            poolInfo["auction_slot"] = std::move(auction);
        }
    }

    result["pool"] = std::move(poolInfo);
    if (!result.isMember(jss::ledger_index) &&
        !result.isMember(jss::ledger_hash))
        result[jss::ledger_current_index] = ledger->header().seq;
    result[jss::validated] =
        context.ledgerMaster.isValidated(*ledger);
    return result;
}

Json::Value
doCLAMMPositions(RPC::JsonContext& context)
{
    auto const& params(context.params);
    Json::Value result;

    std::shared_ptr<ReadView const> ledger;
    result = RPC::lookupLedger(ledger, context);
    if (!ledger)
        return result;

    // Lookup by nftoken_id (single position)
    if (params.isMember("nftoken_id"))
    {
        uint256 nfTokenID;
        if (!nfTokenID.parseHex(params["nftoken_id"].asString()))
        {
            result[jss::error] = "invalidParams";
            result[jss::error_message] = "Invalid nftoken_id";
            return result;
        }

        auto slePos =
            ledger->read(keylet::clammPosition(nfTokenID));
        if (!slePos)
        {
            result[jss::error] = "actNotFound";
            result[jss::error_message] = "Position not found";
            return result;
        }

        Json::Value pos;
        auto const posPoolID = slePos->getFieldH256(sfPoolID);
        pos["pool_id"] = to_string(posPoolID);
        pos["nftoken_id"] =
            to_string(slePos->getFieldH256(sfNFTokenID));
        pos["owner"] = toBase58(slePos->getAccountID(sfOwner));
        pos["lower_tick"] = slePos->getFieldI32(sfLowerTick);
        pos["upper_tick"] = slePos->getFieldI32(sfUpperTick);
        pos["liquidity"] =
            uint128FieldToHex(*slePos, sfLiquidityAmount);
        pos["fee_growth_inside0_last"] =
            uint128FieldToHex(*slePos, sfFeeGrowthInside0Last);
        pos["fee_growth_inside1_last"] =
            uint128FieldToHex(*slePos, sfFeeGrowthInside1Last);
        pos["tokens_owed0"] =
            std::to_string(slePos->getFieldU64(sfTokensOwed0));
        pos["tokens_owed1"] =
            std::to_string(slePos->getFieldU64(sfTokensOwed1));

        // Determine if position is in range
        if (auto slePool = ledger->read(keylet::clamm(posPoolID)))
        {
            auto const currentTick = slePool->getFieldI32(sfCurrentTick);
            auto const lowerTick = slePos->getFieldI32(sfLowerTick);
            auto const upperTick = slePos->getFieldI32(sfUpperTick);
            pos["in_range"] =
                (currentTick >= lowerTick && currentTick < upperTick);
        }

        Json::Value positions(Json::arrayValue);
        positions.append(std::move(pos));
        result["positions"] = std::move(positions);
        return result;
    }

    // Lookup by account
    if (!params.isMember(jss::account))
    {
        result[jss::error] = "invalidParams";
        result[jss::error_message] =
            "Must provide nftoken_id or account";
        return result;
    }

    auto const accountID =
        parseBase58<AccountID>(params[jss::account].asString());
    if (!accountID)
    {
        result[jss::error] = "invalidParams";
        result[jss::error_message] = "Invalid account";
        return result;
    }

    std::optional<uint256> filterPoolID;
    if (params.isMember("pool_id"))
    {
        uint256 poolID;
        if (!poolID.parseHex(params["pool_id"].asString()))
        {
            result[jss::error] = "invalidParams";
            result[jss::error_message] = "Invalid pool_id";
            return result;
        }
        filterPoolID = poolID;
    }

    constexpr std::uint32_t maxPositionsLimit = 256;
    auto const limit = std::min(
        params.isMember("limit") ? params["limit"].asUInt() : 100u,
        maxPositionsLimit);

    // Pagination: marker is the last nftoken_id from previous page
    std::optional<uint256> marker;
    if (params.isMember(jss::marker))
    {
        uint256 m;
        if (!m.parseHex(params[jss::marker].asString()))
        {
            result[jss::error] = "invalidParams";
            result[jss::error_message] = "Invalid marker";
            return result;
        }
        marker = m;
    }

    Json::Value positions(Json::arrayValue);
    std::uint32_t count = 0;
    bool pastMarker = !marker.has_value();
    std::optional<uint256> lastNFTokenID;

    forEachItem(
        *ledger,
        *accountID,
        [&](std::shared_ptr<SLE const> const& sle) {
            if (count > limit)
                return;
            if (sle->getType() != ltCLAMM_POSITION)
                return;

            auto const nftID = sle->getFieldH256(sfNFTokenID);

            // Skip entries until we pass the marker
            if (!pastMarker)
            {
                if (nftID == *marker)
                    pastMarker = true;
                return;
            }

            if (count >= limit)
            {
                // We have one more item -- set marker for next page
                lastNFTokenID = nftID;
                ++count;
                return;
            }

            if (filterPoolID &&
                sle->getFieldH256(sfPoolID) != *filterPoolID)
                return;

            Json::Value pos;
            auto const posPoolID = sle->getFieldH256(sfPoolID);
            pos["pool_id"] = to_string(posPoolID);
            pos["nftoken_id"] = to_string(nftID);
            pos["owner"] = toBase58(sle->getAccountID(sfOwner));
            pos["lower_tick"] = sle->getFieldI32(sfLowerTick);
            pos["upper_tick"] = sle->getFieldI32(sfUpperTick);
            pos["liquidity"] =
                uint128FieldToHex(*sle, sfLiquidityAmount);
            pos["tokens_owed0"] =
                std::to_string(sle->getFieldU64(sfTokensOwed0));
            pos["tokens_owed1"] =
                std::to_string(sle->getFieldU64(sfTokensOwed1));

            // Determine if position is in range
            if (auto slePool = ledger->read(keylet::clamm(posPoolID)))
            {
                auto const currentTick =
                    slePool->getFieldI32(sfCurrentTick);
                auto const lowerTick = sle->getFieldI32(sfLowerTick);
                auto const upperTick = sle->getFieldI32(sfUpperTick);
                pos["in_range"] =
                    (currentTick >= lowerTick && currentTick < upperTick);
            }

            lastNFTokenID = nftID;
            positions.append(std::move(pos));
            ++count;
        });

    result["positions"] = std::move(positions);

    // If we exceeded the limit, there are more results
    if (count > limit && lastNFTokenID)
        result[jss::marker] = to_string(*lastNFTokenID);

    return result;
}

Json::Value
doCLAMMTicks(RPC::JsonContext& context)
{
    auto const& params(context.params);
    Json::Value result;

    std::shared_ptr<ReadView const> ledger;
    result = RPC::lookupLedger(ledger, context);
    if (!ledger)
        return result;

    // Resolve pool (supports both pool_id and asset/asset2/fee_tier)
    auto sleClamm = resolveCLAMMPool(*ledger, params, result);
    if (!sleClamm)
        return result;

    auto const poolID = sleClamm->key();
    auto const tickSpacing = sleClamm->getFieldU16(sfTickSpacing);

    // Optional tick range filter
    auto minTick =
        params.isMember("min_tick") ? params["min_tick"].asInt() : CLAMM_MIN_TICK;
    auto const maxTick =
        params.isMember("max_tick") ? params["max_tick"].asInt() : CLAMM_MAX_TICK;
    constexpr std::uint32_t maxTicksLimit = 256;
    auto const limit = std::min(
        params.isMember("limit") ? params["limit"].asUInt() : 100u,
        maxTicksLimit);

    // Pagination: marker is the last tick_index from previous page
    if (params.isMember(jss::marker))
    {
        auto const markerTick = params[jss::marker].asInt();
        // Start from the tick after the marker
        minTick = markerTick + static_cast<std::int32_t>(tickSpacing);
    }

    // Scan aligned ticks in the requested range
    Json::Value ticks(Json::arrayValue);
    std::uint32_t count = 0;

    auto tick = minTick;
    // Align to tick spacing
    if (tick >= 0)
        tick = (tick / tickSpacing) * tickSpacing;
    else
        tick = ((tick - tickSpacing + 1) / tickSpacing) * tickSpacing;

    std::int32_t lastTick = tick;
    for (; tick <= maxTick && count < limit;
         tick += static_cast<std::int32_t>(tickSpacing))
    {
        auto sleTick =
            ledger->read(keylet::clammTick(poolID, tick));
        if (!sleTick)
            continue;

        Json::Value tickInfo;
        tickInfo["tick_index"] = sleTick->getFieldI32(sfTickIndex);
        tickInfo["liquidity_gross"] =
            uint128FieldToHex(*sleTick, sfLiquidityGross);
        tickInfo["liquidity_net"] =
            to_string(sleTick->getFieldH128(sfLiquidityNet));
        tickInfo["fee_growth_outside0"] =
            uint128FieldToHex(*sleTick, sfFeeGrowthOutside0);
        tickInfo["fee_growth_outside1"] =
            uint128FieldToHex(*sleTick, sfFeeGrowthOutside1);

        lastTick = tick;
        ticks.append(std::move(tickInfo));
        ++count;
    }

    result["ticks"] = std::move(ticks);

    // If we hit the limit and there might be more ticks, set marker
    if (count >= limit && tick <= maxTick)
        result[jss::marker] = lastTick;

    return result;
}

Json::Value
doCLAMMQuote(RPC::JsonContext& context)
{
    auto const& params(context.params);
    Json::Value result;

    std::shared_ptr<ReadView const> ledger;
    result = RPC::lookupLedger(ledger, context);
    if (!ledger)
        return result;

    if (!params.isMember("amount"))
    {
        result[jss::error] = "invalidParams";
        result[jss::error_message] = "Must provide amount";
        return result;
    }

    // Resolve pool
    auto sleClamm = resolveCLAMMPool(*ledger, params, result);
    if (!sleClamm)
        return result;

    auto const poolID = sleClamm->key();
    auto const& clammRef = std::as_const(*sleClamm);
    auto const issue0 = clammRef[sfAsset].get<Issue>();
    auto const issue1 = clammRef[sfAsset2].get<Issue>();
    auto const tradingFee = sleClamm->getFieldU16(sfTradingFee);
    auto const tickSpacing = sleClamm->getFieldU16(sfTickSpacing);

    // Parse the input amount
    std::uint64_t amountIn = 0;
    bool zeroForOne = true;

    try
    {
        if (params["amount"].isObject())
        {
            amountIn = std::stoull(params["amount"]["value"].asString());
            auto const currency =
                params["amount"].isMember("currency")
                ? params["amount"]["currency"].asString()
                : "";
            zeroForOne =
                (currency == to_string(issue0.currency));
        }
        else
        {
            amountIn = std::stoull(params["amount"].asString());
            if (params.isMember("direction"))
                zeroForOne =
                    (params["direction"].asString() == "zero_for_one");
        }
    }
    catch (std::exception const&)
    {
        return RPC::invalid_field_error("amount");
    }

    // Use shared swap simulation
    auto const sqrtPrice =
        clamm::fromSLEField(sleClamm->getFieldH128(sfSqrtPrice));
    auto const currentTick = sleClamm->getFieldI32(sfCurrentTick);
    auto const liquidity =
        clamm::fromSLEField(sleClamm->getFieldH128(sfLiquidityAmount));

    auto const sim = clamm::simulateSwap(
        *ledger,
        poolID,
        sqrtPrice,
        currentTick,
        liquidity,
        tickSpacing,
        tradingFee,
        amountIn,
        zeroForOne);

    auto const outputIssue = zeroForOne ? issue1 : issue0;
    auto const inputIssue = zeroForOne ? issue0 : issue1;

    Json::Value quote;
    quote["amount_in"] = std::to_string(sim.amountIn);
    quote["amount_out"] = std::to_string(sim.amountOut);
    quote["fee_amount"] = std::to_string(sim.feeAmount);
    quote["input_asset"] = to_json(inputIssue);
    quote["output_asset"] = to_json(outputIssue);
    quote["final_tick"] = sim.finalTick;
    quote["final_sqrt_price"] =
        to_string(clamm::toSLEField(sim.finalSqrtPrice));
    quote["ticks_crossed"] = sim.ticksCrossed;

    // price_impact = |1 - effectivePrice / initialPrice|
    // Use integer arithmetic for precision: scale by 10^6 for 6 decimal places.
    if (sim.amountIn > 0 && sqrtPrice > 0)
    {
        // initialPrice = sqrtPrice^2 / 2^192 (Q64.96 squared)
        // effectivePrice = amountOut/amountIn (zeroForOne) or amountIn/amountOut
        // Compute ratio = effectivePrice / initialPrice using cross-multiplication:
        //   ratio = (effectiveNum * 2^192) / (effectiveDen * sqrtPrice^2)
        // Then priceImpact = |1 - ratio|
        constexpr std::uint64_t SCALE = 1000000;  // 10^6
        auto const sqrtPrice256 = clamm::uint256(sqrtPrice);
        auto const priceSq = sqrtPrice256 * sqrtPrice256;
        auto const scale192 = clamm::uint256(1) << 192;

        clamm::uint256 effectiveNum, effectiveDen;
        if (zeroForOne)
        {
            effectiveNum = clamm::uint256(sim.amountOut);
            effectiveDen = clamm::uint256(sim.amountIn);
        }
        else
        {
            effectiveNum = clamm::uint256(sim.amountIn);
            effectiveDen = clamm::uint256(sim.amountOut);
        }

        // ratio_scaled = (effectiveNum * scale192 * SCALE) / (effectiveDen * priceSq)
        auto const denom = effectiveDen * priceSq;
        double priceImpact = 0.0;
        if (denom > 0)
        {
            auto const ratioScaled =
                (effectiveNum * scale192 * SCALE) / denom;
            // Clamp to avoid overflow when casting to int64
            auto const ratioVal = ratioScaled > clamm::uint256(INT64_MAX)
                ? INT64_MAX
                : static_cast<std::int64_t>(ratioScaled);
            auto const diff = std::abs(
                static_cast<std::int64_t>(SCALE) - ratioVal);
            priceImpact = static_cast<double>(diff) / SCALE;
        }

        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.6f", priceImpact);
        quote["price_impact"] = buf;
    }
    else
    {
        quote["price_impact"] = "0.000000";
    }

    result["quote"] = std::move(quote);
    return result;
}

}  // namespace xrpl
