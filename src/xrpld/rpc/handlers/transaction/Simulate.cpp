#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/ledger/OpenLedger.h>
#include <xrpld/app/misc/Transaction.h>
#include <xrpld/app/misc/TxQ.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/DeliveredAmount.h>
#include <xrpld/rpc/MPTokenIssuanceID.h>
#include <xrpld/rpc/detail/TransactionSign.h>

#include <xrpl/basics/Expected.h>
#include <xrpl/basics/Log.h>
#include <xrpl/basics/Number.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/StringUtilities.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/core/NetworkIDService.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/NFTSyntheticSerializer.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STParsedJSON.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/resource/Fees.h>

#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace xrpl {

static Expected<std::uint32_t, json::Value>
getAutofillSequence(json::Value const& txJson, RPC::JsonContext& context)
{
    // autofill Sequence
    bool const hasTicketSeq = txJson.isMember(sfTicketSequence.jsonName);
    auto const& accountStr = txJson[jss::Account];
    if (!accountStr.isString())
    {
        // sanity check, should fail earlier
        // LCOV_EXCL_START
        return Unexpected(RPC::invalidFieldError("tx.Account"));
        // LCOV_EXCL_STOP
    }
    auto const srcAddressID = parseBase58<AccountID>(accountStr.asString());
    if (!srcAddressID.has_value())
    {
        return Unexpected(
            RPC::makeError(RpcSrcActMalformed, RPC::invalidFieldMessage("tx.Account")));
    }
    std::shared_ptr<SLE const> const sle =
        context.app.getOpenLedger().current()->read(keylet::account(*srcAddressID));
    if (!hasTicketSeq && !sle)
    {
        JLOG(context.app.getJournal("Simulate").debug())
            << "Failed to find source account "
            << "in current ledger: " << toBase58(*srcAddressID);

        return Unexpected(rpcError(RpcSrcActNotFound));
    }

    return hasTicketSeq ? 0 : context.app.getTxQ().nextQueuableSeq(sle).value();
}

static std::optional<json::Value>
autofillSignature(json::Value& sigObject)
{
    if (!sigObject.isMember(jss::SigningPubKey))
    {
        // autofill SigningPubKey
        sigObject[jss::SigningPubKey] = "";
    }

    if (sigObject.isMember(jss::Signers))
    {
        if (!sigObject[jss::Signers].isArray())
            return RPC::invalidFieldError("tx.Signers");
        // check multisigned signers
        for (unsigned index = 0; index < sigObject[jss::Signers].size(); index++)
        {
            auto& signer = sigObject[jss::Signers][index];
            if (!signer.isObject() || !signer.isMember(jss::Signer) ||
                !signer[jss::Signer].isObject())
                return RPC::invalidFieldError("tx.Signers[" + std::to_string(index) + "]");

            if (!signer[jss::Signer].isMember(jss::SigningPubKey))
            {
                // autofill SigningPubKey
                signer[jss::Signer][jss::SigningPubKey] = "";
            }

            if (!signer[jss::Signer].isMember(jss::TxnSignature))
            {
                // autofill TxnSignature
                signer[jss::Signer][jss::TxnSignature] = "";
            }
            else if (signer[jss::Signer][jss::TxnSignature] != "")
            {
                // Transaction must not be signed
                return rpcError(RpcTxSigned);
            }
        }
    }

    if (!sigObject.isMember(jss::TxnSignature))
    {
        // autofill TxnSignature
        sigObject[jss::TxnSignature] = "";
    }
    else if (sigObject[jss::TxnSignature] != "")
    {
        // Transaction must not be signed
        return rpcError(RpcTxSigned);
    }
    return std::nullopt;
}

static std::optional<json::Value>
autofillTx(json::Value& txJson, RPC::JsonContext& context)
{
    if (!txJson.isMember(jss::Fee))
    {
        // autofill Fee
        // Must happen after all the other autofills happen
        // Error handling/messaging works better that way
        auto feeOrError = RPC::getCurrentNetworkFee(
            context.role,
            context.app.config(),
            context.app.getFeeTrack(),
            context.app.getTxQ(),
            context.app,
            txJson);
        if (feeOrError.isMember(jss::error))
            return feeOrError;
        txJson[jss::Fee] = feeOrError;
    }

    if (auto error = autofillSignature(txJson))
        return error;

    if (!txJson.isMember(jss::Sequence))
    {
        auto const seq = getAutofillSequence(txJson, context);
        if (!seq)
            return seq.error();
        txJson[sfSequence.jsonName] = *seq;
    }

    if (!txJson.isMember(jss::NetworkID))
    {
        auto const networkId = context.app.getNetworkIDService().getNetworkID();
        if (networkId > 1024)
            txJson[jss::NetworkID] = to_string(networkId);
    }

    return std::nullopt;
}

