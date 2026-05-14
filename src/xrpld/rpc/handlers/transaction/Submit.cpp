/** @file
 *  Implements the `submit` RPC command handler.
 *
 *  Supports two mutually exclusive submission modes: a pre-signed binary blob
 *  (`tx_blob`) and a server-side signing path (`tx_json` + `secret`). The blob
 *  path is the production-safe mode; the signing path is deprecated.
 */

#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/misc/Transaction.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/Role.h>
#include <xrpld/rpc/detail/TransactionSign.h>

#include <xrpl/basics/Expected.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/StringUtilities.h>
#include <xrpl/basics/safe_cast.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/resource/Fees.h>
#include <xrpl/tx/apply.h>

#include <exception>
#include <functional>
#include <memory>

namespace xrpl {

/** Parse and validate the optional `fail_hard` request parameter.
 *
 *  Converts the boolean `fail_hard` field from the request JSON into a
 *  `NetworkOPs::FailHard` enum value. When `fail_hard` is true, the network
 *  layer will reject the transaction outright if it cannot be applied to the
 *  current open ledger, rather than queuing it for a future ledger. Omitting
 *  the field produces lenient (non-hard-fail) behavior.
 *
 *  @param context  The RPC request context; `context.params` is inspected for
 *      `jss::fail_hard`.
 *  @return The resolved `NetworkOPs::FailHard` value on success, or an error
 *      JSON value if `fail_hard` is present but not a boolean.
 */
static Expected<NetworkOPs::FailHard, json::Value>
getFailHard(RPC::JsonContext const& context)
{
    if (context.params.isMember(jss::fail_hard) && !context.params[jss::fail_hard].isBool())
    {
        return Unexpected(RPC::expectedFieldError(jss::fail_hard, "boolean"));
    }
    return NetworkOPs::doFailHard(
        context.params.isMember(jss::fail_hard) && context.params[jss::fail_hard].asBool());
}

/** Handle the `submit` RPC command.
 *
 *  Dispatches to one of two submission paths based on the presence of `tx_blob`
 *  in the request parameters:
 *
 *  - **tx_blob path (primary):** The hex-encoded, pre-signed transaction binary
 *    is decoded, deserialized into an `STTx`, validated (signature and local
 *    rules), wrapped in a `Transaction`, and forwarded to `NetworkOPs` for
 *    application to the open ledger and P2P broadcast.
 *  - **tx_json path (deprecated):** Falls back to server-side signing via
 *    `RPC::transactionSubmit`. Requires `ADMIN` role or `canSign()` enabled in
 *    configuration. Every response from this path includes a `deprecated`
 *    warning.
 *
 *  On success the response contains `tx_json`, `tx_blob`, and — when the
 *  network reached a deterministic result — `engine_result`,
 *  `engine_result_code`, `engine_result_message`, `accepted`, `applied`,
 *  `broadcast`, `queued`, `kept`, and advisory ledger-state fields
 *  (`account_sequence_next`, `account_sequence_available`,
 *  `open_ledger_cost`, `validated_ledger_index`).
 *
 *  @param context  The RPC request context. `context.params` must contain
 *      either `tx_blob` (hex string) or `tx_json` + `secret`. The optional
 *      `fail_hard` boolean controls queuing behaviour on rejection.
 *  @return A JSON object representing the submission outcome, or a structured
 *      error object. Possible error keys: `invalidTransaction` (bad binary or
 *      local-check failure), `internalSubmit` (exception during network
 *      dispatch), `internalJson` (exception during response serialization).
 *  @note When `context.app.checkSigs()` is false, signature verification is
 *      skipped via `forceValidity` (pre-marking the transaction as
 *      `SigGoodOnly` in the `HashRouter` cache). This is intended only for
 *      trusted internal submissions where the caller guarantees validity.
 *  @note Resource cost is registered as `feeMediumBurdenRPC` at entry,
 *      regardless of outcome.
 */
json::Value
doSubmit(RPC::JsonContext& context)
{
    context.loadType = Resource::kFEE_MEDIUM_BURDEN_RPC;

    if (!context.params.isMember(jss::tx_blob))
    {
        auto const failType = getFailHard(context);
        if (!failType)
            return failType.error();

        if (context.role != Role::ADMIN && !context.app.config().canSign())
            return RPC::makeError(RpcNotSupported, "Signing is not supported by this server.");

        auto ret = RPC::transactionSubmit(
            context.params,
            context.apiVersion,
            *failType,
            context.role,
            context.ledgerMaster.getValidatedLedgerAge(),
            context.app,
            RPC::getProcessTxnFn(context.netOps));

        ret[jss::deprecated] =
            "Signing support in the 'submit' command has been "
            "deprecated and will be removed in a future version "
            "of the server. Please migrate to a standalone "
            "signing tool.";

        return ret;
    }

    json::Value jvResult;

    auto ret = strUnHex(context.params[jss::tx_blob].asString());

    if (!ret || ret->empty())
        return rpcError(RpcInvalidParams);

    SerialIter sitTrans(makeSlice(*ret));

    std::shared_ptr<STTx const> stTx;

    try
    {
        stTx = std::make_shared<STTx const>(std::ref(sitTrans));
    }
    catch (std::exception& e)
    {
        jvResult[jss::error] = "invalidTransaction";
        jvResult[jss::error_exception] = e.what();

        return jvResult;
    }

    {
        if (!context.app.checkSigs())
        {
            forceValidity(
                context.app.getHashRouter(), stTx->getTransactionID(), Validity::SigGoodOnly);
        }
        auto [validity, reason] = checkValidity(
            context.app.getHashRouter(), *stTx, context.ledgerMaster.getCurrentLedger()->rules());
        if (validity != Validity::Valid)
        {
            jvResult[jss::error] = "invalidTransaction";
            jvResult[jss::error_exception] = "fails local checks: " + reason;

            return jvResult;
        }
    }

    std::string reason;
    auto transaction = std::make_shared<Transaction>(stTx, reason, context.app);
    if (transaction->getStatus() != TransStatus::NEW)
    {
        jvResult[jss::error] = "invalidTransaction";
        jvResult[jss::error_exception] = "fails local checks: " + reason;

        return jvResult;
    }

    try
    {
        auto const failType = getFailHard(context);
        if (!failType)
            return failType.error();

        context.netOps.processTransaction(transaction, isUnlimited(context.role), true, *failType);
    }
    catch (std::exception& e)
    {
        jvResult[jss::error] = "internalSubmit";
        jvResult[jss::error_exception] = e.what();

        return jvResult;
    }

    try
    {
        jvResult[jss::tx_json] = transaction->getJson(JsonOptions::Values::None);
        jvResult[jss::tx_blob] = strHex(transaction->getSTransaction()->getSerializer().peekData());

        if (temUNCERTAIN != transaction->getResult())
        {
            std::string sToken;
            std::string sHuman;

            transResultInfo(transaction->getResult(), sToken, sHuman);

            jvResult[jss::engine_result] = sToken;
            jvResult[jss::engine_result_code] = transaction->getResult();
            jvResult[jss::engine_result_message] = sHuman;

            auto const submitResult = transaction->getSubmitResult();

            jvResult[jss::accepted] = submitResult.any();
            jvResult[jss::applied] = submitResult.applied;
            jvResult[jss::broadcast] = submitResult.broadcast;
            jvResult[jss::queued] = submitResult.queued;
            jvResult[jss::kept] = submitResult.kept;

            if (auto currentLedgerState = transaction->getCurrentLedgerState())
            {
                jvResult[jss::account_sequence_next] =
                    safeCast<json::Value::UInt>(currentLedgerState->accountSeqNext);
                jvResult[jss::account_sequence_available] =
                    safeCast<json::Value::UInt>(currentLedgerState->accountSeqAvail);
                jvResult[jss::open_ledger_cost] = to_string(currentLedgerState->minFeeRequired);
                jvResult[jss::validated_ledger_index] =
                    safeCast<json::Value::UInt>(currentLedgerState->validatedLedger);
            }
        }

        return jvResult;
    }
    catch (std::exception& e)
    {
        jvResult[jss::error] = "internalJson";
        jvResult[jss::error_exception] = e.what();

        return jvResult;
    }
}

}  // namespace xrpl
