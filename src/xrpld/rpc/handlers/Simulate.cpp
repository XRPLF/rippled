#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/ledger/OpenLedger.h>
#include <xrpld/app/misc/HashRouter.h>
#include <xrpld/app/misc/Transaction.h>
#include <xrpld/app/misc/TxQ.h>
#include <xrpld/app/tx/apply.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/DeliveredAmount.h>
#include <xrpld/rpc/GRPCHandlers.h>
#include <xrpld/rpc/MPTokenIssuanceID.h>
#include <xrpld/rpc/detail/TransactionSign.h>

#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/NFTSyntheticSerializer.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/STParsedJSON.h>
#include <xrpl/resource/Fees.h>

namespace ripple {

static Expected<std::uint32_t, Json::Value>
getAutofillSequence(Json::Value const& tx_json, RPC::JsonContext& context)
{
    // autofill Sequence
    bool const hasTicketSeq = tx_json.isMember(sfTicketSequence.jsonName);
    auto const& accountStr = tx_json[jss::Account];
    if (!accountStr.isString())
    {
        // sanity check, should fail earlier
        // LCOV_EXCL_START
        return Unexpected(RPC::invalid_field_error("tx.Account"));
        // LCOV_EXCL_STOP
    }
    auto const srcAddressID = parseBase58<AccountID>(accountStr.asString());
    if (!srcAddressID.has_value())
    {
        return Unexpected(RPC::make_error(
            rpcSRC_ACT_MALFORMED, RPC::invalid_field_message("tx.Account")));
    }
    std::shared_ptr<SLE const> const sle =
        context.app.openLedger().current()->read(
            keylet::account(*srcAddressID));
    if (!hasTicketSeq && !sle)
    {
        JLOG(context.app.journal("Simulate").debug())
            << "Failed to find source account "
            << "in current ledger: " << toBase58(*srcAddressID);

        return Unexpected(rpcError(rpcSRC_ACT_NOT_FOUND));
    }

    return hasTicketSeq ? 0 : context.app.getTxQ().nextQueuableSeq(sle).value();
}

static std::optional<Json::Value>
autofillSignature(Json::Value& sigObject)
{
    if (!sigObject.isMember(jss::SigningPubKey))
    {
        // autofill SigningPubKey
        sigObject[jss::SigningPubKey] = "";
    }

    if (sigObject.isMember(jss::Signers))
    {
        if (!sigObject[jss::Signers].isArray())
            return RPC::invalid_field_error("tx.Signers");
        // check multisigned signers
        for (unsigned index = 0; index < sigObject[jss::Signers].size();
             index++)
        {
            auto& signer = sigObject[jss::Signers][index];
            if (!signer.isObject() || !signer.isMember(jss::Signer) ||
                !signer[jss::Signer].isObject())
                return RPC::invalid_field_error(
                    "tx.Signers[" + std::to_string(index) + "]");

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
                return rpcError(rpcTX_SIGNED);
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
        return rpcError(rpcTX_SIGNED);
    }
    return std::nullopt;
}

static std::optional<Json::Value>
autofillTx(Json::Value& tx_json, RPC::JsonContext& context)
{
    if (!tx_json.isMember(jss::Fee))
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
            tx_json);
        if (feeOrError.isMember(jss::error))
            return feeOrError;
        tx_json[jss::Fee] = feeOrError;
    }

    if (auto error = autofillSignature(tx_json))
        return *error;

    if (!tx_json.isMember(jss::Sequence))
    {
        auto const seq = getAutofillSequence(tx_json, context);
        if (!seq)
            return seq.error();
        tx_json[sfSequence.jsonName] = *seq;
    }

    if (!tx_json.isMember(jss::NetworkID))
    {
        auto const networkId = context.app.config().NETWORK_ID;
        if (networkId > 1024)
            tx_json[jss::NetworkID] = to_string(networkId);
    }

    return std::nullopt;
}

