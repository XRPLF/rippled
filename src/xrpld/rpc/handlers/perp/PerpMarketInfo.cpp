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
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STNumber.h>  // IWYU pragma: keep
#include <xrpl/protocol/STVector256.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/jss.h>

#include <cstdint>
#include <memory>
#include <optional>

namespace xrpl {

namespace {

/**
 * A currency code member of the request; nullopt after injecting rpcINVALID_PARAMS.
 */
std::optional<Currency>
parseCurrency(json::Value const& params, json::StaticString const& field, json::Value& result)
{
    Currency currency;
    auto const& value = params[field];
    if (!value.isString() || !toCurrency(currency, value.asString()) || currency == badCurrency())
    {
        rpc::injectError(RpcInvalidParams, rpc::expectedFieldMessage(field, "currency"), result);
        return std::nullopt;
    }
    return currency;
}

/**
 * The market a request names, by market_id or by exchange_id, base_asset and quote_asset.
 */
std::optional<uint256>
parseMarket(json::Value const& params, json::Value& result)
{
    bool const hasID = params.isMember(jss::market_id);
    bool const hasPair = params.isMember(jss::exchange_id) && params.isMember(jss::base_asset) &&
        params.isMember(jss::quote_asset);
    bool const hasPairPart = params.isMember(jss::exchange_id) ||
        params.isMember(jss::base_asset) || params.isMember(jss::quote_asset);
    if (hasID && !hasPairPart)
        return rpc::parsePerpID(params, jss::market_id, result);

    if (hasID || !hasPair)
    {
        rpc::injectError(
            RpcInvalidParams,
            "Must specify either 'market_id' or 'exchange_id', 'base_asset' and 'quote_asset'.",
            result);
        return std::nullopt;
    }

    auto const exchangeID = rpc::parsePerpID(params, jss::exchange_id, result);
    if (!exchangeID)
        return std::nullopt;
    auto const base = parseCurrency(params, jss::base_asset, result);
    if (!base)
        return std::nullopt;
    auto const quote = parseCurrency(params, jss::quote_asset, result);
    if (!quote)
        return std::nullopt;
    return keylet::perpMarket(*exchangeID, *base, *quote).key;
}

}  // namespace

json::Value
doPerpMarketInfo(rpc::JsonContext const& context)
{
    std::shared_ptr<ReadView const> ledger;
    auto result = rpc::lookupLedger(ledger, context);
    if (!ledger)
        return result;

    auto const id = parseMarket(context.params, result);
    if (!id)
        return result;

    // A zero key names no entry, and the ledger refuses to be asked for one.
    auto const market = *id == beast::kZero ? nullptr : ledger->read(keylet::perpMarket(*id));
    if (!market)
    {
        rpc::injectError(RpcEntryNotFound, result);
        return result;
    }
    result[jss::perp_market] = market->getJson(JsonOptions::Values::None);

    // The live values are the ones the next transaction of this ledger refreshes the market to.
    Sandbox sb(ledger.get(), TapNone);
    STVector256 marketIDs;
    marketIDs.pushBack(*id);
    rpc::refreshPerpMarkets(sb, marketIDs, context.j);
    auto const live = sb.read(keylet::perpMarket(*id));
    if (!live)
        return result;  // LCOV_EXCL_LINE

    // A Stale market has no quorum index.
    if (std::uint8_t const status = (*live)[sfMarketStatus]; status != perp::kMarketStale)
        result[jss::index_price] = to_string(Number{(*live)[sfIndexPrice]});
    result[jss::next_funding_rate] = to_string(Number{(*live)[sfFundingRate]});
    return result;
}

}  // namespace xrpl
