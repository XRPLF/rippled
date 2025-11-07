#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/misc/Transaction.h>
#include <xrpld/app/tx/apply.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/TransactionSign.h>

#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/resource/Fees.h>

namespace ripple {

static NetworkOPs::FailHard
getFailHard(RPC::JsonContext const& context)
{
    return NetworkOPs::doFailHard(
        context.params.isMember("fail_hard") &&
        context.params["fail_hard"].asBool());
}

// {
//   tx_blob: <string> XOR tx_json: <object>,
//   secret: <secret>
// }
Json::Value
doSubmit(RPC::JsonContext& context)
{
    context.loadType = Resource::feeMediumBurdenRPC;

    if (!context.params.isMember(jss::tx_blob))
    {
        auto const failType = getFailHard(context);

        if (context.role != Role::ADMIN && !context.app.config().canSign())
            return RPC::make_error(
                rpcNOT_SUPPORTED, "Signing is not supported by this server.");

        auto ret = RPC::transactionSubmit(
            context.params,
            context.apiVersion,
            failType,
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

    Json::Value jvResult;

    auto ret = strUnHex(context.params[jss::tx_blob].asString());

    if (!ret || !ret->size())
        return rpcError(rpcINVALID_PARAMS);

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
            forceValidity(
                context.app.getHashRouter(),
                stTx->getTransactionID(),
                Validity::SigGoodOnly);
        auto [validity, reason] = checkValidity(
            context.app.getHashRouter(),
            *stTx,
            context.ledgerMaster.getCurrentLedger()->rules(),
            context.app.config());
        if (validity != Validity::Valid)
        {
            jvResult[jss::error] = "invalidTransaction";
            jvResult[jss::error_exception] = "fails local checks: " + reason;

            return jvResult;
        }
    }

    std::string reason;
    auto transaction = std::make_shared<Transaction>(stTx, reason, context.app);
    if (transaction->getStatus() != NEW)
    {
        jvResult[jss::error] = "invalidTransaction";
        jvResult[jss::error_exception] = "fails local checks: " + reason;

        return jvResult;
    }

    try
    {
        auto const failType = getFailHard(context);

        context.netOps.processTransaction(
            transaction, isUnlimited(context.role), true, failType);
    }
    catch (std::exception& e)
    {
        jvResult[jss::error] = "internalSubmit";
        jvResult[jss::error_exception] = e.what();

        return jvResult;
    }

    try
    {
        jvResult[jss::tx_json] = transaction->getJson(JsonOptions::none);
        jvResult[jss::tx_blob] =
            strHex(transaction->getSTransaction()->getSerializer().peekData());

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
                    safe_cast<Json::Value::UInt>(
                        currentLedgerState->accountSeqNext);
                jvResult[jss::account_sequence_available] =
                    safe_cast<Json::Value::UInt>(
                        currentLedgerState->accountSeqAvail);
                jvResult[jss::open_ledger_cost] =
                    to_string(currentLedgerState->minFeeRequired);
                jvResult[jss::validated_ledger_index] =
                    safe_cast<Json::Value::UInt>(
                        currentLedgerState->validatedLedger);
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

}  // namespace ripple
