#include <xrpld/rpc/handlers/orderbook/GetAggregatePrice.h>

#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/RPCLedgerHelpers.h>
#include <xrpld/rpc/detail/SpecBridge.hpp>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/safe_cast.h>
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
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STCurrency.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/jss.h>

#include <boost/bimap.hpp>
#include <boost/bimap/bimap.hpp>
#include <boost/bimap/multiset_of.hpp>

#include <rpcspec/DeferredTypes.hpp>
#include <rpcspec/Errors.hpp>
#include <rpcspec/handlers/get_aggregate_price/Types.hpp>

#include <algorithm>
#include <cstdint>
#include <expected>
#include <functional>
#include <iterator>
#include <memory>
#include <numeric>
#include <optional>
#include <set>
#include <tuple>
#include <utility>

namespace xrpl::rpc {

static_assert(::rpc::spec::handlers::get_aggregate_price::kTrimMax == kMaxTrim);

namespace {

using namespace boost::bimaps;
// sorted descending by lastUpdateTime, ascending by AssetPrice
using Prices = bimap<multiset_of<std::uint32_t, std::greater<>>, multiset_of<STAmount>>;

/**
 * Calls callback "f" on the ledger-object sle and up to three previous
 * metadata objects. Stops early if the callback returns true.
 */
void
iteratePriceData(
    JsonContext const& context,
    SLE::ConstRef sle,
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

        UInt256 const prevTx = chain->getFieldH256(sfPreviousTxnID);
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
std::tuple<STAmount, Number, std::uint16_t>
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
}

}  // namespace

GetAggregatePriceHandler::GetAggregatePriceHandler(JsonContext& context) : context_(context)
{
}

/**
 * oracles: array of {account, oracle_document_id}
 * base_asset: is the asset to be priced
 * quote_asset: is the denomination in which the prices are expressed
 * trim : percentage of outliers to trim [optional]
 * time_threshold : defines a range of prices to include based on the timestamp
 *   range - {most recent, most recent - time_threshold} [optional]
 */
std::expected<GetAggregatePriceHandler::Output, ::rpc::Status>
GetAggregatePriceHandler::process(Input const& input) const
{
    auto const& context = context_.get();

    Output output;
    if (auto const status = getLedger(output.ledger, input.ledger, context);
        status != ::rpc::Status::kOK)
    {
        return std::unexpected{status};
    }
    auto const& ledger = *output.ledger;

    auto const fail = [&output](::rpc::Status status) {
        output.error = std::move(status);
        return output;
    };

    if (!input.oracles)
    {
        return fail(
            input.oracles.error() == ::rpc::spec::DeferredError::Malformed ? RpcOracleMalformed
                                                                           : RpcInvalidParams);
    }

    auto const& params = context.params;
    if (to_string(input.baseAsset) != params[jss::base_asset].asString() ||
        to_string(input.quoteAsset) != params[jss::quote_asset].asString())
    {
        return fail(RpcObjectNotFound);
    }

    Prices prices;
    // Track seen {account, documentID} pairs to skip duplicates
    std::set<std::pair<AccountID, std::uint32_t>> seen;
    for (auto const& oracle : *input.oracles)
    {
        // Skip duplicate oracle entries
        if (!seen.emplace(oracle.account, oracle.documentId).second)
            continue;

        auto const sle = ledger.read(keylet::oracle(oracle.account, oracle.documentId));
        iteratePriceData(context, sle, [&](STObject const& node) {
            auto const& series = node.getFieldArray(sfPriceDataSeries);
            // find the token pair entry with the price
            if (auto const iter = std::ranges::find_if(
                    series,
                    [&](STObject const& o) -> bool {
                        return o.getFieldCurrency(sfBaseAsset) == input.baseAsset &&
                            o.getFieldCurrency(sfQuoteAsset) == input.quoteAsset &&
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
        return fail(RpcObjectNotFound);

    // erase outdated data
    // sorted in descending, therefore begin is the latest, end is the oldest
    auto const latestTime = prices.left.begin()->first;
    if (auto const threshold = input.timeThreshold.value_or(0))
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
            return fail(RpcInternal);  // LCOV_EXCL_LINE
    }
    json::Value stats;
    stats[jss::time] = latestTime;

    // calculate stats
    auto const [avg, sd, size] = getStats(prices.right.begin(), prices.right.end());
    stats[jss::entire_set][jss::mean] = avg.getText();
    stats[jss::entire_set][jss::size] = size;
    stats[jss::entire_set][jss::standard_deviation] = to_string(sd);

    auto itAdvance = [&](auto it, int distance) {
        std::advance(it, distance);
        return it;
    };

    auto const median = [&prices, &itAdvance, &size = size] {
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
    stats[jss::median] = median.getText();

    if (input.trim.has_value())
    {
        auto const trimCount = prices.size() * *input.trim / 100;

        auto const [avg, sd, size] = getStats(
            itAdvance(prices.right.begin(), trimCount), itAdvance(prices.right.end(), -trimCount));
        stats[jss::trimmed_set][jss::mean] = avg.getText();
        stats[jss::trimmed_set][jss::size] = size;
        stats[jss::trimmed_set][jss::standard_deviation] = to_string(sd);
    }

    output.stats = std::move(stats);
    return output;
}

void
GetAggregatePriceHandler::writeResult(json::Value& value, Output const& output) const
{
    if (output.error.has_value())
    {
        injectLedgerFields(*output.ledger, context_.get(), value);
        injectSpecError(value, *output.error);
        return;
    }

    value = output.stats;
    injectLedgerFields(*output.ledger, context_.get(), value);
}

}  // namespace xrpl::rpc
