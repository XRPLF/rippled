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
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/GRPCHandlers.h>
#include <xrpld/rpc/detail/RPCHelpers.h>
#include <xrpld/rpc/detail/TransactionSign.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/STParsedJSON.h>
#include <xrpl/resource/Fees.h>

namespace ripple {

std::optional<Json::Value>
autofillTx(Json::Value& tx_json, RPC::JsonContext& context)
{
    if (!tx_json.isMember(jss::Fee))
    {
        // autofill Fee
        // Must happen after all the other autofills happen
        // Error handling/messaging works better that way
        auto feeOrError = RPC::getCurrentFee(
            context.role,
            context.app.config(),
            context.app.getFeeTrack(),
            context.app.getTxQ(),
            context.app,
            tx_json);
        if (feeOrError.isMember(jss::error))
            return feeOrError;
        tx_json[jss::Fee] = feeOrError;
    }
    if (!tx_json.isMember(sfSigningPubKey.jsonName))
    {
        // autofill SigningPubKey
        tx_json[sfSigningPubKey.jsonName] = "";
    }
    if (tx_json.isMember(jss::Signers))
    {
        if (!tx_json[jss::Signers].isArray())
            return RPC::invalid_field_error("tx.Signers");
        // check multisigned signers
        for (int index = 0; index < tx_json[jss::Signers].size(); index++)
        {
            auto& signer = tx_json[jss::Signers][index];
            if (!signer.isObject() || !signer.isMember(jss::Signer) ||
                !signer[jss::Signer].isObject())
                return RPC::invalid_field_error(
                    "tx.Signers[" + std::to_string(index) + "]");
            if (!signer[jss::Signer].isMember(sfTxnSignature.jsonName))
            {
                // autofill TxnSignature
                signer[jss::Signer][sfTxnSignature.jsonName] = "";
            }
            else if (signer[jss::Signer][sfTxnSignature.jsonName] != "")
            {
                // Transaction must not be signed
                return rpcError(rpcTX_SIGNED);
            }
        }
    }
    if (!tx_json.isMember(sfTxnSignature.jsonName))
    {
        // autofill TxnSignature
        tx_json[sfTxnSignature.jsonName] = "";
    }
    else if (tx_json[sfTxnSignature.jsonName] != "")
    {
        // Transaction must not be signed
        return rpcError(rpcTX_SIGNED);
    }
    if (!tx_json.isMember(jss::Sequence))
    {
        // autofill Sequence
        bool const hasTicketSeq = tx_json.isMember(sfTicketSequence.jsonName);
        auto const accountStr = tx_json[jss::Account];
        if (!accountStr.isString())
        {
            // sanity check, should fail earlier
            return RPC::invalid_field_error("tx.Account");  // LCOV_EXCL_LINE
        }
        auto const srcAddressID =
            parseBase58<AccountID>(tx_json[jss::Account].asString());
        if (!srcAddressID.has_value())
        {
            return RPC::make_error(
                rpcSRC_ACT_MALFORMED, RPC::invalid_field_message("tx.Account"));
        }
        std::shared_ptr<SLE const> sle =
            context.app.openLedger().current()->read(
                keylet::account(*srcAddressID));
        if (!hasTicketSeq && !sle)
        {
            JLOG(context.app.journal("Simulate").debug())
                << "Failed to find source account "
                << "in current ledger: " << toBase58(*srcAddressID);

            return rpcError(rpcSRC_ACT_NOT_FOUND);
        }
        tx_json[jss::Sequence] = hasTicketSeq
            ? 0
            : context.app.getTxQ().nextQueuableSeq(sle).value();
    }

    return std::nullopt;
}

// {
//   tx_blob: <string>,
//   tx_json: <object>,
//   binary: <bool>
// }
// tx_blob XOR tx_json
Json::Value
doSimulate(RPC::JsonContext& context)
{
    context.loadType = Resource::feeMediumBurdenRPC;

    Json::Value jvResult;  // the returned result
    Json::Value tx_json;   // the tx as a JSON

    // check validity of `binary` param
    if (context.params.isMember(jss::binary))
    {
        auto const binary = context.params[jss::binary];
        if (!binary.isBool())
        {
            return RPC::invalid_field_error(jss::binary);
        }
    }

    if (context.params.isMember(jss::tx_blob))
    {
        if (context.params.isMember(jss::tx_json))
        {
            // both `tx_blob` and `tx_json` included
            return rpcError(rpcINVALID_PARAMS);
        }
        auto const blob = context.params[jss::tx_blob];
        if (!blob.isString())
        {
            return RPC::invalid_field_error(jss::tx_blob);
        }
        auto unHexed = strUnHex(blob.asString());

        if (!unHexed || !unHexed->size())
            return RPC::invalid_field_error(jss::tx_blob);
        try
        {
            SerialIter sitTrans(makeSlice(*unHexed));
            tx_json = STObject(std::ref(sitTrans), sfGeneric)
                          .getJson(JsonOptions::none);
        }
        catch (std::runtime_error& e)
        {
            return RPC::invalid_field_error(jss::tx_blob);
        }
    }
    else if (context.params.isMember(jss::tx_json))
    {
        tx_json = context.params[jss::tx_json];
        if (!tx_json.isObject())
        {
            return RPC::object_field_error(jss::tx_json);
        }
    }
    else
    {
        // neither `tx_blob` nor `tx_json` included
        return rpcError(rpcINVALID_PARAMS);
    }

    // basic sanity checks for transaction shape
    if (!tx_json.isMember(jss::TransactionType))
    {
        return RPC::missing_field_error("tx.TransactionType");
    }
    if (!tx_json.isMember(jss::Account))
    {
        return RPC::missing_field_error("tx.Account");
    }

    // autofill fields if they're not included (e.g. `Fee`, `Sequence`)
    if (auto error = autofillTx(tx_json, context))
        return *error;

    STParsedJSONObject parsed(std::string(jss::tx_json), tx_json);
    if (!parsed.object.has_value())
    {
        jvResult[jss::error] = parsed.error[jss::error];
        jvResult[jss::error_code] = parsed.error[jss::error_code];
        jvResult[jss::error_message] = parsed.error[jss::error_message];
        return jvResult;
    }

    std::shared_ptr<STTx const> stpTrans;
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

    std::string reason;
    auto tpTrans = std::make_shared<Transaction>(stpTrans, reason, context.app);

    // Actually run the transaction through the transaction processor
    try
    {
        ApplyFlags flags = tapDRY_RUN;

        // Process the transaction
        OpenView view = *context.app.openLedger().current();
        auto const result = context.app.getTxQ().apply(
            context.app, view, tpTrans->getSTransaction(), flags, context.j);

        jvResult[jss::applied] = result.applied;
        jvResult[jss::ledger_index] = view.seq();

        const bool isBinaryOutput =
            context.params.get(jss::binary, false).asBool();

        // Convert the TER to human-readable values
        std::string sToken;
        std::string sHuman;
        transResultInfo(result.ter, sToken, sHuman);

        // Engine result
        jvResult[jss::engine_result] = sToken;
        jvResult[jss::engine_result_code] = result.ter;
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
                jvResult[jss::meta_blob] = strHex(makeSlice(metaBlob));
            }
            else
            {
                jvResult[jss::meta] =
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
    // LCOV_EXCL_START this is just in case, so rippled doesn't crash
    catch (std::exception& e)
    {
        jvResult[jss::error] = "internalSimulate";
        jvResult[jss::error_exception] = e.what();
        return jvResult;
        // LCOV_EXCL_STOP
    }
}

}  // namespace ripple
