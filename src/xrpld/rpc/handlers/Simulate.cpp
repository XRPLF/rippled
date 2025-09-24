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
#include <xrpld/app/ledger/TransactionMaster.h>
#include <xrpld/app/misc/HashRouter.h>
#include <xrpld/app/misc/Transaction.h>
#include <xrpld/app/tx/apply.h>
#include <xrpld/rpc/CTID.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/DeliveredAmount.h>
#include <xrpld/rpc/GRPCHandlers.h>
#include <xrpld/rpc/MPTokenIssuanceID.h>
#include <xrpld/rpc/detail/RPCHelpers.h>
#include <xrpld/rpc/detail/TransactionSign.h>

#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/NFTSyntheticSerializer.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/STParsedJSON.h>
#include <xrpl/resource/Fees.h>

namespace ripple {

constexpr int const MAX_SIMULATE_TXS = 1000;

static Expected<std::uint32_t, Json::Value>
getAutofillSequence(
    Json::Value const& tx_json,
    RPC::JsonContext& context,
    std::shared_ptr<ReadView const> lpLedger,
    bool const isCurrentLedger)
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
        lpLedger->read(keylet::account(*srcAddressID));
    if (hasTicketSeq)
    {
        return 0;
    }
    if (!sle)
    {
        JLOG(context.app.journal("Simulate").debug())
            << "Failed to find source account "
            << "in current ledger: " << toBase58(*srcAddressID);

        return Unexpected(rpcError(rpcSRC_ACT_NOT_FOUND));
    }
    if (!isCurrentLedger)
        return sle->getFieldU32(sfSequence);

    return context.app.getTxQ().nextQueuableSeq(sle).value();
}

