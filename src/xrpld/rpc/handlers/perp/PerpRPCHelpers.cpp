#include <xrpld/rpc/handlers/perp/PerpRPCHelpers.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/helpers/PerpHelpers.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STVector256.h>

#include <optional>

namespace xrpl::rpc {

std::optional<uint256>
parsePerpID(json::Value const& params, json::StaticString const& field, json::Value& result)
{
    uint256 id;
    auto const& value = params[field];
    if (!value.isString() || !id.parseHex(value.asString()))
    {
        injectError(RpcInvalidParams, expectedFieldMessage(field, "hex string"), result);
        return std::nullopt;
    }
    return id;
}

void
refreshPerpMarkets(ApplyView& view, STVector256 const& marketIDs, beast::Journal j)
{
    for (auto const& marketID : marketIDs)
    {
        auto const market = view.peek(keylet::perpMarket(marketID));
        if (!market)
            continue;
        auto const exchange = view.peek(keylet::perpExchange((*market)[sfExchangeID]));
        if (!exchange)
            continue;
        perp::refreshMarket(view, market, exchange, j);
    }
}

}  // namespace xrpl::rpc
