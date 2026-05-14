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

/** Dual-indexed price dataset for the aggregate-price computation.
 *
 *  The left side is keyed by `lastUpdateTime` in descending order so that
 *  stale entries can be erased in O(log n) via `left.upper_bound(cutoff)`.
 *  The right side is keyed by `STAmount` in ascending order so that
 *  statistical operations (median, trimmed mean) iterate over prices already
 *  sorted by value.  Both views stay automatically synchronized: erasing an
 *  entry from either side removes the corresponding entry from the other.
 */
using Prices =
    bimap<multiset_of<std::uint32_t, std::greater<std::uint32_t>>, multiset_of<STAmount>>;

/** Walk an oracle's current state and up to three prior transaction metadata
 *  snapshots, invoking a callback on each until a matching price is found.
 *
 *  An oracle may have been updated without touching the requested token pair,
 *  leaving the current `sfPriceDataSeries` with no relevant entry.  This
 *  function follows `sfPreviousTxnID` / `sfPreviousTxnLgrSeq` backward
 *  through the ledger history (up to `kMAX_HISTORY = 3` hops) so the caller
 *  can retrieve a price from the most recent revision that did record the pair.
 *
 *  Two pointers track separate concerns:
 *  - **`oracle`** — the object containing `sfPriceDataSeries`.  Starts as the
 *      live SLE; after the first hop it becomes `sfNewFields` (create) or
 *      `sfFinalFields` (modify) inside the matching metadata node.
 *  - **`chain`** — the object containing the navigation fields
 *      `sfPreviousTxnID` and `sfPreviousTxnLgrSeq`.  Starts equal to `oracle`
 *      (the live SLE), then diverges to the enclosing `ModifiedNode` /
 *      `CreatedNode` wrapper once inside metadata (where those fields live on
 *      the wrapper, not on the nested field object).
 *
 *  The `prevChain == chain` guard detects a missing `ltORACLE` node in the
 *  inner scan and exits the loop cleanly.  An `isNew` flag short-circuits
 *  immediately when the earliest hop was a creation transaction, since there
 *  is no further history behind it.
 *
 *  @param context  RPC execution context; used to fetch historical ledgers.
 *  @param sle      The live `PriceOracle` SLE for the oracle being examined.
 *                  May be null if the oracle does not exist on the ledger.
 *  @param f        Callback receiving each `STObject` that has
 *                  `sfPriceDataSeries`.  Return `true` to stop iteration (price
 *                  found); return `false` to continue to the next history hop.
 */