static Json::Value
getTxJsonFromParams(Json::Value const& params)
{
    Json::Value tx_json;

    if (params.isMember(jss::tx_blob))
    {
        if (params.isMember(jss::tx_json))
        {
            return RPC::make_param_error(
                "Can only include one of `tx_blob` and `tx_json`.");
        }

        auto const tx_blob = params[jss::tx_blob];
        if (!tx_blob.isString())
        {
            return RPC::invalid_field_error(jss::tx_blob);
        }

        auto unHexed = strUnHex(tx_blob.asString());
        if (!unHexed || unHexed->empty())
            return RPC::invalid_field_error(jss::tx_blob);

        try
        {
            SerialIter sitTrans(makeSlice(*unHexed));
            tx_json = STObject(std::ref(sitTrans), sfGeneric)
                          .getJson(JsonOptions::none);
        }
        catch (std::runtime_error const&)
        {
            return RPC::invalid_field_error(jss::tx_blob);
        }
    }
    else if (params.isMember(jss::tx_json))
    {
        tx_json = params[jss::tx_json];
        if (!tx_json.isObject())
        {
            return RPC::object_field_error(jss::tx_json);
        }
    }
    else
    {
        return RPC::make_param_error(
            "Neither `tx_blob` nor `tx_json` included.");
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

    return tx_json;
}

static Json::Value
simulateTxn(RPC::JsonContext& context, std::shared_ptr<Transaction> transaction)
{
    Json::Value jvResult;
    // Process the transaction
    OpenView view = *context.app.openLedger().current();
    auto const result = context.app.getTxQ().apply(
        context.app,
        view,
        transaction->getSTransaction(),
        tapDRY_RUN,
        context.j);

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
        jvResult[jss::engine_result_message] =
            "The simulated transaction would have been applied.";
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
            jvResult[jss::meta] = result.metadata->getJson(JsonOptions::none);
            RPC::insertDeliveredAmount(
                jvResult[jss::meta],
                view,
                transaction->getSTransaction(),
                *result.metadata);
            RPC::insertNFTSyntheticInJson(
                jvResult, transaction->getSTransaction(), *result.metadata);
            RPC::insertMPTokenIssuanceID(
                jvResult[jss::meta],
                transaction->getSTransaction(),
                *result.metadata);
        }
    }

    if (isBinaryOutput)
    {
        auto const txBlob =
            transaction->getSTransaction()->getSerializer().getData();
        jvResult[jss::tx_blob] = strHex(makeSlice(txBlob));
    }
    else
    {
        jvResult[jss::tx_json] = transaction->getJson(JsonOptions::none);
    }

    return jvResult;
}

// {
//   tx_blob: <string> XOR tx_json: <object>,
//   binary: <bool>
// }
Json::Value
doSimulate(RPC::JsonContext& context)
{
    context.loadType = Resource::feeMediumBurdenRPC;

    Json::Value tx_json;  // the tx as a JSON

    // check validity of `binary` param
    if (context.params.isMember(jss::binary) &&
        !context.params[jss::binary].isBool())
    {
        return RPC::invalid_field_error(jss::binary);
    }

    for (auto const field :
         {jss::secret, jss::seed, jss::seed_hex, jss::passphrase})
    {
        if (context.params.isMember(field))
        {
            return RPC::invalid_field_error(field);
        }
    }

    // get JSON equivalent of transaction
    tx_json = getTxJsonFromParams(context.params);
    if (tx_json.isMember(jss::error))
        return tx_json;

    // autofill fields if they're not included (e.g. `Fee`, `Sequence`)
    if (auto error = autofillTx(tx_json, context))
        return *error;

    STParsedJSONObject parsed(std::string(jss::tx_json), tx_json);
    if (!parsed.object.has_value())
        return parsed.error;

    std::shared_ptr<STTx const> stTx;
    try
    {
        stTx = std::make_shared<STTx>(std::move(parsed.object.value()));
    }
    catch (std::exception& e)
    {
        Json::Value jvResult = Json::objectValue;
        jvResult[jss::error] = "invalidTransaction";
        jvResult[jss::error_exception] = e.what();
        return jvResult;
    }

    if (stTx->getTxnType() == ttBATCH)
    {
        return RPC::make_error(rpcNOT_IMPL);
    }

    std::string reason;
    auto transaction = std::make_shared<Transaction>(stTx, reason, context.app);
    // Actually run the transaction through the transaction processor
    try
    {
        return simulateTxn(context, transaction);
    }
    // LCOV_EXCL_START this is just in case, so rippled doesn't crash
    catch (std::exception const& e)
    {
        Json::Value jvResult = Json::objectValue;
        jvResult[jss::error] = "internalSimulate";
        jvResult[jss::error_exception] = e.what();
        return jvResult;
    }
    // LCOV_EXCL_STOP
}

}  // namespace ripple
