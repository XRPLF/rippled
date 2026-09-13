#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/RPCLedgerHelpers.h>

#include <expected>
#include <xrpl/basics/Log.h>
#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/AMMCore.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/jss.h>

#include <boost/endian/conversion.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace xrpl {

// Defined in AMMInfo.cpp
std::expected<Asset, ErrorCodeI>
getAsset(json::Value const& v, beast::Journal j);

namespace {

constexpr std::int32_t kMinTick = -887272;
constexpr std::int32_t kMaxTick = 887272;
constexpr unsigned int kAmmTicksLimitDefault = 200;
constexpr unsigned int kAmmTicksLimitMax = 400;

// Decode the offset-binary tickIndex stored in the low 64 bits of an AMM_TICK
// structured key.
std::int32_t
decodeTickFromKey(uint256 const& key)
{
    static constexpr std::int64_t tickOffset = 887272;
    auto const encoded = boost::endian::big_to_native(((std::uint64_t const*)key.end())[-1]);
    return static_cast<std::int32_t>(static_cast<std::int64_t>(encoded) - tickOffset);
}

// True if `key` is strictly within (base, end) for the given pool.
bool
sameScope(uint256 const& key, uint256 const& base, uint256 const& end)
{
    return key > base && key < end;
}

}  // namespace

