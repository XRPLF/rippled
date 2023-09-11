//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2014 Ripple Labs Inc.

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
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/app/misc/Transaction.h>
#include <ripple/app/rdb/backend/PostgresDatabase.h>
#include <ripple/app/rdb/backend/SQLiteDatabase.h>
#include <ripple/core/Pg.h>
#include <ripple/json/json_reader.h>
#include <ripple/json/json_value.h>
#include <ripple/ledger/ReadView.h>
#include <ripple/net/RPCErr.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/NFTSyntheticSerializer.h>
#include <ripple/protocol/UintTypes.h>
#include <ripple/protocol/jss.h>
#include <ripple/resource/Fees.h>
#include <ripple/rpc/Context.h>
#include <ripple/rpc/DeliveredAmount.h>
#include <ripple/rpc/Role.h>
#include <ripple/rpc/impl/RPCHelpers.h>

#include <grpcpp/grpcpp.h>

namespace ripple {

using TxnsData = RelationalDatabase::AccountTxs;
using TxnsDataBinary = RelationalDatabase::MetaTxsList;
using TxnDataBinary = RelationalDatabase::txnMetaLedgerType;
using AccountTxArgs = RelationalDatabase::AccountTxArgs;
using AccountTxResult = RelationalDatabase::AccountTxResult;

using LedgerShortcut = RelationalDatabase::LedgerShortcut;
using LedgerSpecifier = RelationalDatabase::LedgerSpecifier;

// parses args into a ledger specifier, or returns a Json object on error
std::variant<std::optional<LedgerSpecifier>, Json::Value>
parseLedgerArgs(RPC::Context& context, Json::Value const& params)
{
    Json::Value response;
    // if ledger_index_min or max is specified, then ledger_hash or ledger_index
    // should not be specified. Error out if it is
    if (context.apiVersion > 1)
    {
        if ((params.isMember(jss::ledger_index_min) ||
             params.isMember(jss::ledger_index_max)) &&
            (params.isMember(jss::ledger_hash) ||
             params.isMember(jss::ledger_index)))
        {
            RPC::Status status{rpcINVALID_PARAMS, "invalidParams"};
            status.inject(response);
            return response;
        }
    }
    if (params.isMember(jss::ledger_index_min) ||
        params.isMember(jss::ledger_index_max))
    {
        uint32_t min = params.isMember(jss::ledger_index_min) &&
                params[jss::ledger_index_min].asInt() >= 0
            ? params[jss::ledger_index_min].asUInt()
            : 0;
        uint32_t max = params.isMember(jss::ledger_index_max) &&
                params[jss::ledger_index_max].asInt() >= 0
            ? params[jss::ledger_index_max].asUInt()
            : UINT32_MAX;

        return LedgerRange{min, max};
    }
    else if (params.isMember(jss::ledger_hash))
    {
        auto& hashValue = params[jss::ledger_hash];
        if (!hashValue.isString())
        {
            RPC::Status status{rpcINVALID_PARAMS, "ledgerHashNotString"};
            status.inject(response);
            return response;
        }

        LedgerHash hash;
        if (!hash.parseHex(hashValue.asString()))
        {
            RPC::Status status{rpcINVALID_PARAMS, "ledgerHashMalformed"};
            status.inject(response);
            return response;
        }
        return hash;
    }
    else if (params.isMember(jss::ledger_index))
    {
        LedgerSpecifier ledger;
        if (params[jss::ledger_index].isNumeric())
            ledger = params[jss::ledger_index].asUInt();
        else
        {
            std::string ledgerStr = params[jss::ledger_index].asString();

            if (ledgerStr == "current" || ledgerStr.empty())
                ledger = LedgerShortcut::CURRENT;
            else if (ledgerStr == "closed")
                ledger = LedgerShortcut::CLOSED;
            else if (ledgerStr == "validated")
                ledger = LedgerShortcut::VALIDATED;
            else
            {
                RPC::Status status{
                    rpcINVALID_PARAMS, "ledger_index string malformed"};
                status.inject(response);
                return response;
            }
        }
        return ledger;
    }
    return std::optional<LedgerSpecifier>{};
}

std::variant<LedgerRange, RPC::Status>
getLedgerRange(
    RPC::Context& context,
    std::optional<LedgerSpecifier> const& ledgerSpecifier)
{
    std::uint32_t uValidatedMin;
    std::uint32_t uValidatedMax;
    bool bValidated =
        context.ledgerMaster.getValidatedRange(uValidatedMin, uValidatedMax);

    if (!bValidated)
    {
        // Don't have a validated ledger range.
        if (context.apiVersion == 1)
            return rpcLGR_IDXS_INVALID;
        return rpcNOT_SYNCED;
    }

    std::uint32_t uLedgerMin = uValidatedMin;
    std::uint32_t uLedgerMax = uValidatedMax;
    // Does request specify a ledger or ledger range?
    if (ledgerSpecifier)
    {
        auto const status = std::visit(
            [&](auto const& ls) -> RPC::Status {
                using T = std::decay_t<decltype(ls)>;
                if constexpr (std::is_same_v<T, LedgerRange>)
                {
                    // if ledger_index_min or ledger_index_max is out of
                    // valid ledger range, error out. exclude -1 as
                    // it is a valid input
                    if (context.apiVersion > 1)
                    {
                        if ((ls.max > uValidatedMax && ls.max != -1) ||
                            (ls.min < uValidatedMin && ls.min != 0))
                        {
                            return rpcLGR_IDX_MALFORMED;
                        }
                    }
                    if (ls.min > uValidatedMin)
                    {
                        uLedgerMin = ls.min;
                    }
                    if (ls.max < uValidatedMax)
                    {
                        uLedgerMax = ls.max;
                    }
                    if (uLedgerMax < uLedgerMin)
                    {
                        if (context.apiVersion == 1)
                            return rpcLGR_IDXS_INVALID;
                        return rpcINVALID_LGR_RANGE;
                    }
                }
                else
                {
                    std::shared_ptr<ReadView const> ledgerView;
                    auto const status = getLedger(ledgerView, ls, context);
                    if (!ledgerView)
                    {
                        return status;
                    }

                    bool validated = RPC::isValidated(
                        context.ledgerMaster, *ledgerView, context.app);

                    if (!validated || ledgerView->info().seq > uValidatedMax ||
                        ledgerView->info().seq < uValidatedMin)
                    {
                        return rpcLGR_NOT_VALIDATED;
                    }
                    uLedgerMin = uLedgerMax = ledgerView->info().seq;
                }
                return RPC::Status::OK;
            },
            *ledgerSpecifier);

        if (status)
            return status;
    }
    return LedgerRange{uLedgerMin, uLedgerMax};
}

std::pair<AccountTxResult, RPC::Status>
doAccountTxHelp(RPC::Context& context, AccountTxArgs const& args)
{
    context.loadType = Resource::feeMediumBurdenRPC;
    if (context.app.config().reporting())
    {
        auto const db = dynamic_cast<PostgresDatabase*>(
            &context.app.getRelationalDatabase());

        if (!db)
            Throw<std::runtime_error>("Failed to get relational database");

        return db->getAccountTx(args);
    }

    AccountTxResult result;

    auto lgrRange = getLedgerRange(context, args.ledger);
    if (auto stat = std::get_if<RPC::Status>(&lgrRange))
    {
        // An error occurred getting the requested ledger range
        return {result, *stat};
    }

    result.ledgerRange = std::get<LedgerRange>(lgrRange);

    result.marker = args.marker;

    RelationalDatabase::AccountTxPageOptions options = {
        args.account,
        result.ledgerRange.min,
        result.ledgerRange.max,
        result.marker,
        args.limit,
        isUnlimited(context.role)};

    auto const db =
        dynamic_cast<SQLiteDatabase*>(&context.app.getRelationalDatabase());

    if (!db)
        Throw<std::runtime_error>("Failed to get relational database");

    if (args.binary)
    {
        if (args.forward)
        {
            auto [tx, marker] = db->oldestAccountTxPageB(options);
            result.transactions = tx;
            result.marker = marker;
        }
        else
        {
            auto [tx, marker] = db->newestAccountTxPageB(options);
            result.transactions = tx;
            result.marker = marker;
        }
    }
    else
    {
        if (args.forward)
        {
            auto [tx, marker] = db->oldestAccountTxPage(options);
            result.transactions = tx;
            result.marker = marker;
        }
        else
        {
            auto [tx, marker] = db->newestAccountTxPage(options);
            result.transactions = tx;
            result.marker = marker;
        }
    }

    result.limit = args.limit;
    JLOG(context.j.debug()) << __func__ << " : finished";

    return {result, rpcSUCCESS};
}

Json::Value
populateJsonResponse(
    std::pair<AccountTxResult, RPC::Status> const& res,
    AccountTxArgs const& args,
    RPC::JsonContext const& context)
{
    Json::Value response;
    RPC::Status const& error = res.second;
    if (error.toErrorCode() != rpcSUCCESS)
    {
        error.inject(response);
    }
    else
    {
        AccountTxResult const& result = res.first;
        response[jss::validated] = true;
        response[jss::limit] = result.limit;
        response[jss::account] = context.params[jss::account].asString();
        response[jss::ledger_index_min] = result.ledgerRange.min;
        response[jss::ledger_index_max] = result.ledgerRange.max;

        Json::Value& jvTxns = (response[jss::transactions] = Json::arrayValue);

        if (auto txnsData = std::get_if<TxnsData>(&result.transactions))
        {
            assert(!args.binary);
            for (auto const& [txn, txnMeta] : *txnsData)
            {
                if (txn)
                {
                    Json::Value& jvObj = jvTxns.append(Json::objectValue);

                    jvObj[jss::tx] = txn->getJson(JsonOptions::include_date);
                    if (txnMeta)
                    {
                        jvObj[jss::meta] =
                            txnMeta->getJson(JsonOptions::include_date);
                        jvObj[jss::validated] = true;
                        insertDeliveredAmount(
                            jvObj[jss::meta], context, txn, *txnMeta);
                        insertNFTSyntheticInJson(
                            jvObj, txn->getSTransaction(), *txnMeta);
                    }
                }
            }
        }
        else
        {
            assert(args.binary);

            for (auto const& binaryData :
                 std::get<TxnsDataBinary>(result.transactions))
            {
                Json::Value& jvObj = jvTxns.append(Json::objectValue);

                jvObj[jss::tx_blob] = strHex(std::get<0>(binaryData));
                jvObj[jss::meta] = strHex(std::get<1>(binaryData));
                jvObj[jss::ledger_index] = std::get<2>(binaryData);
                jvObj[jss::validated] = true;
            }
        }

        if (result.marker)
        {
            response[jss::marker] = Json::objectValue;
            response[jss::marker][jss::ledger] = result.marker->ledgerSeq;
            response[jss::marker][jss::seq] = result.marker->txnSeq;
        }
        if (context.app.config().reporting())
            response["used_postgres"] = true;
    }

    JLOG(context.j.debug()) << __func__ << " : finished";
    return response;
}

// {
//   account: account,
//   ledger_index_min: ledger_index  // optional, defaults to earliest
//   ledger_index_max: ledger_index, // optional, defaults to latest
//   binary: boolean,                // optional, defaults to false
//   forward: boolean,               // optional, defaults to false
//   limit: integer,                 // optional
//   marker: object {ledger: ledger_index, seq: txn_sequence} // optional,
//   resume previous query
// }
Json::Value
doAccountTxJson(RPC::JsonContext& context)
{
    if (!context.app.config().useTxTables())
        return rpcError(rpcNOT_ENABLED);

    auto& params = context.params;
    AccountTxArgs args;
    Json::Value response;

    args.limit = params.isMember(jss::limit) ? params[jss::limit].asUInt() : 0;
    args.binary = params.isMember(jss::binary) && params[jss::binary].asBool();
    args.forward =
        params.isMember(jss::forward) && params[jss::forward].asBool();

    if (!params.isMember(jss::account))
        return rpcError(rpcINVALID_PARAMS);

    auto const account =
        parseBase58<AccountID>(params[jss::account].asString());
    if (!account)
        return rpcError(rpcACT_MALFORMED);

    args.account = *account;

    auto parseRes = parseLedgerArgs(context, params);
    if (auto jv = std::get_if<Json::Value>(&parseRes))
    {
        return *jv;
    }
    else
    {
        args.ledger = std::get<std::optional<LedgerSpecifier>>(parseRes);
    }

    if (params.isMember(jss::marker))
    {
        auto& token = params[jss::marker];
        if (!token.isMember(jss::ledger) || !token.isMember(jss::seq) ||
            !token[jss::ledger].isConvertibleTo(Json::ValueType::uintValue) ||
            !token[jss::seq].isConvertibleTo(Json::ValueType::uintValue))
        {
            RPC::Status status{
                rpcINVALID_PARAMS,
                "invalid marker. Provide ledger index via ledger field, and "
                "transaction sequence number via seq field"};
            status.inject(response);
            return response;
        }
        args.marker = {token[jss::ledger].asUInt(), token[jss::seq].asUInt()};
    }

    auto res = doAccountTxHelp(context, args);
    JLOG(context.j.debug()) << __func__ << " populating response";
    return populateJsonResponse(res, args, context);
}

}  // namespace ripple
