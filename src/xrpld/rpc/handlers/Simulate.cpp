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

        // Process the transaction
        OpenView view = *context.app.openLedger().current();
        auto const result = context.app.getTxQ().apply(
            context.app, view, tpTrans->getSTransaction(), flags, context.j);

        jvResult[jss::applied] = result.second;

        // Convert the TER to human-readable values
        std::string sToken;
        std::string sHuman;
        transResultInfo(result.first, sToken, sHuman);

        // Engine result
        jvResult[jss::engine_result] = sToken;
        jvResult[jss::engine_result_code] = result.first;
        jvResult[jss::engine_result_message] = sHuman;
        if (sToken == "tesSUCCESS")
        {
            static const std::string alternateSuccessMessage =
                "The simulated transaction would have been applied.";
            jvResult[jss::engine_result_message] = alternateSuccessMessage;
        }

        if (result.metadata)
            jvResult[jss::metadata] =
                result.metadata->getJson(JsonOptions::none);

        return jvResult;
    }
    catch (std::exception& e)
    {
        jvResult[jss::error] = "internalSimulate";
        jvResult[jss::error_exception] = e.what();

        return jvResult;
    }
}

}  // namespace ripple
