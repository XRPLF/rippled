/** @file
 *  Implements the `simulate` RPC command for dry-run transaction execution.
 *
 *  A client can test a transaction against the current ledger state and
 *  receive realistic metadata and a TER result code without broadcasting
 *  to the network, spending XRP, or persisting any state change. The
 *  mechanism is `TapDryRun`: after the full transactor execution path
 *  (including metadata generation), the snapshot of the open ledger is
 *  discarded rather than committed.
 *
 *  @see doSimulate
 */

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

/** Derive the sequence number to use when autofilling a simulated transaction.
 *
 *  Queries `TxQ::nextQueuableSeq()` against the live open ledger to obtain a
 *  sequence number that is consistent with the account's current state. When
 *  the transaction carries a `TicketSequence`, the wire-format rule requires
 *  `Sequence = 0`, so that value is returned directly without a ledger lookup.
 *
 *  @param txJson  The transaction JSON object; must contain a valid `Account`
 *      field.
 *  @param context The RPC dispatch context providing access to the open ledger
 *      and transaction queue.
 *  @return The sequence number to insert, or an error JSON value if the source
 *      account does not exist in the current ledger and no `TicketSequence` is
 *      present, or if `Account` is malformed.
 *  @note The account-not-found branch returns `rpcSRC_ACT_NOT_FOUND`, not a
 *      fatal error — callers that supply `TicketSequence` bypass this check
 *      entirely.
 */
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

/** Autofill signature-related fields on a transaction or signer object.
 *
 *  Sets `SigningPubKey` and `TxnSignature` to empty strings when absent —
 *  the canonical XRPL representation of an unsigned transaction. The
 *  `TapDryRun` flag tells `Transactor::checkSign` to skip validation when
 *  these fields are empty.
 *
 *  If the caller has pre-populated `TxnSignature` with a non-empty value
 *  (on the top-level transaction or on any element of the `Signers` array),
 *  the function returns `rpcTX_SIGNED` immediately. Simulate must not silently
 *  execute an already-signed transaction, as that would mislead the caller
 *  into believing signature validation passed.
 *
 *  @param sigObject  The transaction JSON object (or a `Signer` sub-object for
 *      multi-sign) to inspect and mutate.
 *  @return An error JSON value if the object is already signed or structurally
 *      invalid; `std::nullopt` on success.
 *  @note `Transactor.cpp` contains a defensive comment: "This code should
 *      never be hit because it's checked in the `simulate` RPC" — this
 *      function is the authoritative first line of defence.
 */
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

/** Synthesize mandatory transaction fields that the caller omitted.
 *
 *  Handles `Fee`, `Sequence`, `NetworkID`, and the signature fields in one
 *  pass. Each field is only written when absent — explicit caller values are
 *  preserved unchanged.
 *
 *  Fee is computed last because `RPC::getCurrentNetworkFee()` may depend on
 *  other fields (e.g. transaction type) already being set. Sequence is filled
 *  via `getAutofillSequence()`, which queries the live open ledger.
 *  `NetworkID` is injected only when the network ID exceeds 1024, per the
 *  XRPL protocol rule that mainnet and other low-numbered networks omit the
 *  field.
 *
 *  @param txJson   The transaction JSON object to mutate in place.
 *  @param context  RPC dispatch context providing access to fee-track, TxQ,
 *      and the network-ID service.
 *  @return An error JSON value if any autofill step fails (e.g. account not
 *      found, transaction already signed); `std::nullopt` on success.
 */
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

