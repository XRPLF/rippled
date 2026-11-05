#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/RPCHelpers.h>
#include <xrpld/rpc/detail/RPCLedgerHelpers.h>
#include <xrpld/rpc/detail/Tuning.h>
#include <xrpld/rpc/handlers/perp/PerpRPCHelpers.h>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>  // IWYU pragma: keep
#include <xrpl/protocol/jss.h>

#include <memory>
#include <optional>
#include <string>

namespace xrpl {

json::Value
doPerpBookOrders(rpc::JsonContext const& context)
{
    std::shared_ptr<ReadView const> ledger;
    auto result = rpc::lookupLedger(ledger, context);
    if (!ledger)
        return result;

    auto const& params = context.params;
    if (!params.isMember(jss::market_id))
        return rpc::missingFieldError(jss::market_id);
    auto const marketID = rpc::parsePerpID(params, jss::market_id, result);
    if (!marketID)
        return result;

    auto const& sideField = params[jss::side];
    std::string const side = sideField.isString() ? sideField.asString() : std::string{};
    if (side != "bid" && side != "ask")
        return rpc::expectedFieldError(jss::side, "'bid' or 'ask'");
    bool const sell = side == "ask";

    bool aggregate = false;
    if (params.isMember(jss::aggregate))
    {
        if (!params[jss::aggregate].isBool())
            return rpc::expectedFieldError(jss::aggregate, "boolean");
        aggregate = params[jss::aggregate].asBool();
    }

    unsigned int limit = 0;
    if (auto error = rpc::readLimitField(limit, rpc::tuning::kBookOffers, context))
        return *error;

    // A zero key names no entry, and the ledger refuses to be asked for one.
    if (*marketID == beast::kZero || !ledger->exists(keylet::perpMarket(*marketID)))
    {
        rpc::injectError(RpcEntryNotFound, result);
        return result;
    }

    // Price levels are visited after cursor; a marker names the first order not yet returned.
    uint256 cursor = keylet::perpBook(*marketID, sell).key;
    uint256 const end = getQualityNext(cursor);
    std::optional<uint256> skipTo;
    if (params.isMember(jss::marker))
    {
        auto const marker = rpc::parsePerpID(params, jss::marker, result);
        if (!marker)
            return result;
        auto const order =
            *marker == beast::kZero ? nullptr : ledger->read(keylet::perpOrder(*marker));
        if (!order || (*order)[sfMarketID] != *marketID || order->isFlag(lsfPerpSell) != sell)
            return rpc::invalidFieldError(jss::marker);
        cursor = (*order)[sfBookDirectory];
        --cursor;
        skipTo = *marker;
    }

    json::Value orders{json::ValueType::Array};
    unsigned int count = 0;
    std::optional<uint256> next;
    while (!next)
    {
        auto const level = ledger->succ(cursor, end);
        if (!level)
            break;
        cursor = *level;

        Number price;
        Number size;
        SLE::const_pointer page;
        unsigned int index = 0;
        uint256 entry;
        for (bool more = cdirFirst(*ledger, *level, page, index, entry); more;
             more = cdirNext(*ledger, *level, page, index, entry))
        {
            if (skipTo)
            {
                if (entry != *skipTo)
                    continue;
                skipTo.reset();
            }
            auto const order = ledger->read(keylet::perpOrder(entry));
            if (!order)
                continue;  // LCOV_EXCL_LINE

            // Aggregated, the limit counts price levels, so a level is never split.
            bool const levelStart = size == Number{};
            if (count == limit && (!aggregate || levelStart))
            {
                next = entry;
                break;
            }
            if (!aggregate)
            {
                orders.append(order->getJson(JsonOptions::Values::None));
                ++count;
                continue;
            }
            if (levelStart)
                price = Number{(*order)[sfLimitPrice]};
            size = size + Number{(*order)[sfSize]};
        }

        if (aggregate && size != Number{})
        {
            json::Value levelJson;
            levelJson[jss::price] = to_string(price);
            levelJson[jss::size] = to_string(size);
            orders.append(levelJson);
            ++count;
        }
    }

    result[jss::market_id] = to_string(*marketID);
    result[jss::side] = side;
    if (aggregate)
        result[jss::aggregate] = true;
    result[jss::orders] = orders;
    if (next)
        result[jss::marker] = to_string(*next);
    return result;
}

}  // namespace xrpl