static json::Value
getTxJsonFromParams(json::Value const& params)
{
    json::Value txJson;

    if (params.isMember(jss::tx_blob))
    {
        if (params.isMember(jss::tx_json))
        {
            return RPC::makeParamError("Can only include one of `tx_blob` and `tx_json`.");
        }

        auto const txBlob = params[jss::tx_blob];
        if (!txBlob.isString())
        {
            return RPC::invalidFieldError(jss::tx_blob);
        }

        auto unHexed = strUnHex(txBlob.asString());
        if (!unHexed || unHexed->empty())
            return RPC::invalidFieldError(jss::tx_blob);

        try
        {
            SerialIter sitTrans(makeSlice(*unHexed));
            txJson = STObject(std::ref(sitTrans), sfGeneric).getJson(JsonOptions::Values::None);
        }
        catch (std::runtime_error const&)
        {
            return RPC::invalidFieldError(jss::tx_blob);
        }
    }
    else if (params.isMember(jss::tx_json))
    {
        txJson = params[jss::tx_json];
        if (!txJson.isObject())
        {
            return RPC::objectFieldError(jss::tx_json);
        }
    }
    else
    {
        return RPC::makeParamError("Neither `tx_blob` nor `tx_json` included.");
    }

    // basic sanity checks for transaction shape
    if (!txJson.isMember(jss::TransactionType))
    {
        return RPC::missingFieldError("tx.TransactionType");
    }

    if (!txJson.isMember(jss::Account))
    {
        return RPC::missingFieldError("tx.Account");
    }

    return txJson;
}

static json::Value
simulateTxn(RPC::JsonContext& context, std::shared_ptr<Transaction> transaction)
{
    json::Value jvResult;
    // Process the transaction
    OpenView view = *context.app.getOpenLedger().current();
    auto const result = context.app.getTxQ().apply(
        context.app, view, transaction->getSTransaction(), TapDryRun, context.j);

    jvResult[jss::applied] = result.applied;
    jvResult[jss::ledger_index] = view.seq();

    bool const isBinaryOutput = context.params.get(jss::binary, false).asBool();

    // Convert the TER to human-readable values
    std::string token;
    std::string message;
    if (transResultInfo(result.ter, token, message))
    {
        // Engine result
        jvResult[jss::engine_result] = token;
        jvResult[jss::engine_result_code] = result.ter;
        jvResult[jss::engine_result_message] = message;
    }
    else
    {
        // shouldn't be hit
        // LCOV_EXCL_START
        jvResult[jss::engine_result] = "unknown";
        jvResult[jss::engine_result_code] = result.ter;
        jvResult[jss::engine_result_message] = "unknown";
        // LCOV_EXCL_STOP
    }

    if (token == "tesSUCCESS")
    {
        jvResult[jss::engine_result_message] = "The simulated transaction would have been applied.";
    }

    if (result.metadata)
    {
        if (isBinaryOutput)
        {
            auto const metaBlob = result.metadata->getAsObject().getSerializer().getData();
            jvResult[jss::meta_blob] = strHex(makeSlice(metaBlob));
        }
        else
        {
            jvResult[jss::meta] = result.metadata->getJson(JsonOptions::Values::None);
            RPC::insertDeliveredAmount(
                jvResult[jss::meta], view, transaction->getSTransaction(), *result.metadata);
            RPC::insertNFTSyntheticInJson(
                jvResult, transaction->getSTransaction(), *result.metadata);
            RPC::insertMPTokenIssuanceID(
                jvResult[jss::meta], transaction->getSTransaction(), *result.metadata);
        }
    }

    if (isBinaryOutput)
    {
        auto const txBlob = transaction->getSTransaction()->getSerializer().getData();
        jvResult[jss::tx_blob] = strHex(makeSlice(txBlob));
    }
    else
    {
        jvResult[jss::tx_json] = transaction->getJson(JsonOptions::Values::None);
    }

    return jvResult;
}

// {
//   tx_blob: <string> XOR tx_json: <object>,
//   binary: <bool>
// }
json::Value
doSimulate(RPC::JsonContext& context)
{
    context.loadType = Resource::kFeeMediumBurdenRpc;

    json::Value txJson;  // the tx as a JSON

    // check validity of `binary` param
    if (context.params.isMember(jss::binary) && !context.params[jss::binary].isBool())
    {
        return RPC::invalidFieldError(jss::binary);
    }

    for (auto const field : {jss::secret, jss::seed, jss::seed_hex, jss::passphrase})
    {
        if (context.params.isMember(field))
        {
            return RPC::invalidFieldError(field);
        }
    }

    // get JSON equivalent of transaction
    txJson = getTxJsonFromParams(context.params);
    if (txJson.isMember(jss::error))
        return txJson;

    // autofill fields if they're not included (e.g. `Fee`, `Sequence`)
    if (auto error = autofillTx(txJson, context))
        return *error;

    STParsedJSONObject parsed(std::string(jss::tx_json), txJson);
    if (!parsed.object.has_value())
        return parsed.error;

    std::shared_ptr<STTx const> stTx;
    try
    {
        stTx = std::make_shared<STTx>(std::move(parsed.object.value()));
    }
    catch (std::exception& e)
    {
        json::Value jvResult = json::ValueType::Object;
        jvResult[jss::error] = "invalidTransaction";
        jvResult[jss::error_exception] = e.what();
        return jvResult;
    }

    if (stTx->getTxnType() == ttBATCH)
    {
        return RPC::makeError(RpcNotImpl);
    }

    std::string reason;
    auto transaction = std::make_shared<Transaction>(stTx, reason, context.app);
    // Actually run the transaction through the transaction processor
    try
    {
        return simulateTxn(context, transaction);
    }
    // LCOV_EXCL_START this is just in case, so xrpld doesn't crash
    catch (std::exception const& e)
    {
        json::Value jvResult = json::ValueType::Object;
        jvResult[jss::error] = "internalSimulate";
        jvResult[jss::error_exception] = e.what();
        return jvResult;
    }
    // LCOV_EXCL_STOP
}

}  // namespace xrpl