json::Value
doAMMTicks(rpc::JsonContext& context)
{
    auto const& params(context.params);
    json::Value result;

    std::shared_ptr<ReadView const> ledger;
    result = rpc::lookupLedger(ledger, context);
    if (!ledger)
        return result;

    // Parse asset / asset2 / amm_account (same pattern as amm_info).
    std::optional<Asset> asset1;
    std::optional<Asset> asset2;
    std::optional<uint256> ammIDFromAccount;

    static constexpr auto kInvalid = [](json::Value const& p) -> bool {
        return (p.isMember(jss::asset) != p.isMember(jss::asset2)) ||
            (p.isMember(jss::asset) == p.isMember(jss::amm_account));
    };

    if (context.apiVersion < 3 && kInvalid(params))
    {
        rpc::injectError(RpcInvalidParams, result);
        return result;
    }

    if (params.isMember(jss::asset))
    {
        if (auto const i = getAsset(params[jss::asset], context.j))
            asset1 = *i;
        else
        {
            rpc::injectError(i.error(), result);
            return result;
        }
    }

    if (params.isMember(jss::asset2))
    {
        if (auto const i = getAsset(params[jss::asset2], context.j))
            asset2 = *i;
        else
        {
            rpc::injectError(i.error(), result);
            return result;
        }
    }

    if (params.isMember(jss::amm_account))
    {
        auto const id = parseBase58<AccountID>(params[jss::amm_account].asString());
        if (!id)
        {
            rpc::injectError(RpcActMalformed, result);
            return result;
        }
        auto const sle = ledger->read(keylet::account(*id));
        if (!sle)
        {
            rpc::injectError(RpcActMalformed, result);
            return result;
        }
        ammIDFromAccount = sle->getFieldH256(sfAMMID);
        if (ammIDFromAccount->isZero())
        {
            rpc::injectError(RpcActNotFound, result);
            return result;
        }
    }

    if (context.apiVersion >= 3 && kInvalid(params))
    {
        rpc::injectError(RpcInvalidParams, result);
        return result;
    }

    // Curve type must be CL.
    std::uint8_t curveType = CtConstantProduct;
    if (params.isMember(jss::curve_type))
        curveType = static_cast<std::uint8_t>(params[jss::curve_type].asUInt());
    else if (asset1 && asset2)
    {
        // No curve_type provided with asset/asset2 -> cannot identify which CL
        // pool the caller wants.
        rpc::injectError(
            RpcInvalidParams,
            "amm_ticks requires curve_type=1 (ConcentratedLiquidity).",
            result);
        return result;
    }

    auto const ammKeylet = [&]() {
        if (asset1 && asset2)
            return keylet::amm(*asset1, *asset2, curveType);
        return keylet::amm(*ammIDFromAccount);
    }();
    auto const amm = ledger->read(ammKeylet);
    if (!amm)
    {
        rpc::injectError(RpcActNotFound, result);
        return result;
    }

    // If we resolved via amm_account, derive the curve type from the SLE so we
    // can validate it.
    if (ammIDFromAccount)
        curveType = amm->getFieldU8(sfCurveType);

    if (curveType != CtConcentratedLiquidity)
    {
        rpc::injectError(
            RpcInvalidParams,
            "amm_ticks only valid for ConcentratedLiquidity (curve_type 1) pools.",
            result);
        return result;
    }

    // Optional tick_lower / tick_upper filters.
    std::int32_t tickLower = kMinTick;
    std::int32_t tickUpper = kMaxTick;
    if (params.isMember(jss::tick_lower))
    {
        if (!params[jss::tick_lower].isIntegral())
        {
            rpc::injectError(RpcInvalidParams, "tick_lower must be an integer.", result);
            return result;
        }
        tickLower = params[jss::tick_lower].asInt();
    }
    if (params.isMember(jss::tick_upper))
    {
        if (!params[jss::tick_upper].isIntegral())
        {
            rpc::injectError(RpcInvalidParams, "tick_upper must be an integer.", result);
            return result;
        }
        tickUpper = params[jss::tick_upper].asInt();
    }
    if (tickLower < kMinTick || tickUpper > kMaxTick || tickLower > tickUpper)
    {
        rpc::injectError(RpcInvalidParams, "Invalid tick range.", result);
        return result;
    }

    // Limit.
    unsigned int limit = kAmmTicksLimitDefault;
    if (params.isMember(jss::limit))
    {
        if (!params[jss::limit].isIntegral())
        {
            rpc::injectError(RpcInvalidParams, "limit must be an integer.", result);
            return result;
        }
        auto const requested = params[jss::limit].asUInt();
        limit = std::min<unsigned int>(requested, kAmmTicksLimitMax);
        if (limit == 0)
            limit = kAmmTicksLimitDefault;
    }

    auto const ammID = amm->key();
    auto const baseKey = keylet::ammTickBase(ammID).key;
    auto const endKey = keylet::ammTickEnd(ammID).key;

    // Determine where iteration starts.
    uint256 cursor = baseKey;
    if (params.isMember(jss::marker))
    {
        json::Value const& marker = params[jss::marker];
        if (!marker.isString())
        {
            rpc::injectError(RpcInvalidParams, "marker must be a string.", result);
            return result;
        }
        uint256 m;
        if (!m.parseHex(marker.asString()))
        {
            rpc::injectError(RpcInvalidParams, "Invalid marker.", result);
            return result;
        }
        if (!sameScope(m, baseKey, endKey))
        {
            rpc::injectError(RpcInvalidParams, "Marker does not belong to this pool.", result);
            return result;
        }
        cursor = m;
    }

    json::Value ticks(json::ValueType::Array);
    std::optional<uint256> lastKey;
    unsigned int collected = 0;

    while (collected < limit)
    {
        auto const next = ledger->succ(cursor, endKey);
        if (!next)
            break;
        if (!sameScope(*next, baseKey, endKey))
            break;

        auto const sle = ledger->read(keylet::ammTick(*next));
        cursor = *next;
        if (!sle || sle->getType() != ltAMM_TICK)
            continue;
        // Defensive scope check: the SLE must belong to this pool.
        if (sle->getFieldH256(sfAMMID) != ammID)
            continue;

        std::int32_t const tickIndex = decodeTickFromKey(*next);
        if (tickIndex < tickLower || tickIndex > tickUpper)
            continue;

        json::Value entry;
        entry[jss::tick_index] = tickIndex;
        entry[jss::liquidity_net] = std::to_string(sle->getFieldU64(sfLiquidityNet));
        entry[jss::liquidity_gross] = std::to_string(sle->getFieldU64(sfLiquidityGross));
        entry[jss::fee_growth_outside_0] =
            to_string(Number{sle->getFieldNumber(sfFeeGrowthOutside0)});
        entry[jss::fee_growth_outside_1] =
            to_string(Number{sle->getFieldNumber(sfFeeGrowthOutside1)});
        entry[jss::index] = to_string(*next);
        ticks.append(std::move(entry));
        lastKey = *next;
        ++collected;
    }

    // If we hit the limit, see whether more results would be available so we
    // can decide whether to emit a marker.
    if (collected == limit && lastKey)
    {
        auto const peek = ledger->succ(*lastKey, endKey);
        if (peek && sameScope(*peek, baseKey, endKey))
            result[jss::marker] = to_string(*lastKey);
    }

    result[jss::amm_id] = to_string(ammID);
    if (amm->isFieldPresent(sfCurrentTick))
        result[jss::current_tick] = amm->getFieldI32(sfCurrentTick);
    if (amm->isFieldPresent(sfSqrtPriceX96))
        result[jss::sqrt_price_x96] = to_string(amm->getFieldH256(sfSqrtPriceX96));
    if (amm->isFieldPresent(sfActiveLiquidity))
        result[jss::active_liquidity] = std::to_string(amm->getFieldU64(sfActiveLiquidity));
    result[jss::ticks] = std::move(ticks);

    if (!result.isMember(jss::ledger_index) && !result.isMember(jss::ledger_hash))
        result[jss::ledger_current_index] = ledger->header().seq;
    result[jss::validated] = context.ledgerMaster.isValidated(*ledger);

    return result;
}

}  // namespace xrpl
