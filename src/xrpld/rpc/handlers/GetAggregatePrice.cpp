//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2023 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/main/Application.h>
#include <ripple/json/json_value.h>
#include <ripple/ledger/ReadView.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/jss.h>
#include <ripple/rpc/Context.h>
#include <ripple/rpc/impl/RPCHelpers.h>

#include <boost/bimap.hpp>
#include <boost/bimap/multiset_of.hpp>

namespace ripple {

using namespace boost::bimaps;
// sorted descending by lastUpdateTime, ascending by AssetPrice
using Prices = bimap<
    multiset_of<std::uint32_t, std::greater<std::uint32_t>>,
    multiset_of<STAmount>>;

/** Calls callback "f" on the ledger-object sle and up to three previous
 * metadata objects. Stops early if the callback returns true.
 */
static void
iteratePriceData(
    RPC::JsonContext& context,
    std::shared_ptr<SLE const> const& sle,
    std::function<bool(STObject const&)>&& f)
{
    using Meta = std::shared_ptr<STObject const>;
    constexpr std::uint8_t maxHistory = 3;
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

        if (!oracle || f(*oracle) || isNew)
            return;

        if (++history > maxHistory)
            return;

        uint256 prevTx = chain->getFieldH256(sfPreviousTxnID);
        std::uint32_t prevSeq = chain->getFieldU32(sfPreviousTxnLgrSeq);

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

            oracle = isNew
                ? &static_cast<const STObject&>(node.peekAtField(sfNewFields))
                : &static_cast<const STObject&>(
                      node.peekAtField(sfFinalFields));
            break;
        }
    }
}