static std::optional<Json::Value>
autofillTx(
    Json::Value& tx_json,
    RPC::JsonContext& context,
    std::shared_ptr<ReadView const> lpLedger,
    bool const isCurrentLedger)
{
    if (!tx_json.isMember(jss::Fee))
    {
        // autofill Fee
        // Must happen after all the other autofills happen
        // Error handling/messaging works better that way
        if (isCurrentLedger)
        {
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
        else
        {
            // can't calculate server load for a past ledger
            tx_json[jss::Fee] = lpLedger->fees().base.jsonClipped();
        }
    }

    if (!tx_json.isMember(jss::SigningPubKey))
    {
        // autofill SigningPubKey
        tx_json[jss::SigningPubKey] = "";
    }

    if (tx_json.isMember(jss::Signers))
    {
        if (!tx_json[jss::Signers].isArray())
            return RPC::invalid_field_error("tx.Signers");
        // check multisigned signers
        for (unsigned index = 0; index < tx_json[jss::Signers].size(); index++)
        {
            auto& signer = tx_json[jss::Signers][index];
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

    if (tx_json.isMember(jss::TxnSignature))
    {
        if (tx_json[jss::TxnSignature] != "")
        {
            // Transaction must not be signed
            return rpcError(rpcTX_SIGNED);
        }
        else
        {
            tx_json.removeMember(jss::TxnSignature);
        }
    }

    if (!tx_json.isMember(jss::Sequence))
    {
        auto const seq =
            getAutofillSequence(tx_json, context, lpLedger, isCurrentLedger);
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
getTxJsonFromHistory(RPC::JsonContext& context, bool const isCurrentLedger)
{
    auto const params = context.params;
    uint256 hash;
    if (params.isMember(jss::tx_hash))
    {
        auto const tx_hash = params[jss::tx_hash];
        if (!tx_hash.isString())
        {
            return RPC::invalid_field_error(jss::tx_hash);
        }
        if (isCurrentLedger)
        {
            return RPC::make_param_error(
                "Cannot use `tx_hash` without `ledger_index` or "
                "`ledger_hash`.");
        }
        if (!hash.parseHex(context.params[jss::tx_hash].asString()))
            return RPC::invalid_field_error(jss::tx_hash);
    }
    else if (params.isMember(jss::ctid))
    {
        auto const ctid = params[jss::ctid];
        if (!ctid.isString())
        {
            return RPC::invalid_field_error(jss::ctid);
        }
        if (isCurrentLedger)
        {
            return RPC::make_param_error(
                "Cannot use `ctid` without `ledger_index` or `ledger_hash`.");
        }
        auto decodedCTID =
            RPC::decodeCTID(context.params[jss::ctid].asString());
        if (!decodedCTID)
        {
            return RPC::invalid_field_error(jss::ctid);
        }
        auto const [ledgerSq, txId, _] = *decodedCTID;
        if (auto const optHash =
                context.app.getLedgerMaster().txnIdFromIndex(ledgerSq, txId);
            optHash)
        {
            hash = *optHash;
        }
        else
        {
            return RPC::make_error(rpcTXN_NOT_FOUND);
        }
    }
    using TxPair =
        std::pair<std::shared_ptr<Transaction>, std::shared_ptr<TxMeta>>;
    auto ec{rpcSUCCESS};
    std::variant<TxPair, TxSearched> v =
        context.app.getMasterTransaction().fetch(hash, ec);
    if (std::get_if<TxSearched>(&v))
    {
        return RPC::make_error(rpcTXN_NOT_FOUND);
    }

    auto [txn, _meta] = std::get<TxPair>(v);
    Json::Value tx_json = txn->getJson(JsonOptions::none);
    for (auto const field :
         {jss::SigningPubKey,
          jss::TxnSignature,
          jss::ctid,
          jss::hash,
          jss::inLedger,
          jss::ledger_index})
    {
        if (tx_json.isMember(field))
            tx_json.removeMember(field);
    }
    if (tx_json.isMember(jss::Signers))
    {
        for (auto& signer : tx_json[jss::Signers])
        {
            signer[jss::Signer].removeMember(jss::TxnSignature);
        }
    }
    return tx_json;
}

static Json::Value
getTxJsonFromParams(
    RPC::JsonContext& context,
    Json::Value txInput,
    bool const isCurrentLedger)
{
    Json::Value tx_json;

    if (txInput.isMember(jss::tx_blob))
    {
        auto const tx_blob = txInput[jss::tx_blob];
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
    else if (txInput.isMember(jss::tx_json))
    {
        tx_json = txInput[jss::tx_json];
        if (!tx_json.isObject())
        {
            return RPC::object_field_error(jss::tx_json);
        }
    }
    else
    {
        auto const result = getTxJsonFromHistory(context, isCurrentLedger);
        if (result.isMember(jss::error))
        {
            return result;
        }
        else
        {
            tx_json = result;
        }
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
processResult(
    ApplyResult const& result,
    // easier to type as this due to sfRawTransactions in batch transactions
    STObject const& transaction,
    bool const isBinaryOutput,
    LedgerIndex const seq)
{
    Json::Value jvResult = Json::objectValue;
    jvResult[jss::applied] = result.applied;
    jvResult[jss::ledger_index] = seq;

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
        auto const txBlob = transaction.getSerializer().getData();
        jvResult[jss::tx_blob] = strHex(makeSlice(txBlob));
    }
    else
    {
        jvResult[jss::tx_json] = transaction.getJson(JsonOptions::none);
    }
    return jvResult;
}

static Json::Value
simulateTxn(
    RPC::JsonContext& context,
    std::vector<std::shared_ptr<Transaction>> transactions,
    std::shared_ptr<ReadView const> lpLedger,
    bool const isCurrentLedger)
{
    Json::Value jvTransactions = Json::arrayValue;
    std::vector<ApplyResult> results;
    bool const isBinaryOutput = context.params.get(jss::binary, false).asBool();

    OpenView origView = OpenView(&*lpLedger);
    OpenView view(batch_view, origView);
    LedgerIndex const seq = view.seq();
    for (auto const& transaction : transactions)
    {
        OpenView perTxView(batch_view, view);
        auto const txn = *transaction->getSTransaction();
        /***************************************
         * SECURITY NOTE: This technically applies the transaction to the view.
         * However, since the `view` is a copy of the ledger it's being applied
         * to, and is thrown away immediately after the simulated transactions
         * are processed, the original ledger remains unchanged. Therefore, you
         * cannot use `simulate` to bypass signature checks and submit
         * transactions/modify the current ledger directly.
         ***************************************/
        auto const result =
            apply(context.app, perTxView, txn, tapDRY_RUN, context.j);
        if (isTesSuccess(result.ter) || isTecClaim(result.ter))
            perTxView.apply(view);
        jvTransactions.append(processResult(result, txn, isBinaryOutput, seq));

        if (isTesSuccess(result.ter) && txn.getTxnType() == ttBATCH)
        {
            OpenView wholeBatchView(batch_view, view);

            if (auto const batchResults = applyBatchTransactions(
                    context.app, wholeBatchView, txn, tapDRY_RUN, context.j);
                batchResults)
            {
                for (int i = 0; i < batchResults->size(); ++i)
                {
                    auto const& innerResult = (*batchResults)[i];
                    jvTransactions.append(processResult(
                        innerResult,
                        txn.getFieldArray(sfRawTransactions)[i],
                        isBinaryOutput,
                        seq));
                }
                wholeBatchView.apply(view);
            }
        }
    }

    if (jvTransactions.size() == 1)
    {
        return jvTransactions[0u];
    }
    Json::Value jvFinalResult = Json::objectValue;
    jvFinalResult[jss::transactions] = jvTransactions;
    return jvFinalResult;
}

bool
checkIsCurrentLedger(Json::Value const params)
{
    if (params.isMember(jss::ledger_index))
    {
        auto const& ledgerIndex = params[jss::ledger_index];
        if (!ledgerIndex.isNull())
        {
            return ledgerIndex == RPC::LedgerShortcut::CURRENT;
        }
    }
    if (params.isMember(jss::ledger_hash))
    {
        if (!params[jss::ledger_hash].isNull())
            return false;
    }
    return true;
}

Expected<std::shared_ptr<Transaction>, Json::Value>
processTransaction(
    RPC::JsonContext& context,
    Json::Value txInput,
    std::shared_ptr<ReadView const> lpLedger,
    bool const isCurrentLedger)
{
    // get JSON equivalent of transaction
    Json::Value tx_json =
        getTxJsonFromParams(context, txInput, isCurrentLedger);
    if (tx_json.isMember(jss::error))
        return Unexpected(tx_json);

    // autofill fields if they're not included (e.g. `Fee`, `Sequence`)
    if (auto error = autofillTx(tx_json, context, lpLedger, isCurrentLedger))
        return Unexpected(*error);

    STParsedJSONObject parsed(std::string(jss::tx_json), tx_json);
    if (!parsed.object.has_value())
        return Unexpected(parsed.error);

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
        return Unexpected(jvResult);
    }

    std::string reason;
    return std::make_shared<Transaction>(stTx, reason, context.app);
}

// {
//   tx_blob: <string> XOR tx_json: <object> XOR tx_hash: <string> XOR ctid:
//   <string> XOR
//       transactions: [ <object> ]
//         (each <object contains> one of tx_json/tx_blob/tx_hash/ctid),
//   binary: <bool>
// }
Json::Value
doSimulate(RPC::JsonContext& context)
{
    context.loadType = Resource::feeMediumBurdenRPC;

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

    auto const numParams =
        (context.params.isMember(jss::transactions) +
         context.params.isMember(jss::tx_json) +
         context.params.isMember(jss::tx_blob) +
         context.params.isMember(jss::tx_hash) +
         context.params.isMember(jss::ctid));
    if (numParams == 0)
    {
        return RPC::make_param_error(
            "Must include one of 'transactions', 'tx_json', 'tx_blob', "
            "'tx_hash', or 'ctid'.");
    }
    // if more than one of these fields is included, error out
    if (numParams > 1)
    {
        return RPC::make_param_error(
            "Cannot include more than one of 'transactions', 'tx_json', "
            "'tx_blob', "
            "'tx_hash', and 'ctid'.");
    }

    std::shared_ptr<ReadView const> lpLedger;
    auto jvResult = RPC::lookupLedger(lpLedger, context);
    if (!lpLedger)
        return jvResult;
    bool const isCurrentLedger = checkIsCurrentLedger(context.params);

    auto transactions = std::vector<std::shared_ptr<Transaction>>{};
    if (context.params.isMember(jss::transactions))
    {
        auto const txs = context.params[jss::transactions];
        if (!txs.isArray())
        {
            return RPC::expected_field_error(jss::transactions, "array");
        }
        if (txs.size() == 0)
        {
            return RPC::expected_field_error(
                jss::transactions, "nonempty array");
        }
        if (txs.size() > MAX_SIMULATE_TXS)
        {
            return RPC::make_param_error(
                "Cannot include more than " + std::to_string(MAX_SIMULATE_TXS) +
                " transactions in 'transactions' array.");
        }
        for (auto const& txInput : context.params[jss::transactions])
        {
            if (!txInput.isObject())
            {
                return RPC::expected_field_error(
                    jss::transactions, "array of objects");
            }
            {
                auto const numParams = txInput.isMember(jss::tx_json) +
                    txInput.isMember(jss::tx_blob) +
                    txInput.isMember(jss::tx_hash) +
                    txInput.isMember(jss::ctid);
                if (numParams == 0)
                {
                    return RPC::make_param_error(
                        "Must include one of 'tx_json', 'tx_blob', "
                        "'tx_hash', or 'ctid' in each transaction.");
                }
                else if (numParams > 1)
                {
                    return RPC::make_param_error(
                        "Cannot include more than one of 'tx_json', 'tx_blob', "
                        "'tx_hash', and 'ctid' in each transaction.");
                }
            }

            auto transaction =
                processTransaction(context, txInput, lpLedger, isCurrentLedger);
            if (!transaction)
                return transaction.error();
            transactions.push_back(transaction.value());
        }
    }
    else
    {
        // single transaction
        auto transaction = processTransaction(
            context, context.params, lpLedger, isCurrentLedger);
        if (!transaction)
            return transaction.error();
        transactions.push_back(transaction.value());
    }
    // Actually run the transaction through the transaction processor
    try
    {
        return simulateTxn(context, transactions, lpLedger, isCurrentLedger);
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
