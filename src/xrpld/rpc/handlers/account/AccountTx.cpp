#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/app/misc/DeliverMax.h>
#include <xrpld/app/misc/Transaction.h>
#include <xrpld/app/rdb/backend/SQLiteDatabase.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/DeliveredAmount.h>
#include <xrpld/rpc/MPTokenIssuanceID.h>
#include <xrpld/rpc/Role.h>
#include <xrpld/rpc/Status.h>
#include <xrpld/rpc/detail/RPCHelpers.h>
#include <xrpld/rpc/detail/RPCLedgerHelpers.h>
#include <xrpld/rpc/detail/Tuning.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/LedgerShortcut.h>
#include <xrpl/protocol/NFTSyntheticSerializer.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/RippleLedgerHash.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/rdb/RelationalDatabase.h>
#include <xrpl/resource/Fees.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

namespace xrpl {

using TxnsData = RelationalDatabase::AccountTxs;
using TxnsDataBinary = RelationalDatabase::MetaTxsList;
using TxnDataBinary = RelationalDatabase::txnMetaLedgerType;
using AccountTxArgs = RelationalDatabase::AccountTxArgs;
using AccountTxResult = RelationalDatabase::AccountTxResult;
using LedgerSpecifier = RelationalDatabase::LedgerSpecifier;

// parses args into a ledger specifier, or returns a Json object on error
std::variant<std::optional<LedgerSpecifier>, json::Value>
parseLedgerArgs(RPC::Context& context, json::Value const& params)
{
    json::Value response;
    // if ledger_index_min or max is specified, then ledger_hash or ledger_index
    // should not be specified. Error out if it is
    if (context.apiVersion > 1u)
    {
        if ((params.isMember(jss::ledger_index_min) || params.isMember(jss::ledger_index_max)) &&
            (params.isMember(jss::ledger_hash) || params.isMember(jss::ledger_index)))
        {
            RPC::Status const status{RpcInvalidParams, "invalidParams"};
            status.inject(response);
            return response;
        }
    }
    if (params.isMember(jss::ledger_index_min) || params.isMember(jss::ledger_index_max))
    {
        uint32_t const min =
            params.isMember(jss::ledger_index_min) && params[jss::ledger_index_min].asInt() >= 0
            ? params[jss::ledger_index_min].asUInt()
            : 0;
        uint32_t const max =
            params.isMember(jss::ledger_index_max) && params[jss::ledger_index_max].asInt() >= 0
            ? params[jss::ledger_index_max].asUInt()
            : UINT32_MAX;

        return LedgerRange{.min = min, .max = max};
    }
    if (params.isMember(jss::ledger_hash))
    {
        auto& hashValue = params[jss::ledger_hash];
        if (!hashValue.isString())
        {
            RPC::Status const status{RpcInvalidParams, "ledgerHashNotString"};
            status.inject(response);
            return response;
        }

        LedgerHash hash;
        if (!hash.parseHex(hashValue.asString()))
        {
            RPC::Status const status{RpcInvalidParams, "ledgerHashMalformed"};
            status.inject(response);
            return response;
        }
        return hash;
    }
    if (params.isMember(jss::ledger_index))
    {
        LedgerSpecifier ledger;
        if (params[jss::ledger_index].isNumeric())
        {
            ledger = params[jss::ledger_index].asUInt();
        }
        else
        {
            std::string const ledgerStr = params[jss::ledger_index].asString();

            if (ledgerStr == "current" || ledgerStr.empty())
            {
                ledger = LedgerShortcut::Current;
            }
            else if (ledgerStr == "closed")
            {
                ledger = LedgerShortcut::Closed;
            }
            else if (ledgerStr == "validated")
            {
                ledger = LedgerShortcut::Validated;
            }
            else
            {
                RPC::Status const status{RpcInvalidParams, "ledger_index string malformed"};
                status.inject(response);
                return response;
            }
        }
        return ledger;
    }
    return std::optional<LedgerSpecifier>{};
}

std::variant<LedgerRange, RPC::Status>
getLedgerRange(RPC::Context& context, std::optional<LedgerSpecifier> const& ledgerSpecifier)
{
    std::uint32_t uValidatedMin = 0;
    std::uint32_t uValidatedMax = 0;
    bool const bValidated = context.ledgerMaster.getValidatedRange(uValidatedMin, uValidatedMax);

    if (!bValidated)
    {
        // Don't have a validated ledger range.
        if (context.apiVersion == 1)
            return RpcLgrIdxsInvalid;
        return RpcNotSynced;
    }

    std::uint32_t uLedgerMin = uValidatedMin;
    std::uint32_t uLedgerMax = uValidatedMax;
    // Does request specify a ledger or ledger range?
    if (ledgerSpecifier)
    {
        auto status = std::visit(
            [&](auto const& ls) -> RPC::Status {
                using T = std::decay_t<decltype(ls)>;
                if constexpr (std::is_same_v<T, LedgerRange>)
                {
                    // if ledger_index_min or ledger_index_max is out of
                    // valid ledger range, error out. exclude -1 as
                    // it is a valid input
                    if (context.apiVersion > 1u)
                    {
                        if ((ls.max > uValidatedMax && ls.max != -1) ||
                            (ls.min < uValidatedMin && ls.min != 0))
                        {
                            return RpcLgrIdxMalformed;
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
                            return RpcLgrIdxsInvalid;
                        return RpcInvalidLgrRange;
                    }
                }
                else
                {
                    std::shared_ptr<ReadView const> ledgerView;
                    auto status = getLedger(ledgerView, ls, context);
                    if (!ledgerView)
                    {
                        return status;
                    }

                    bool const validated = context.ledgerMaster.isValidated(*ledgerView);

                    if (!validated || ledgerView->header().seq > uValidatedMax ||
                        ledgerView->header().seq < uValidatedMin)
                    {
                        return RpcLgrNotValidated;
                    }
                    uLedgerMin = uLedgerMax = ledgerView->header().seq;
                }
                return RPC::Status::kOK;
            },
            *ledgerSpecifier);

        if (status)
            return status;
    }
    return LedgerRange{.min = uLedgerMin, .max = uLedgerMax};
}

std::pair<AccountTxResult, RPC::Status>
doAccountTxHelp(RPC::Context& context, AccountTxArgs const& args)
{
    context.loadType = Resource::kFeeMediumBurdenRpc;

    AccountTxResult result;

    auto lgrRange = getLedgerRange(context, args.ledger);
    if (auto stat = std::get_if<RPC::Status>(&lgrRange))
    {
        // An error occurred getting the requested ledger range
        return {result, *stat};
    }

    result.ledgerRange = std::get<LedgerRange>(lgrRange);

    result.marker = args.marker;

    RelationalDatabase::AccountTxPageOptions const options = {
        .account = args.account,
        .ledgerRange = result.ledgerRange,
        .marker = result.marker,
        .limit = args.limit,
        .bAdmin = isUnlimited(context.role)};

    auto& db = context.app.getRelationalDatabase();

    if (args.binary)
    {
        if (args.forward)
        {
            auto [tx, marker] = db.oldestAccountTxPageB(options);
            result.transactions = tx;
            result.marker = marker;
        }
        else
        {
            auto [tx, marker] = db.newestAccountTxPageB(options);
            result.transactions = tx;
            result.marker = marker;
        }
    }
    else
    {
        if (args.forward)
        {
            auto [tx, marker] = db.oldestAccountTxPage(options);
            result.transactions = tx;
            result.marker = marker;
        }
        else
        {
            auto [tx, marker] = db.newestAccountTxPage(options);
            result.transactions = tx;
            result.marker = marker;
        }
    }

    result.limit = args.limit;
    JLOG(context.j.debug()) << __func__ << " : finished";

    return {result, RpcSuccess};
}

json::Value
populateJsonResponse(
    std::pair<AccountTxResult, RPC::Status> const& res,
    AccountTxArgs const& args,
    RPC::JsonContext const& context)
{
    json::Value response;
    RPC::Status const& error = res.second;
    if (error.toErrorCode() != RpcSuccess)
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

        json::Value& jvTxns = (response[jss::transactions] = json::ValueType::Array);

        if (auto txnsData = std::get_if<TxnsData>(&result.transactions))
        {
            XRPL_ASSERT(!args.binary, "xrpl::populateJsonResponse : binary is not set");

            for (auto const& [txn, txnMeta] : *txnsData)
            {
                if (txn)
                {
                    json::Value& jvObj = jvTxns.append(json::ValueType::Object);
                    jvObj[jss::validated] = true;

                    auto const jsonTx = (context.apiVersion > 1 ? jss::tx_json : jss::tx);
                    if (context.apiVersion > 1)
                    {
                        jvObj[jsonTx] = txn->getJson(
                            static_cast<JsonOptions::underlying_t>(
                                JsonOptions::Values::IncludeDate) |
                                static_cast<JsonOptions::underlying_t>(
                                    JsonOptions::Values::DisableApiPriorV2),
                            false);
                        jvObj[jss::hash] = to_string(txn->getID());
                        jvObj[jss::ledger_index] = txn->getLedger();
                        jvObj[jss::ledger_hash] =
                            to_string(context.ledgerMaster.getHashBySeq(txn->getLedger()));

                        if (auto closeTime =
                                context.ledgerMaster.getCloseTimeBySeq(txn->getLedger()))
                            jvObj[jss::close_time_iso] = toStringIso(*closeTime);
                    }
                    else
                    {
                        jvObj[jsonTx] = txn->getJson(JsonOptions::Values::IncludeDate);
                    }

                    auto const& sttx = txn->getSTransaction();
                    RPC::insertDeliverMax(jvObj[jsonTx], sttx->getTxnType(), context.apiVersion);
                    if (txnMeta)
                    {
                        jvObj[jss::meta] = txnMeta->getJson(JsonOptions::Values::IncludeDate);
                        insertDeliveredAmount(jvObj[jss::meta], context, txn, *txnMeta);
                        RPC::insertNFTSyntheticInJson(jvObj, sttx, *txnMeta);
                        RPC::insertMPTokenIssuanceID(jvObj[jss::meta], sttx, *txnMeta);
                    }
                    else
                    {
                        // LCOV_EXCL_START
                        UNREACHABLE(
                            "xrpl::populateJsonResponse : missing "
                            "transaction metadata");
                        // LCOV_EXCL_STOP
                    }
                }
            }
        }
        else
        {
            XRPL_ASSERT(args.binary, "xrpl::populateJsonResponse : binary is set");

            for (auto const& binaryData : std::get<TxnsDataBinary>(result.transactions))
            {
                json::Value& jvObj = jvTxns.append(json::ValueType::Object);

                jvObj[jss::tx_blob] = strHex(std::get<0>(binaryData));
                auto const jsonMeta = (context.apiVersion > 1 ? jss::meta_blob : jss::meta);
                jvObj[jsonMeta] = strHex(std::get<1>(binaryData));
                jvObj[jss::ledger_index] = std::get<2>(binaryData);
                jvObj[jss::validated] = true;
            }
        }

        if (result.marker)
        {
            response[jss::marker] = json::ValueType::Object;
            response[jss::marker][jss::ledger] = result.marker->ledgerSeq;
            response[jss::marker][jss::seq] = result.marker->txnSeq;
        }
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
json::Value
doAccountTx(RPC::JsonContext& context)
{
    if (!context.app.config().useTxTables())
        return rpcError(RpcNotEnabled);

    auto& params = context.params;
    AccountTxArgs args;
    json::Value response;

    // The document[https://xrpl.org/account_tx.html#account_tx] states that
    // binary and forward params are both boolean values, however, assigning any
    // string value works. Do not allow this. This check is for api Version 2
    // onwards only
    if (context.apiVersion > 1u && params.isMember(jss::binary) && !params[jss::binary].isBool())
    {
        return RPC::invalidFieldError(jss::binary);
    }
    if (context.apiVersion > 1u && params.isMember(jss::forward) && !params[jss::forward].isBool())
    {
        return RPC::invalidFieldError(jss::forward);
    }

    if (auto const err = RPC::readLimitField(args.limit, RPC::Tuning::kAccountTx, context))
        return *err;

    args.binary = params.isMember(jss::binary) && params[jss::binary].asBool();
    args.forward = params.isMember(jss::forward) && params[jss::forward].asBool();

    if (!params.isMember(jss::account))
        return RPC::missingFieldError(jss::account);

    if (!params[jss::account].isString())
        return RPC::invalidFieldError(jss::account);

    auto const account = parseBase58<AccountID>(params[jss::account].asString());
    if (!account)
        return rpcError(RpcActMalformed);

    args.account = *account;

    auto parseRes = parseLedgerArgs(context, params);
    if (auto jv = std::get_if<json::Value>(&parseRes))
    {
        return *jv;
    }

    args.ledger = std::get<std::optional<LedgerSpecifier>>(parseRes);

    if (params.isMember(jss::marker))
    {
        auto& token = params[jss::marker];
        if (!token.isMember(jss::ledger) || !token.isMember(jss::seq) ||
            !token[jss::ledger].isConvertibleTo(json::ValueType::UInt) ||
            !token[jss::seq].isConvertibleTo(json::ValueType::UInt))
        {
            RPC::Status const status{
                RpcInvalidParams,
                "invalid marker. Provide ledger index via ledger field, and "
                "transaction sequence number via seq field"};
            status.inject(response);
            return response;
        }
        args.marker = {
            .ledgerSeq = token[jss::ledger].asUInt(), .txnSeq = token[jss::seq].asUInt()};
    }

    auto res = doAccountTxHelp(context, args);
    JLOG(context.j.debug()) << __func__ << " populating response";
    return populateJsonResponse(res, args, context);
}

}  // namespace xrpl
