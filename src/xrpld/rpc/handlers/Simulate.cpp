//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2024 Ripple Labs Inc.

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

#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/ledger/OpenLedger.h>
#include <xrpld/app/misc/HashRouter.h>
#include <xrpld/app/misc/Transaction.h>
#include <xrpld/app/tx/apply.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/resource/Fees.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/GRPCHandlers.h>
#include <xrpld/rpc/detail/RPCHelpers.h>
#include <xrpld/rpc/detail/TransactionSign.h>

namespace ripple {

class OpenLedger;

// static NetworkOPs::FailHard
// getFailHard(RPC::JsonContext const& context)
// {
//     return NetworkOPs::doFailHard(
//         context.params.isMember("fail_hard") &&
//         context.params["fail_hard"].asBool());
// }

// {
//   tx_blob: <object>
// }
Json::Value
doSimulate(RPC::JsonContext& context)
{
    context.loadType = Resource::feeMediumBurdenRPC;

    // TODO: also support tx_json, and fee/sequence autofill
    if (!context.params.isMember(jss::tx_blob))
        return rpcError(rpcINVALID_PARAMS);

    Json::Value jvResult;

    auto ret = strUnHex(context.params[jss::tx_blob].asString());

    if (!ret || !ret->size())
        return rpcError(rpcINVALID_PARAMS);

    SerialIter sitTrans(makeSlice(*ret));

    std::shared_ptr<STTx const> stpTrans;

    try
    {
        stpTrans = std::make_shared<STTx const>(std::ref(sitTrans));
    }
    catch (std::exception& e)
    {
        jvResult[jss::error] = "invalidTransaction";
        jvResult[jss::error_exception] = e.what();

        return jvResult;
    }

    std::string reason;
    auto tpTrans = std::make_shared<Transaction>(stpTrans, reason, context.app);

    try
    {
        // we check before adding to the batch
        ApplyFlags flags = tapDRY_RUN;

        // if (getFailHard(context) == NetworkOps::FailHard::yes)
        //     flags |= tapFAIL_HARD;

        auto view = *context.app.openLedger().current();
        auto const result = context.app.getTxQ().apply(
            context.app, view, tpTrans->getSTransaction(), flags, context.j);
        // e.result = result.first;
        // e.applied = result.second;

        std::string sToken;
        std::string sHuman;

        transResultInfo(result.first, sToken, sHuman);

        jvResult[jss::engine_result] = sToken;
        jvResult[jss::engine_result_code] = result.first;
        jvResult[jss::engine_result_message] = sHuman;
        jvResult[jss::applied] = result.second;
    }
    catch (std::exception& e)
    {
        jvResult[jss::error] = "internalSimulate";
        jvResult[jss::error_exception] = e.what();

        return jvResult;
    }

    try
    {
        jvResult[jss::tx_json] = tpTrans->getJson(JsonOptions::none);
        jvResult[jss::tx_blob] =
            strHex(tpTrans->getSTransaction()->getSerializer().peekData());

        // if (temUNCERTAIN != tpTrans->getResult())
        // {
        std::string sToken;
        std::string sHuman;

        transResultInfo(tpTrans->getResult(), sToken, sHuman);

        // jvResult[jss::engine_result] = sToken;
        // jvResult[jss::engine_result_code] = tpTrans->getResult();
        // jvResult[jss::engine_result_message] = sHuman;

        auto const submitResult = tpTrans->getSubmitResult();

        jvResult[jss::accepted] = submitResult.any();
        // jvResult[jss::applied] = submitResult.applied;
        jvResult[jss::broadcast] = submitResult.broadcast;
        jvResult[jss::queued] = submitResult.queued;
        jvResult[jss::kept] = submitResult.kept;

        if (auto currentLedgerState = tpTrans->getCurrentLedgerState())
        {
            jvResult[jss::account_sequence_next] = safe_cast<Json::Value::UInt>(
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
        // }

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