static void
iteratePriceData(
    RPC::JsonContext& context,
    std::shared_ptr<SLE const> const& sle,
    std::function<bool(STObject const&)> const& f)
{
    using Meta = std::shared_ptr<STObject const>;
    constexpr std::uint8_t kMAX_HISTORY = 3;
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

    Meta meta = nullptr;
    while (true)
    {
        if (prevChain == chain)
            return;

        if ((oracle == nullptr) || f(*oracle) || isNew)
            return;

        if (++history > kMAX_HISTORY)
            return;

        uint256 const prevTx = chain->getFieldH256(sfPreviousTxnID);
        std::uint32_t const prevSeq = chain->getFieldU32(sfPreviousTxnLgrSeq);

        auto const ledger = context.ledgerMaster.getLedgerBySeq(prevSeq);
        if (!ledger)
            return;  // LCOV_EXCL_LINE

        meta = ledger->txRead(prevTx).second;

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

/** Compute mean, sample standard deviation, and element count over a range of
 *  price-sorted bimap entries.
 *
 *  The mean is computed as the arithmetic average of the `STAmount` values in
 *  [begin, end).  The standard deviation uses Bessel's correction (`n - 1`
 *  denominator), appropriate for a sample drawn from a broader population of
 *  potential oracle reports.  The square root is taken via XRPL's `Number`
 *  type to preserve financial-grade precision.  When the range contains only
 *  one element the standard deviation is returned as zero.
 *
 *  @param begin  Iterator to the first element of the value-sorted (right)
 *               view of the `Prices` bimap.
 *  @param end    Past-the-end iterator for the same range.
 *  @return A tuple of `{mean, standard_deviation, count}`.
 */
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

/** Handler for the `get_aggregate_price` RPC method (XLS-47).
 *
 *  Aggregates prices from up to 200 on-chain `PriceOracle` ledger objects
 *  for a given (base_asset, quote_asset) pair and returns a statistical
 *  summary: mean, sample standard deviation, median, and — if `trim` is
 *  supplied — a symmetrically trimmed mean and standard deviation.
 *
 *  Because individual oracles may be stale or dishonest, the method is
 *  designed to be called with a list of independent oracle identifiers so
 *  that the aggregate resists manipulation by any single reporter.  History
 *  walking (`iteratePriceData`) recovers prices from oracles that were last
 *  updated without touching the requested pair.
 *
 *  **Input parameters** (all in `context.params`):
 *  - `oracles` (required) — JSON array of `{account, oracle_document_id}`,
 *      1–200 entries; empty or oversized arrays return `oracleMalformed`.
 *  - `base_asset` (required) — currency code for the asset being priced;
 *      must pass `currencyFromJson` validation.
 *  - `quote_asset` (required) — currency code for the denomination; same
 *      validation as `base_asset`.
 *  - `trim` (optional) — integer 1–25; percentage of lowest and highest
 *      prices to remove symmetrically before computing trimmed statistics.
 *      Zero and values above 25 are rejected as `invalidParams`.
 *  - `time_threshold` (optional) — non-negative integer; prices whose
 *      `lastUpdateTime` is older than `(latestTime - time_threshold)` are
 *      excluded.  Zero means no filtering.
 *
 *  **Result fields** (on success):
 *  - `time` — `lastUpdateTime` of the most recently updated oracle.
 *  - `entire_set.mean`, `entire_set.standard_deviation`, `entire_set.size`.
 *  - `median`.
 *  - `trimmed_set.mean`, `trimmed_set.standard_deviation`, `trimmed_set.size`
 *      (only present when `trim` was supplied and non-zero).
 *
 *  @param context  RPC execution context carrying params, ledger master, and
 *                  application references.
 *  @return A `Json::Value` containing the aggregate statistics, or an error
 *          object populated via `RPC::injectError` / `RPC::missingFieldError`.
 *  @note If no oracle in the list has a price for the requested pair (including
 *      up to three historical hops), the handler returns `objectNotFound`.
 */
json::Value
doGetAggregatePrice(RPC::JsonContext& context)
{
    json::Value result;
    auto const& params(context.params);

    constexpr std::uint16_t kMAX_ORACLES = 200;
    if (!params.isMember(jss::oracles))
        return RPC::missingFieldError(jss::oracles);
    if (!params[jss::oracles].isArray() || params[jss::oracles].size() == 0 ||
        params[jss::oracles].size() > kMAX_ORACLES)
    {
        RPC::injectError(RpcOracleMalformed, result);
        return result;
    }

    if (!params.isMember(jss::base_asset))
        return RPC::missingFieldError(jss::base_asset);

    if (!params.isMember(jss::quote_asset))
        return RPC::missingFieldError(jss::quote_asset);

    // Returns true when the named field is a non-negative integer or a string
    // that lexically converts to one — accepts JSON uint, non-negative int, or
    // a decimal string (e.g. "200") to accommodate loose clients.
    auto validUInt = [](json::Value const& params, json::StaticString const& field) {
        auto const& jv = params[field];
        std::uint32_t v = 0;
        return jv.isUInt() || (jv.isInt() && jv.asInt() >= 0) ||
            (jv.isString() && beast::lexicalCastChecked(v, jv.asString()));
    };

    // Reads an optional uint32 parameter from params; returns the field value
    // on success, `def` when absent, or `RpcInvalidParams` when present but
    // not a valid non-negative integer.  Return type is a variant so callers
    // can distinguish a missing field from a parse failure without exceptions.
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

    // Validates that an asset field is a non-empty, well-formed currency code
    // by delegating to `currencyFromJson`, which throws on malformed input.
    // Returns the raw JSON value on success so it can be compared textually
    // against `sfBaseAsset`/`sfQuoteAsset` values in `sfPriceDataSeries`.
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
        (std::get<std::uint32_t>(trim) == 0 || std::get<std::uint32_t>(trim) > kMAX_TRIM))
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

    // Collect prices into the bimap.  sfAssetPrice is stored as a uint64
    // mantissa; sfScale is negated so STAmount represents price * 10^(-scale).
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

    // Left view is sorted descending, so begin() is the newest entry.
    auto const latestTime = prices.left.begin()->first;
    if (auto const threshold = std::get<std::uint32_t>(timeThreshold))
    {
        // Acceptable range is [latestTime - threshold, latestTime].  Prices
        // older than the lower bound are erased.  When latestTime <= threshold
        // the subtraction would underflow, so upperBound is clamped to
        // oldestTime, which preserves all entries.
        auto const oldestTime = prices.left.rbegin()->first;
        auto const upperBound = latestTime > threshold ? (latestTime - threshold) : oldestTime;
        if (upperBound > oldestTime)
            prices.left.erase(prices.left.upper_bound(upperBound), prices.left.end());

        // upperBound is either equal to oldestTime (nothing erased) or strictly
        // less than latestTime (some entries erased, but the newest survives).
        // An empty set here would be a logic error.
        if (prices.empty())
        {
            // LCOV_EXCL_START
            RPC::injectError(RpcInternal, result);
            return result;
            // LCOV_EXCL_STOP
        }
    }
    result[jss::time] = latestTime;

    auto const [avg, sd, size] = getStats(prices.right.begin(), prices.right.end());
    result[jss::entire_set][jss::mean] = avg.getText();
    result[jss::entire_set][jss::size] = size;
    result[jss::entire_set][jss::standard_deviation] = to_string(sd);

    // Helper to return an advanced copy of a bimap iterator by value,
    // keeping median and trimmed-set arithmetic readable inline.
    auto itAdvance = [&](auto it, int distance) {
        std::advance(it, distance);
        return it;
    };

    auto const median = [&prices, &itAdvance, &size = size]() {
        auto const middle = size / 2;
        if ((size % 2) == 0)
        {
            static STAmount const kTWO{noIssue(), 2, 0};
            auto it = itAdvance(prices.right.begin(), middle - 1);
            auto const& a1 = it->first;
            auto const& a2 = (++it)->first;
            return divide(a1 + a2, kTWO, noIssue());
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
