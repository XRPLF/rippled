#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/RPCLedgerHelpers.h>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/safe_cast.h>
#include <xrpl/beast/core/LexicalCast.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STCurrency.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/jss.h>

#include <boost/bimap.hpp>
#include <boost/bimap/bimap.hpp>
#include <boost/bimap/multiset_of.hpp>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <iterator>
#include <memory>
#include <numeric>
#include <optional>
#include <tuple>
#include <variant>

namespace xrpl {

using namespace boost::bimaps;
// sorted descending by lastUpdateTime, ascending by AssetPrice
using Prices =
    bimap<multiset_of<std::uint32_t, std::greater<std::uint32_t>>, multiset_of<STAmount>>;

/** Calls callback "f" on the ledger-object sle and up to three previous
 * metadata objects. Stops early if the callback returns true.
 */
static void
iteratePriceData(
    RPC::JsonContext& context,
    std::shared_ptr<SLE const> const& sle,
    std::function<bool(STObject const&)> const& f)
{
    static constexpr std::uint8_t kMaxHistory = 3;
    bool isNew = false;
    std::uint8_t history = 0;

    // `oracle` points to an object that has an `sfPriceDataSeries` field.
    // When this function is called, that is a `PriceOracle` ledger object,
    // but after one iteration of the loop below, it is an `sfNewFields`
    // / `sfFinalFields` object in a `CreatedNode` / `ModifiedNode` object in
    // a transaction's metadata.

    // `chain` points to an object that has `sfPreviousTxnID` and
    // `sfPreviousTxnLgrSeq` fields. When this function is called,
    // that is the `PriceOracle` ledger object pointed to by `oracle`,
    // but after one iteration of the loop below, then it is a `ModifiedNode`
    // / `CreatedNode` object in a transaction's metadata.
    STObject const* oracle = sle.get();
    STObject const* chain = oracle;
    // Use to test an unlikely scenario when CreatedNode / ModifiedNode
    // for the Oracle is not found in the inner loop
    STObject const* prevChain = nullptr;

    std::shared_ptr<STObject const> meta = nullptr;
    while (true)
    {
        if (prevChain == chain)
            return;

        if ((oracle == nullptr) || f(*oracle) || isNew)
            return;

        if (++history > kMaxHistory)
            return;

        uint256 const prevTx = chain->getFieldH256(sfPreviousTxnID);
        std::uint32_t const prevSeq = chain->getFieldU32(sfPreviousTxnLgrSeq);

        auto const ledger = context.ledgerMaster.getLedgerBySeq(prevSeq);
        if (!ledger)
            return;  // LCOV_EXCL_LINE

        meta = ledger->txRead(prevTx).second;
        if (!meta)
            return;

        prevChain = chain;
        for (STObject const& node : meta->getFieldArray(sfAffectedNodes))
        {
            if (node.getFieldU16(sfLedgerEntryType) != ltORACLE)
            {
                continue;
            }

            chain = &node;
            isNew = node.isFieldPresent(sfNewFields);
            // if a meta is for the new and this is the first
            // look-up then it's the meta for the tx that
            // created the current object; i.e. there is no
            // historical data
            if (isNew && history == 1)
                return;

            oracle = isNew ? &safeDowncast<STObject const&>(node.peekAtField(sfNewFields))
                           : &safeDowncast<STObject const&>(node.peekAtField(sfFinalFields));
            break;
        }
    }
}

// Return avg, sd, data set size
static std::tuple<STAmount, Number, std::uint16_t>
getStats(Prices::right_const_iterator const& begin, Prices::right_const_iterator const& end)
{
    STAmount avg{noIssue(), 0, 0};
    Number sd{0};
    std::uint16_t const size = std::distance(begin, end);
    avg = std::accumulate(
        begin, end, avg, [&](STAmount const& acc, auto const& it) { return acc + it.first; });
    avg = divide(avg, STAmount{noIssue(), size, 0}, noIssue());
    if (size > 1)
    {
        sd = std::accumulate(begin, end, sd, [&](Number const& acc, auto const& it) {
            return acc + (it.first - avg) * (it.first - avg);
        });
        sd = root2(sd / (size - 1));
    }
    return {avg, sd, size};
};

/**
 * oracles: array of {account, oracle_document_id}
 * base_asset: is the asset to be priced
 * quote_asset: is the denomination in which the prices are expressed
 * trim : percentage of outliers to trim [optional]
 * time_threshold : defines a range of prices to include based on the timestamp
 *   range - {most recent, most recent - time_threshold} [optional]
 */
json::Value
doGetAggregatePrice(RPC::JsonContext& context)
{
    json::Value result;
    auto const& params(context.params);

    static constexpr std::uint16_t kMaxOracles = 200;
    if (!params.isMember(jss::oracles))
        return RPC::missingFieldError(jss::oracles);
    if (!params[jss::oracles].isArray() || params[jss::oracles].size() == 0 ||
        params[jss::oracles].size() > kMaxOracles)
    {
        RPC::injectError(RpcOracleMalformed, result);
        return result;
    }

    if (!params.isMember(jss::base_asset))
        return RPC::missingFieldError(jss::base_asset);

    if (!params.isMember(jss::quote_asset))
        return RPC::missingFieldError(jss::quote_asset);

    // Lambda to validate uint type
    // support positive int, uint, and a number represented as a string
    auto validUInt = [](json::Value const& params, json::StaticString const& field) {
        auto const& jv = params[field];
        std::uint32_t v = 0;
        return jv.isUInt() || (jv.isInt() && jv.asInt() >= 0) ||
            (jv.isString() && beast::lexicalCastChecked(v, jv.asString()));
    };

    // Lambda to get `trim` and `time_threshold` fields. If the field
    // is not included in the input then a default value is returned.
    auto getField = [&params, &validUInt](
                        json::StaticString const& field,
                        unsigned int def = 0) -> std::variant<std::uint32_t, ErrorCodeI> {
        if (params.isMember(field))
        {
            if (!validUInt(params, field))
                return RpcInvalidParams;
            return params[field].asUInt();
        }
        return def;
    };

    // Lambda to get `base_asset` and `quote_asset`. The values have
    // to conform to the Currency type.
    auto getCurrency = [&params](SField const& sField, json::StaticString const& field)
        -> std::variant<json::Value, ErrorCodeI> {
        try
        {
            if (params[field].asString().empty())
                return RpcInvalidParams;
            currencyFromJson(sField, params[field]);
            return params[field];
        }
        catch (...)
        {
            return RpcInvalidParams;
        }
    };

    auto const trim = getField(jss::trim);
    if (std::holds_alternative<ErrorCodeI>(trim))
    {
        RPC::injectError(std::get<ErrorCodeI>(trim), result);
        return result;
    }
    if (params.isMember(jss::trim) &&
        (std::get<std::uint32_t>(trim) == 0 || std::get<std::uint32_t>(trim) > kMaxTrim))
    {
        RPC::injectError(RpcInvalidParams, result);
        return result;
    }

    auto const timeThreshold = getField(jss::time_threshold, 0);
    if (std::holds_alternative<ErrorCodeI>(timeThreshold))
    {
        RPC::injectError(std::get<ErrorCodeI>(timeThreshold), result);
        return result;
    }

    auto const baseAsset = getCurrency(sfBaseAsset, jss::base_asset);
    if (std::holds_alternative<ErrorCodeI>(baseAsset))
    {
        RPC::injectError(std::get<ErrorCodeI>(baseAsset), result);
        return result;
    }
    auto const quoteAsset = getCurrency(sfQuoteAsset, jss::quote_asset);
    if (std::holds_alternative<ErrorCodeI>(quoteAsset))
    {
        RPC::injectError(std::get<ErrorCodeI>(quoteAsset), result);
        return result;
    }

    std::shared_ptr<ReadView const> ledger;
    result = RPC::lookupLedger(ledger, context);
    if (!ledger)
        return result;  // LCOV_EXCL_LINE

    // Collect the dataset into bimap keyed by lastUpdateTime and
    // STAmount (Number is int64 and price is uint64)
    Prices prices;
    for (auto const& oracle : params[jss::oracles])
    {
        if (!oracle.isMember(jss::oracle_document_id) || !oracle.isMember(jss::account))
        {
            RPC::injectError(RpcOracleMalformed, result);
            return result;
        }
        auto const documentID = validUInt(oracle, jss::oracle_document_id)
            ? std::make_optional(oracle[jss::oracle_document_id].asUInt())
            : std::nullopt;
        auto const account = parseBase58<AccountID>(oracle[jss::account].asString());
        if (!account || account->isZero() || !documentID)
        {
            RPC::injectError(RpcInvalidParams, result);
            return result;
        }

        auto const sle = ledger->read(keylet::oracle(*account, *documentID));
        iteratePriceData(context, sle, [&](STObject const& node) {
            auto const& series = node.getFieldArray(sfPriceDataSeries);
            // find the token pair entry with the price
            if (auto iter = std::ranges::find_if(
                    series,
                    [&](STObject const& o) -> bool {
                        return o.getFieldCurrency(sfBaseAsset).getText() ==
                            std::get<json::Value>(baseAsset) &&
                            o.getFieldCurrency(sfQuoteAsset).getText() ==
                            std::get<json::Value>(quoteAsset) &&
                            o.isFieldPresent(sfAssetPrice);
                    });
                iter != series.end())
            {
                auto const price = iter->getFieldU64(sfAssetPrice);
                auto const scale = iter->isFieldPresent(sfScale)
                    ? -static_cast<int>(iter->getFieldU8(sfScale))
                    : 0;
                prices.insert(
                    Prices::value_type(
                        node.getFieldU32(sfLastUpdateTime), STAmount{noIssue(), price, scale}));
                return true;
            }
            return false;
        });
    }

    if (prices.empty())
    {
        RPC::injectError(RpcObjectNotFound, result);
        return result;
    }

    // erase outdated data
    // sorted in descending, therefore begin is the latest, end is the oldest
    auto const latestTime = prices.left.begin()->first;
    if (auto const threshold = std::get<std::uint32_t>(timeThreshold))
    {
        // threshold defines an acceptable range {max,min} of lastUpdateTime as
        // {latestTime, latestTime - threshold}. Prices with lastUpdateTime
        // less than (latestTime - threshold) are erased (outdated prices).
        auto const oldestTime = prices.left.rbegin()->first;
        auto const upperBound = latestTime > threshold ? (latestTime - threshold) : oldestTime;
        if (upperBound > oldestTime)
            prices.left.erase(prices.left.upper_bound(upperBound), prices.left.end());

        // At least one element should remain since upperBound is either
        // equal to oldestTime or is less than latestTime, in which case
        // the data is deleted between the oldestTime and upperBound.
        if (prices.empty())
        {
            // LCOV_EXCL_START
            RPC::injectError(RpcInternal, result);
            return result;
            // LCOV_EXCL_STOP
        }
    }
    result[jss::time] = latestTime;

    // calculate stats
    auto const [avg, sd, size] = getStats(prices.right.begin(), prices.right.end());
    result[jss::entire_set][jss::mean] = avg.getText();
    result[jss::entire_set][jss::size] = size;
    result[jss::entire_set][jss::standard_deviation] = to_string(sd);

    auto itAdvance = [&](auto it, int distance) {
        std::advance(it, distance);
        return it;
    };

    auto const median = [&prices, &itAdvance, &size = size]() {
        auto const middle = size / 2;
        if ((size % 2) == 0)
        {
            static STAmount const kTwo{noIssue(), 2, 0};
            auto it = itAdvance(prices.right.begin(), middle - 1);
            auto const& a1 = it->first;
            auto const& a2 = (++it)->first;
            return divide(a1 + a2, kTwo, noIssue());
        }
        return itAdvance(prices.right.begin(), middle)->first;
    }();
    result[jss::median] = median.getText();

    if (std::get<std::uint32_t>(trim) != 0)
    {
        auto const trimCount = prices.size() * std::get<std::uint32_t>(trim) / 100;

        auto const [avg, sd, size] = getStats(
            itAdvance(prices.right.begin(), trimCount), itAdvance(prices.right.end(), -trimCount));
        result[jss::trimmed_set][jss::mean] = avg.getText();
        result[jss::trimmed_set][jss::size] = size;
        result[jss::trimmed_set][jss::standard_deviation] = to_string(sd);
    }

    return result;
}

}  // namespace xrpl
