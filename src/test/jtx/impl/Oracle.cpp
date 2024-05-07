//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#include <ripple/protocol/digest.h>
#include <ripple/protocol/jss.h>
#include <test/jtx/Oracle.h>

#include <boost/lexical_cast/try_lexical_convert.hpp>
#include <boost/regex.hpp>

#include <vector>

namespace ripple {
namespace test {
namespace jtx {
namespace oracle {

Oracle::Oracle(Env& env, CreateArg const& arg, bool submit)
    : env_(env), owner_{}, documentID_{}
{
    // LastUpdateTime is checked to be in range
    // {close-maxLastUpdateTimeDelta, close+maxLastUpdateTimeDelta}.
    // To make the validation work and to make the clock consistent
    // for tests running at different time, simulate Unix time starting
    // on testStartTime since Ripple epoch.
    auto const now = env_.timeKeeper().now();
    if (now.time_since_epoch().count() == 0 || arg.close)
        env_.close(now + testStartTime - epoch_offset);
    if (arg.owner)
        owner_ = *arg.owner;
    if (arg.documentID && validDocumentID(*arg.documentID))
        documentID_ = asUInt(*arg.documentID);
    if (submit)
        set(arg);
}

void
Oracle::remove(RemoveArg const& arg)
{
    Json::Value jv;
    jv[jss::TransactionType] = jss::OracleDelete;
    jv[jss::Account] = to_string(arg.owner.value_or(owner_));
    toJson(jv[jss::OracleDocumentID], arg.documentID.value_or(documentID_));
    if (Oracle::fee != 0)
        jv[jss::Fee] = std::to_string(Oracle::fee);
    else if (arg.fee != 0)
        jv[jss::Fee] = std::to_string(arg.fee);
    else
        jv[jss::Fee] = std::to_string(env_.current()->fees().increment.drops());
    if (arg.flags != 0)
        jv[jss::Flags] = arg.flags;
    submit(jv, arg.msig, arg.seq, arg.err);
}

void
Oracle::submit(
    Json::Value const& jv,
    std::optional<jtx::msig> const& msig,
    std::optional<jtx::seq> const& seq,
    std::optional<ter> const& err)
{
    if (msig)
    {
        if (seq && err)
            env_(jv, *msig, *seq, *err);
        else if (seq)
            env_(jv, *msig, *seq);
        else if (err)
            env_(jv, *msig, *err);
        else
            env_(jv, *msig);
    }
    else if (seq && err)
        env_(jv, *seq, *err);
    else if (seq)
        env_(jv, *seq);
    else if (err)
        env_(jv, *err);
    else
        env_(jv);
    env_.close();
}

bool
Oracle::exists(Env& env, AccountID const& account, std::uint32_t documentID)
{
    assert(account.isNonZero());
    return env.le(keylet::oracle(account, documentID)) != nullptr;
}

bool
Oracle::expectPrice(DataSeries const& series) const
{
    if (auto const sle = env_.le(keylet::oracle(owner_, documentID_)))
    {
        auto const& leSeries = sle->getFieldArray(sfPriceDataSeries);
        if (leSeries.size() == 0 || leSeries.size() != series.size())
            return false;
        for (auto const& data : series)
        {
            if (std::find_if(
                    leSeries.begin(),
                    leSeries.end(),
                    [&](STObject const& o) -> bool {
                        auto const& baseAsset = o.getFieldCurrency(sfBaseAsset);
                        auto const& quoteAsset =
                            o.getFieldCurrency(sfQuoteAsset);
                        auto const& price = o.getFieldU64(sfAssetPrice);
                        auto const& scale = o.getFieldU8(sfScale);
                        return baseAsset.getText() == std::get<0>(data) &&
                            quoteAsset.getText() == std::get<1>(data) &&
                            price == std::get<2>(data) &&
                            scale == std::get<3>(data);
                    }) == leSeries.end())
                return false;
        }
        return true;
    }
    return false;
}

bool
Oracle::expectLastUpdateTime(std::uint32_t lastUpdateTime) const
{
    auto const sle = env_.le(keylet::oracle(owner_, documentID_));
    return sle && (*sle)[sfLastUpdateTime] == lastUpdateTime;
}

Json::Value
Oracle::aggregatePrice(
    Env& env,
    std::optional<AnyValue> const& baseAsset,
    std::optional<AnyValue> const& quoteAsset,
    std::optional<OraclesData> const& oracles,
    std::optional<AnyValue> const& trim,
    std::optional<AnyValue> const& timeThreshold)
{
    Json::Value jv;
    Json::Value jvOracles(Json::arrayValue);
    if (oracles)
    {
        for (auto const& id : *oracles)
        {
            Json::Value oracle;
            if (id.first)
                oracle[jss::account] = to_string((*id.first).id());
            if (id.second)
                toJson(oracle[jss::oracle_document_id], *id.second);
            jvOracles.append(oracle);
        }
        jv[jss::oracles] = jvOracles;
    }
    if (trim)
        toJson(jv[jss::trim], *trim);
    if (baseAsset)
        toJson(jv[jss::base_asset], *baseAsset);
    if (quoteAsset)
        toJson(jv[jss::quote_asset], *quoteAsset);
    if (timeThreshold)
        toJson(jv[jss::time_threshold], *timeThreshold);
    // Convert "%None%" to None
    auto str = to_string(jv);
    str = boost::regex_replace(str, boost::regex(NonePattern), UnquotedNone);
    auto jr = env.rpc("json", "get_aggregate_price", str);

    if (jr.isObject())
    {
        if (jr.isMember(jss::result) && jr[jss::result].isMember(jss::status))
            return jr[jss::result];
        else if (jr.isMember(jss::error))
            return jr;
    }
    return Json::nullValue;
}

void
Oracle::set(UpdateArg const& arg)
{
    using namespace std::chrono;
    Json::Value jv;
    if (arg.owner)
        owner_ = *arg.owner;
    if (arg.documentID &&
        std::holds_alternative<std::uint32_t>(*arg.documentID))
    {
        documentID_ = std::get<std::uint32_t>(*arg.documentID);
        jv[jss::OracleDocumentID] = documentID_;
    }
    else if (arg.documentID)
        toJson(jv[jss::OracleDocumentID], *arg.documentID);
    else
        jv[jss::OracleDocumentID] = documentID_;
    jv[jss::TransactionType] = jss::OracleSet;
    jv[jss::Account] = to_string(owner_);
    if (arg.assetClass)
        toJsonHex(jv[jss::AssetClass], *arg.assetClass);
    if (arg.provider)
        toJsonHex(jv[jss::Provider], *arg.provider);
    if (arg.uri)
        toJsonHex(jv[jss::URI], *arg.uri);
    if (arg.flags != 0)
        jv[jss::Flags] = arg.flags;
    if (Oracle::fee != 0)
        jv[jss::Fee] = std::to_string(Oracle::fee);
    else if (arg.fee != 0)
        jv[jss::Fee] = std::to_string(arg.fee);
    else
        jv[jss::Fee] = std::to_string(env_.current()->fees().increment.drops());
    // lastUpdateTime if provided is offset from testStartTime
    if (arg.lastUpdateTime)
    {
        if (std::holds_alternative<std::uint32_t>(*arg.lastUpdateTime))
            jv[jss::LastUpdateTime] = to_string(
                testStartTime.count() +
                std::get<std::uint32_t>(*arg.lastUpdateTime));
        else
            toJson(jv[jss::LastUpdateTime], *arg.lastUpdateTime);
    }
    else
        jv[jss::LastUpdateTime] = to_string(
            duration_cast<seconds>(
                env_.current()->info().closeTime.time_since_epoch())
                .count() +
            epoch_offset.count());
    Json::Value dataSeries(Json::arrayValue);
    auto assetToStr = [](std::string const& s) {
        // assume standard currency
        if (s.size() == 3)
            return s;
        assert(s.size() <= 20);
        // anything else must be 160-bit hex string
        std::string h = strHex(s);
        return strHex(s).append(40 - s.size() * 2, '0');
    };
    for (auto const& data : arg.series)
    {
        Json::Value priceData;
        Json::Value price;
        price[jss::BaseAsset] = assetToStr(std::get<0>(data));
        price[jss::QuoteAsset] = assetToStr(std::get<1>(data));
        if (std::get<2>(data))
            price[jss::AssetPrice] = *std::get<2>(data);
        if (std::get<3>(data))
            price[jss::Scale] = *std::get<3>(data);
        priceData[jss::PriceData] = price;
        dataSeries.append(priceData);
    }
    jv[jss::PriceDataSeries] = dataSeries;
    submit(jv, arg.msig, arg.seq, arg.err);
}

void
Oracle::set(CreateArg const& arg)
{
    set(UpdateArg{
        .owner = arg.owner,
        .documentID = arg.documentID,
        .series = arg.series,
        .assetClass = arg.assetClass,
        .provider = arg.provider,
        .uri = arg.uri,
        .lastUpdateTime = arg.lastUpdateTime,
        .flags = arg.flags,
        .msig = arg.msig,
        .seq = arg.seq,
        .fee = arg.fee,
        .err = arg.err});
}

Json::Value
Oracle::ledgerEntry(
    Env& env,
    std::optional<std::variant<AccountID, std::string>> const& account,
    std::optional<AnyValue> const& documentID,
    std::optional<std::string> const& index)
{
    Json::Value jvParams;
    if (account)
    {
        if (std::holds_alternative<AccountID>(*account))
            jvParams[jss::oracle][jss::account] =
                to_string(std::get<AccountID>(*account));
        else
            jvParams[jss::oracle][jss::account] =
                std::get<std::string>(*account);
    }
    if (documentID)
        toJson(jvParams[jss::oracle][jss::oracle_document_id], *documentID);
    if (index)
    {
        std::uint32_t i;
        if (boost::conversion::try_lexical_convert(*index, i))
            jvParams[jss::oracle][jss::ledger_index] = i;
        else
            jvParams[jss::oracle][jss::ledger_index] = *index;
    }
    // Convert "%None%" to None
    auto str = to_string(jvParams);
    str = boost::regex_replace(str, boost::regex(NonePattern), UnquotedNone);
    auto jr = env.rpc("json", "ledger_entry", str);

    if (jr.isObject())
    {
        if (jr.isMember(jss::result) && jr[jss::result].isMember(jss::status))
            return jr[jss::result];
        else if (jr.isMember(jss::error))
            return jr;
    }
    return Json::nullValue;
}

void
toJson(Json::Value& jv, AnyValue const& v)
{
    std::visit([&](auto&& arg) { jv = arg; }, v);
}

void
toJsonHex(Json::Value& jv, AnyValue const& v)
{
    std::visit(
        [&]<typename T>(T&& arg) {
            if constexpr (std::is_same_v<T, std::string const&>)
            {
                if (arg.starts_with("##"))
                    jv = arg.substr(2);
                else
                    jv = strHex(arg);
            }
            else
                jv = arg;
        },
        v);
}

std::uint32_t
asUInt(AnyValue const& v)
{
    Json::Value jv;
    toJson(jv, v);
    return jv.asUInt();
}

bool
validDocumentID(AnyValue const& v)
{
    try
    {
        Json::Value jv;
        toJson(jv, v);
        jv.asUInt();
        jv.isNumeric();
        return true;
    }
    catch (...)
    {
    }
    return false;
}

}  // namespace oracle
}  // namespace jtx
}  // namespace test
}  // namespace ripple