// Return avg, sd, data set size
static std::tuple<STAmount, Number, std::uint16_t>
getStats(
    Prices::right_const_iterator const& begin,
    Prices::right_const_iterator const& end)
{
    STAmount avg{noIssue(), 0, 0};
    Number sd{0};
    std::uint16_t const size = std::distance(begin, end);
    avg = std::accumulate(
        begin, end, avg, [&](STAmount const& acc, auto const& it) {
            return acc + it.first;
        });
    avg = divide(avg, STAmount{noIssue(), size, 0}, noIssue());
    if (size > 1)
    {
        sd = std::accumulate(
            begin, end, sd, [&](Number const& acc, auto const& it) {
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
Json::Value
doGetAggregatePrice(RPC::JsonContext& context)
{
    Json::Value result;
    auto const& params(context.params);

    constexpr std::uint16_t maxOracles = 200;
    if (!params.isMember(jss::oracles))
        return RPC::missing_field_error(jss::oracles);
    if (!params[jss::oracles].isArray() || params[jss::oracles].size() == 0 ||
        params[jss::oracles].size() > maxOracles)
    {
        RPC::inject_error(rpcORACLE_MALFORMED, result);
        return result;
    }

    if (!params.isMember(jss::base_asset))
        return RPC::missing_field_error(jss::base_asset);

    if (!params.isMember(jss::quote_asset))
        return RPC::missing_field_error(jss::quote_asset);

    // Lambda to validate uint type
    // support positive int, uint, and a number represented as a string
    auto validUInt = [](Json::Value const& params,
                        Json::StaticString const& field) {
        auto const& jv = params[field];
        std::uint32_t v;
        return jv.isUInt() || (jv.isInt() && jv.asInt() >= 0) ||
            (jv.isString() && beast::lexicalCastChecked(v, jv.asString()));
    };

    // Lambda to get `trim` and `time_threshold` fields. If the field
    // is not included in the input then a default value is returned.
    auto getField = [&params, &validUInt](
                        Json::StaticString const& field,
                        unsigned int def =
                            0) -> std::variant<std::uint32_t, error_code_i> {
        if (params.isMember(field))
        {
            if (!validUInt(params, field))
                return rpcINVALID_PARAMS;
            return params[field].asUInt();
        }
        return def;
    };

    // Lambda to get `base_asset` and `quote_asset`. The values have
    // to conform to the Currency type.
    auto getCurrency =
        [&params](SField const& sField, Json::StaticString const& field)
        -> std::variant<Json::Value, error_code_i> {
        try
        {
            if (params[field].asString().empty())
                return rpcINVALID_PARAMS;
            currencyFromJson(sField, params[field]);
            return params[field];
        }
        catch (...)
        {
            return rpcINVALID_PARAMS;
        }
    };

    auto const trim = getField(jss::trim);
    if (std::holds_alternative<error_code_i>(trim))
    {
        RPC::inject_error(std::get<error_code_i>(trim), result);
        return result;
    }
    if (params.isMember(jss::trim) &&
        (std::get<std::uint32_t>(trim) == 0 ||
         std::get<std::uint32_t>(trim) > maxTrim))
    {
        RPC::inject_error(rpcINVALID_PARAMS, result);
        return result;
    }

    auto const timeThreshold = getField(jss::time_threshold, 0);
    if (std::holds_alternative<error_code_i>(timeThreshold))
    {
        RPC::inject_error(std::get<error_code_i>(timeThreshold), result);
        return result;
    }

    auto const baseAsset = getCurrency(sfBaseAsset, jss::base_asset);
    if (std::holds_alternative<error_code_i>(baseAsset))
    {
        RPC::inject_error(std::get<error_code_i>(baseAsset), result);
        return result;
    }
    auto const quoteAsset = getCurrency(sfQuoteAsset, jss::quote_asset);
    if (std::holds_alternative<error_code_i>(quoteAsset))
    {
        RPC::inject_error(std::get<error_code_i>(quoteAsset), result);
        return result;
    }

    // Collect the dataset into bimap keyed by lastUpdateTime and
    // STAmount (Number is int64 and price is uint64)
    Prices prices;
    for (auto const& oracle : params[jss::oracles])
    {
        if (!oracle.isMember(jss::oracle_document_id) ||
            !oracle.isMember(jss::account))
        {
            RPC::inject_error(rpcORACLE_MALFORMED, result);
            return result;
        }
        auto const documentID = validUInt(oracle, jss::oracle_document_id)
            ? std::make_optional(oracle[jss::oracle_document_id].asUInt())
            : std::nullopt;
        auto const account =
            parseBase58<AccountID>(oracle[jss::account].asString());
        if (!account || account->isZero() || !documentID)
        {
            RPC::inject_error(rpcINVALID_PARAMS, result);
            return result;
        }

        std::shared_ptr<ReadView const> ledger;
        result = RPC::lookupLedger(ledger, context);
        if (!ledger)
            return result;  // LCOV_EXCL_LINE

        auto const sle = ledger->read(keylet::oracle(*account, *documentID));
        iteratePriceData(context, sle, [&](STObject const& node) {
            auto const& series = node.getFieldArray(sfPriceDataSeries);
            // find the token pair entry with the price
            if (auto iter = std::find_if(
                    series.begin(),
                    series.end(),
                    [&](STObject const& o) -> bool {
                        return o.getFieldCurrency(sfBaseAsset).getText() ==
                            std::get<Json::Value>(baseAsset) &&
                            o.getFieldCurrency(sfQuoteAsset).getText() ==
                            std::get<Json::Value>(quoteAsset) &&
                            o.isFieldPresent(sfAssetPrice);
                    });
                iter != series.end())
            {
                auto const price = iter->getFieldU64(sfAssetPrice);
                auto const scale = iter->isFieldPresent(sfScale)
                    ? -static_cast<int>(iter->getFieldU8(sfScale))
                    : 0;
                prices.insert(Prices::value_type(
                    node.getFieldU32(sfLastUpdateTime),
                    STAmount{noIssue(), price, scale}));
                return true;
            }
            return false;
        });
    }

    if (prices.empty())
    {
        RPC::inject_error(rpcOBJECT_NOT_FOUND, result);
        return result;
    }

    // erase outdated data
    // sorted in descending, therefore begin is the latest, end is the oldest
    auto const latestTime = prices.left.begin()->first;
    if (auto const threshold = std::get<std::uint32_t>(timeThreshold))
    {
        // threshold defines an acceptable range {max,min} of lastUpdateTime as
        // {latestTime, latestTime - threshold}, the prices with lastUpdateTime
        // greater than (latestTime - threshold) are erased.
        auto const oldestTime = prices.left.rbegin()->first;
        auto const upperBound =
            latestTime > threshold ? (latestTime - threshold) : oldestTime;
        if (upperBound > oldestTime)
            prices.left.erase(
                prices.left.upper_bound(upperBound), prices.left.end());

        // At least one element should remain since upperBound is either
        // equal to oldestTime or is less than latestTime, in which case
        // the data is deleted between the oldestTime and upperBound.
        if (prices.empty())
        {
            // LCOV_EXCL_START
            RPC::inject_error(rpcINTERNAL, result);
            return result;
            // LCOV_EXCL_STOP
        }
    }
    result[jss::time] = latestTime;

    // calculate stats
    auto const [avg, sd, size] =
        getStats(prices.right.begin(), prices.right.end());
    result[jss::entire_set][jss::mean] = avg.getText();
    result[jss::entire_set][jss::size] = size;
    result[jss::entire_set][jss::standard_deviation] = to_string(sd);

    auto itAdvance = [&](auto it, int distance) {
        std::advance(it, distance);
        return it;
    };

    auto const median = [&prices, &itAdvance, &size_ = size]() {
        auto const middle = size_ / 2;
        if ((size_ % 2) == 0)
        {
            static STAmount two{noIssue(), 2, 0};
            auto it = itAdvance(prices.right.begin(), middle - 1);
            auto const& a1 = it->first;
            auto const& a2 = (++it)->first;
            return divide(a1 + a2, two, noIssue());
        }
        return itAdvance(prices.right.begin(), middle)->first;
    }();
    result[jss::median] = median.getText();

    if (std::get<std::uint32_t>(trim) != 0)
    {
        auto const trimCount =
            prices.size() * std::get<std::uint32_t>(trim) / 100;

        auto const [avg, sd, size] = getStats(
            itAdvance(prices.right.begin(), trimCount),
            itAdvance(prices.right.end(), -trimCount));
        result[jss::trimmed_set][jss::mean] = avg.getText();
        result[jss::trimmed_set][jss::size] = size;
        result[jss::trimmed_set][jss::standard_deviation] = to_string(sd);
    }

    return result;
}

}  // namespace ripple
