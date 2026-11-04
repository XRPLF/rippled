#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/RPCLedgerHelpers.h>
#include <xrpld/rpc/handlers/perp/PerpRPCHelpers.h>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/Sandbox.h>
#include <xrpl/ledger/helpers/PerpHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>  // IWYU pragma: keep
#include <xrpl/protocol/jss.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

namespace xrpl {

namespace {

/**
 * The margin account a request names, by margin_account_id or by account and exchange_id.
 */
std::optional<uint256>
parseMarginAccount(json::Value const& params, json::Value& result)
{
    bool const hasID = params.isMember(jss::margin_account_id);
    bool const hasAccount = params.isMember(jss::account);
    bool const hasExchange = params.isMember(jss::exchange_id);
    if (hasID && !hasAccount && !hasExchange)
        return rpc::parsePerpID(params, jss::margin_account_id, result);

    if (hasID || !hasAccount || !hasExchange)
    {
        rpc::injectError(
            RpcInvalidParams,
            "Must specify either 'margin_account_id' or both 'account' and 'exchange_id'.",
            result);
        return std::nullopt;
    }

    auto const& account = params[jss::account];
    auto const id = account.isString() ? parseBase58<AccountID>(account.asString())
                                       : std::optional<AccountID>{};
    if (!id)
    {
        rpc::injectError(
            RpcActMalformed, rpc::expectedFieldMessage(jss::account, "AccountID"), result);
        return std::nullopt;
    }
    auto const exchangeID = rpc::parsePerpID(params, jss::exchange_id, result);
    if (!exchangeID)
        return std::nullopt;
    return keylet::marginAccount(*id, *exchangeID).key;
}

/**
 * The index at which a position's account reaches its maintenance requirement, other markets
 * held at their indexes: the p solving equity + size (p - index) = otherMM + req(|size| p), tier
 * by tier, where req(x) = x r_j - D_j in tier j.
 *
 * @param equity The account's equity.
 * @param maintenance The account's maintenance requirement, this position's included.
 * @return The price, or nullopt when no positive price reaches the requirement.
 */
std::optional<Number>
liquidationPrice(
    Number const& equity,
    Number const& maintenance,
    STLedgerEntry const& market,
    STLedgerEntry const& position)
{
    Number const size = position[sfSize];
    Number const index = market[sfIndexPrice];
    auto const& tiers = market.getFieldArray(sfMarginTiers);
    // This position's requirement, rounded as the account's requirement sums it.
    Number own;
    {
        NumberRoundModeGuard const mg(Number::RoundingMode::Upward);
        own = perp::marginRequirement(abs(size) * index, tiers, false);
    }

    NumberRoundModeGuard const mg(Number::RoundingMode::ToNearest);
    Number const otherMM = maintenance - own;
    Number const base = equity - (size * index);
    Number lower;
    for (std::size_t i = 0; i < tiers.size(); ++i)
    {
        auto const& tier = tiers[i];
        Number const rate = Number{tier[sfMaintenanceMarginRate]} / Number{perp::kRateUnity};
        Number const upper = tier[sfNotionalMax];
        Number const deduction = (upper * rate) - perp::marginRequirement(upper, tiers, false);
        Number const slope = size - (abs(size) * rate);
        if (slope != Number{})
        {
            Number const price = (otherMM - deduction - base) / slope;
            Number const notional = abs(size) * price;
            bool const last = i + 1 == tiers.size();
            if (price > Number{} && notional > lower && (last || notional <= upper))
                return price;
        }
        lower = upper;
    }
    return std::nullopt;
}

/**
 * One position of a margin account as margin_account_info reports it.
 */
json::Value
positionJson(
    STLedgerEntry const& exchange,
    STLedgerEntry const& market,
    STLedgerEntry const& position,
    perp::MarginSummary const& summary)
{
    Number const size = position[sfSize];
    Number const entryNotional = position[sfEntryNotional];
    Number const index = market[sfIndexPrice];
    auto const charge = perp::settlementCharge(exchange, market, position);

    json::Value jv;
    jv[jss::market_id] = to_string(market.key());
    jv[jss::size] = to_string(size);
    if (size != Number{})
    {
        NumberRoundModeGuard const mg(Number::RoundingMode::ToNearest);
        jv[jss::entry_price] = to_string(entryNotional / size);
    }
    {
        NumberRoundModeGuard const mg(Number::RoundingMode::Downward);
        jv[jss::upnl] = to_string((size * index) - entryNotional);
    }
    jv[jss::funding_owed] = to_string(charge.funding);
    jv[jss::loss_owed] = to_string(charge.loss);

    // A Settled position has a fixed value and cannot be liquidated.
    std::uint8_t const status = market[sfMarketStatus];
    if (size != Number{} && status != perp::kMarketSettled)
    {
        if (auto const price = liquidationPrice(
                summary.equity, summary.requirements.maintenance, market, position))
            jv[jss::estimated_liquidation_price] = to_string(*price);
    }
    return jv;
}

}  // namespace

json::Value
doMarginAccountInfo(rpc::JsonContext const& context)
{
    std::shared_ptr<ReadView const> ledger;
    auto result = rpc::lookupLedger(ledger, context);
    if (!ledger)
        return result;

    auto const id = parseMarginAccount(context.params, result);
    if (!id)
        return result;

    // A zero key names no entry, and the ledger refuses to be asked for one.
    auto const stored = *id == beast::kZero ? nullptr : ledger->read(keylet::marginAccount(*id));
    if (!stored)
    {
        rpc::injectError(RpcEntryNotFound, result);
        return result;
    }
    result[jss::margin_account] = stored->getJson(JsonOptions::Values::None);

    // The figures are those the next transaction of this ledger finds after refreshing the
    // account's markets.
    Sandbox sb(ledger.get(), TapNone);
    rpc::refreshPerpMarkets(sb, stored->getFieldV256(sfMarkets), context.j);
    auto const account = sb.read(keylet::marginAccount(*id));
    auto const exchange = sb.read(keylet::perpExchange((*stored)[sfExchangeID]));

    if (!account || !exchange)
    {
        rpc::injectError(RpcInternal, result);  // LCOV_EXCL_LINE
        return result;                          // LCOV_EXCL_LINE
    }

    // A Stale market is valued at its last quorum index, as liquidation values it.
    auto const summary = perp::liquidationSummary(sb, *account);
    if (!summary)
    {
        rpc::injectError(RpcInternal, result);  // LCOV_EXCL_LINE
        return result;                          // LCOV_EXCL_LINE
    }
    // A Stale market leaves nothing withdrawable.
    auto const available = perp::withdrawable(sb, *account);

    result[jss::equity] = to_string(summary->equity);
    result[jss::im] = to_string(summary->requirements.initial);
    result[jss::mm] = to_string(summary->requirements.maintenance);
    result[jss::withdrawable] = to_string(std::max(available.value_or(Number{}), Number{}));
    result[jss::liquidatable] = summary->equity < summary->requirements.maintenance;
    result[jss::waterfall_credit] = to_string(Number{(*account)[sfWaterfallCredit]});

    json::Value positions{json::ValueType::Array};
    for (auto const& marketID : account->getFieldV256(sfMarkets))
    {
        auto const market = sb.read(keylet::perpMarket(marketID));
        auto const position = sb.read(keylet::marginPosition(*id, marketID));
        if (market && position)
            positions.append(positionJson(*exchange, *market, *position, *summary));
    }
    result[jss::positions] = positions;
    return result;
}

}  // namespace xrpl