/** Extract and normalize the transaction from the RPC request parameters.
 *
 *  Accepts either `tx_blob` (hex-encoded binary) or `tx_json` (structured
 *  object), but not both. When `tx_blob` is supplied it is hex-decoded and
 *  deserialized through `SerialIter` into an `STObject`, then immediately
 *  re-serialized to JSON — this round-trip through canonical binary format
 *  normalizes the input before downstream processing.
 *
 *  @param params  The top-level RPC request parameters object.
 *  @return The extracted transaction as a JSON object, or a JSON error object
 *      (containing an `error` key) if both or neither input key is present,
 *      if `tx_blob` cannot be decoded or deserialized, or if the resulting
 *      object is missing `TransactionType` or `Account`.
 *  @note Mutual exclusion between `tx_blob` and `tx_json` is checked first
 *      and is fatal — the caller must not supply both.
 */
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
            txJson = STObject(std::ref(sitTrans), kSF_GENERIC).getJson(JsonOptions::Values::None);
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

/** Execute a dry-run simulation of a transaction and assemble the response.
 *
 *  Copies the current `OpenView` by value, then invokes `TxQ::apply()` with
 *  `TapDryRun`. The flag causes the full transactor execution path — including
 *  metadata generation — to run, but forces `applied = false` at the end of
 *  `Transactor::apply()` so the ledger snapshot is never committed. The copy
 *  is therefore discarded without side effects.
 *
 *  The response always contains `applied`, `ledger_index`, `engine_result`,
 *  `engine_result_code`, and `engine_result_message`. For `tesSUCCESS` the
 *  message is overridden to "The simulated transaction would have been
 *  applied." to prevent the generic success string from implying the
 *  transaction was committed. When metadata is available it is serialized as
 *  `meta_blob` (hex) or `meta` (JSON) according to the `binary` parameter;
 *  JSON metadata receives the same three enrichment calls used in the live
 *  transaction pipeline (`insertDeliveredAmount`, `insertNFTSyntheticInJson`,
 *  `insertMPTokenIssuanceID`). The autofilled transaction is echoed back as
 *  `tx_blob` or `tx_json`.
 *
 *  @param context      RPC dispatch context providing access to the open
 *      ledger and transaction queue.
 *  @param transaction  The fully constructed and autofilled transaction to
 *      simulate.
 *  @return A JSON object containing the simulation result.
 */
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

/** Handler for the `simulate` RPC command.
 *
 *  Validates the request, autofills any absent mandatory fields, and runs the
 *  transaction through the full engine execution path with `TapDryRun` so no
 *  state is committed. The caller receives a realistic TER result code and
 *  complete transaction metadata without broadcasting to the network or
 *  spending XRP.
 *
 *  Accepted parameters:
 *  - `tx_blob` **XOR** `tx_json` — the transaction to simulate.
 *  - `binary` (bool, optional) — if true, metadata and transaction are
 *    returned as hex blobs (`meta_blob`, `tx_blob`) instead of JSON objects.
 *
 *  Credential fields (`secret`, `seed`, `seed_hex`, `passphrase`) are
 *  explicitly rejected before any other processing — simulate is not a signing
 *  endpoint and must not be a channel for key material.
 *
 *  `ttBATCH` transactions are rejected with `rpcNOT_IMPL`: batch execution
 *  semantics cannot be faithfully replicated by the single-transaction
 *  dry-run path.
 *
 *  @param context  RPC dispatch context for this call.
 *  @return JSON object containing `applied`, `ledger_index`, `engine_result*`,
 *      metadata (if generated), and the autofilled transaction. Returns a JSON
 *      error object on invalid input or if the transaction cannot be
 *      constructed.
 *  @note Resource cost is `feeMediumBurdenRPC` — a full engine execution is
 *      more expensive than a read-only query, though cheaper than a real
 *      submission that triggers peer propagation and queue management.
 *  @note The outer `try/catch` around `simulateTxn()` is marked `LCOV_EXCL`
 *      and is not expected to fire under normal conditions; it exists solely
 *      to prevent an unexpected exception from crashing the server.
 */
// {
//   tx_blob: <string> XOR tx_json: <object>,
//   binary: <bool>
// }
json::Value
doSimulate(RPC::JsonContext& context)
{
    context.loadType = Resource::kFEE_MEDIUM_BURDEN_RPC;

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
