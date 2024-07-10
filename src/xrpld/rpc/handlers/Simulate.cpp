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
#include <xrpl/protocol/STParsedJSON.h>
#include <xrpld/app/misc/LoadFeeTrack.h>

namespace ripple {

Json::Value
getFee(Application& app, Role& role)
{
    XRPAmount const feeDefault = app.config().FEES.reference_fee;

    auto ledger = app.openLedger().current();
    // Administrative and identified endpoints are exempt from local fees.
    XRPAmount const loadFee = scaleFeeLoad(
        feeDefault, app.getFeeTrack(), ledger->fees(), isUnlimited(role));
    XRPAmount fee = loadFee;
    {
        auto const metrics = app.getTxQ().getMetrics(*ledger);
        auto const baseFee = ledger->fees().base;
        auto escalatedFee =
            toDrops(metrics.openLedgerFeeLevel - FeeLevel64(1), baseFee) + 1;
        fee = std::max(fee, escalatedFee);
    }

    auto const limit = [&]() {
        // Scale fee units to drops:
        auto const result = mulDiv(
            feeDefault,
            RPC::Tuning::defaultAutoFillFeeMultiplier,
            RPC::Tuning::defaultAutoFillFeeDivisor);
        if (!result)
            Throw<std::overflow_error>("mulDiv");
        return *result;
    }();

    if (fee > limit)
    {
        std::stringstream ss;
        ss << "Fee of " << fee << " exceeds the requested tx limit of "
           << limit;
        return RPC::make_error(rpcHIGH_FEE, ss.str());
    }

    return fee.jsonClipped();
}

// {
//   tx_blob: <object>
// }
Json::Value
doSimulate(RPC::JsonContext& context)
{
    context.loadType = Resource::feeMediumBurdenRPC;

    std::shared_ptr<STTx const> stpTrans;
    Json::Value jvResult;

    // check validity of `binary` param
    if (context.params.isMember(jss::binary))
    {
        auto const binary = context.params[jss::binary];
        if (!binary.isBool())
        {
            return rpcError(rpcINVALID_PARAMS);
        }
    }

    // TODO: also support fee/sequence autofill
    if (context.params.isMember(jss::tx_blob))
    {
        if (context.params.isMember(jss::tx_json))
        {
            return rpcError(rpcINVALID_PARAMS);
        }

        auto ret = strUnHex(context.params[jss::tx_blob].asString());

        if (!ret || !ret->size())
            return RPC::make_error(
                rpcINVALID_PARAMS, RPC::invalid_field_message("tx_blob"));

        SerialIter sitTrans(makeSlice(*ret));

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
    }
    else
    {
        if (!context.params.isMember(jss::tx_json))
        {
            return rpcError(rpcINVALID_PARAMS);
        }

        Json::Value& tx_json(context.params[jss::tx_json]);
        if (!tx_json.isMember(jss::Fee))
        {
            tx_json[jss::Fee] = getFee(context.app, context.role);
        }
        if (!tx_json.isMember(sfSigningPubKey.jsonName))
        {
            tx_json[sfSigningPubKey.jsonName] = "";
        }
        else if (tx_json[sfSigningPubKey.jsonName] != "")
        {
            return RPC::make_error(
                rpcINVALID_PARAMS,
                RPC::invalid_field_message("tx_json.SigningPubKey"));
        }
        if (!tx_json.isMember(sfTxnSignature.jsonName))
        {
            tx_json[sfTxnSignature.jsonName] = "";
        }
        else if (tx_json[sfTxnSignature.jsonName] != "")
        {
            return RPC::make_error(
                rpcINVALID_PARAMS,
                RPC::invalid_field_message("tx_json.TxnSignature"));
        }
        if (!tx_json.isMember(jss::Sequence))
        {
            bool const hasTicketSeq =
                tx_json.isMember(sfTicketSequence.jsonName);
            auto const srcAddressID =
                *(parseBase58<AccountID>(tx_json[jss::Account].asString()));
            if (!srcAddressID)
            {
                return RPC::make_error(
                    rpcSRC_ACT_MALFORMED,
                    RPC::invalid_field_message("tx_json.Account"));
            }
            std::shared_ptr<SLE const> sle =
                context.app.openLedger().current()->read(
                    keylet::account(srcAddressID));
            if (!hasTicketSeq && !sle)
            {
                JLOG(context.app.journal("Simulate").debug())
                    << "simulate: Failed to find source account "
                    << "in current ledger: " << toBase58(srcAddressID);

                return rpcError(rpcSRC_ACT_NOT_FOUND);
            }
            tx_json[jss::Sequence] = hasTicketSeq
                ? 0
                : context.app.getTxQ().nextQueuableSeq(sle).value();
        }

        STParsedJSONObject parsed(std::string(jss::tx_json), tx_json);
        if (!parsed.object.has_value())
        {
            jvResult[jss::error] = parsed.error[jss::error];
            jvResult[jss::error_code] = parsed.error[jss::error_code];
            jvResult[jss::error_message] = parsed.error[jss::error_message];
            return jvResult;
        }

        try
        {
            stpTrans = std::make_shared<STTx>(std::move(parsed.object.value()));
        }
        catch (std::exception& e)
        {
            jvResult[jss::error] = "invalidTransaction";
            jvResult[jss::error_exception] = e.what();

            return jvResult;
        }
    }

    std::string reason;
    auto tpTrans = std::make_shared<Transaction>(stpTrans, reason, context.app);

    try
    {
        // we check before adding to the batch
        ApplyFlags flags = tapDRY_RUN;

        // Process the transaction
        OpenView view = *context.app.openLedger().current();
        auto const result = context.app.getTxQ().apply(
            context.app, view, tpTrans->getSTransaction(), flags, context.j);

        jvResult[jss::applied] = result.second;

        const bool isBinaryOutput =
            context.params.get(jss::binary, false).asBool();

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
        {
            if (isBinaryOutput)
            {
                auto const metaBlob =
                    result.metadata->getAsObject().getSerializer().getData();
                jvResult[jss::metadata] = strHex(makeSlice(metaBlob));
            }
            else
            {
                jvResult[jss::metadata] =
                    result.metadata->getJson(JsonOptions::none);
            }
        }
        if (isBinaryOutput)
        {
            auto const txBlob = stpTrans->getSerializer().getData();
            jvResult[jss::tx_blob] = strHex(makeSlice(txBlob));
        }
        else
        {
            jvResult[jss::tx_json] = tpTrans->getJson(JsonOptions::none);
        }

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
