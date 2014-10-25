//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2014 Ripple Labs Inc.

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

#include <ripple/rpc/impl/TransactionSign.h>
#include <ripple/protocol/TxFlags.h>
#include <ripple/basics/StringUtilities.h>
#include <beast/unit_test.h>

namespace ripple {

//------------------------------------------------------------------------------


namespace RPC {
namespace RPCDetail {

// A local class used to pass extra parameters used when returning a
// a SigningAccount object.
class SigningAccountParams
{
private:
    RippleAddress const* const raMultiSignAddressID_;
    RippleAddress const* const raMultiSignPublicKey_;
public:
    explicit SigningAccountParams ()
    : raMultiSignAddressID_ (nullptr)
    , raMultiSignPublicKey_ (nullptr)
    { }

    SigningAccountParams (
        RippleAddress const& multiSignAddressID,
        RippleAddress const& multiSignPublicKey)
    : raMultiSignAddressID_ (&multiSignAddressID)
    , raMultiSignPublicKey_ (&multiSignPublicKey)
    { }

    bool isMultiSigning () const
    {
        return ((raMultiSignAddressID_ != nullptr) &&
                (raMultiSignPublicKey_ != nullptr));
    }

    RippleAddress const* getAddressID () const
    {
        return raMultiSignAddressID_;
    }

    RippleAddress const* getPublicKey () const
    {
        return raMultiSignPublicKey_;
    }
};

/** Fill in the fee on behalf of the client.
    This is called when the client does not explicitly specify the fee.
    The client may also put a ceiling on the amount of the fee. This ceiling
    is expressed as a multiplier based on the current ledger's fee schedule.

    JSON fields

    "Fee"   The fee paid by the transaction. Omitted when the client
            wants the fee filled in.

    "fee_mult_max"  A multiplier applied to the current ledger's transaction
                    fee that caps the maximum the fee server should auto fill.
                    If this optional field is not specified, then a default
                    multiplier is used.

    @param tx       The JSON corresponding to the transaction to fill in
    @param ledger   A ledger for retrieving the current fee schedule
    @param result   A JSON object for injecting error results, if any
    @param admin    `true` if this is called by an administrative endpoint.
*/
static void autofill_fee (
    Json::Value& request,
    Ledger::pointer ledger,
    Json::Value& result,
    bool admin)
{
    Json::Value& tx (request["tx_json"]);
    if (tx.isMember ("Fee"))
        return;

    int mult = Tuning::defaultAutoFillFeeMultiplier;
    if (request.isMember ("fee_mult_max"))
    {
        if (request["fee_mult_max"].isNumeric ())
        {
            mult = request["fee_mult_max"].asInt();
        }
        else
        {
            RPC::inject_error (rpcHIGH_FEE, RPC::expected_field_message (
                "fee_mult_max", "a number"), result);
            return;
        }
    }

    // Default fee in fee units
    std::uint64_t const feeDefault = getConfig().TRANSACTION_FEE_BASE;

    // Administrative endpoints are exempt from local fees
    std::uint64_t const fee = ledger->scaleFeeLoad (feeDefault, admin);
    std::uint64_t const limit = mult * ledger->scaleFeeBase (feeDefault);

    if (fee > limit)
    {
        std::stringstream ss;
        ss <<
            "Fee of " << fee <<
            " exceeds the requested tx limit of " << limit;
        RPC::inject_error (rpcHIGH_FEE, ss.str(), result);
        return;
    }

    tx ["Fee"] = static_cast<int>(fee);
}

static Json::Value signPayment(
    Json::Value const& params,
    Json::Value& tx_json,
    RippleAddress const& raSrcAddressID,
    Ledger::pointer lSnapshot,
    Role role)
{
    RippleAddress dstAccountID;

    if (!tx_json.isMember ("Amount"))
        return RPC::missing_field_error ("tx_json.Amount");

    STAmount amount;

    if (! amountFromJsonNoThrow (amount, tx_json ["Amount"]))
        return RPC::invalid_field_error ("tx_json.Amount");

    if (!tx_json.isMember ("Destination"))
        return RPC::missing_field_error ("tx_json.Destination");

    if (!dstAccountID.setAccountID (tx_json["Destination"].asString ()))
        return RPC::invalid_field_error ("tx_json.Destination");

    if (tx_json.isMember ("Paths") && params.isMember ("build_path"))
        return RPC::make_error (rpcINVALID_PARAMS,
            "Cannot specify both 'tx_json.Paths' and 'tx_json.build_path'");

    if (!tx_json.isMember ("Paths")
        && tx_json.isMember ("Amount")
        && params.isMember ("build_path"))
    {
        // Need a ripple path.
        STPathSet   spsPaths;
        Currency uSrcCurrencyID;
        Account uSrcIssuerID;

        STAmount    saSendMax;

        if (tx_json.isMember ("SendMax"))
        {
            if (! amountFromJsonNoThrow (saSendMax, tx_json ["SendMax"]))
                return RPC::invalid_field_error ("tx_json.SendMax");
        }
        else
        {
            // If no SendMax, default to Amount with sender as issuer.
            saSendMax = amount;
            saSendMax.setIssuer (raSrcAddressID.getAccountID ());
        }

        if (saSendMax.isNative () && amount.isNative ())
            return RPC::make_error (rpcINVALID_PARAMS,
                "Cannot build XRP to XRP paths.");

        {
            LegacyPathFind lpf (role == Role::ADMIN);
            if (!lpf.isOk ())
                return rpcError (rpcTOO_BUSY);

            auto cache = std::make_shared<RippleLineCache> (lSnapshot);
            STPathSet spsPaths;
            STPath fullLiquidityPath;
            auto valid = findPathsForOneIssuer (
                cache,
                raSrcAddressID.getAccountID(),
                dstAccountID.getAccountID(),
                saSendMax.issue (),
                amount,
                getConfig ().PATH_SEARCH_OLD,
                4,  // iMaxPaths
                spsPaths,
                fullLiquidityPath);

            if (!valid)
            {
                WriteLog (lsDEBUG, RPCHandler)
                        << "transactionSign: build_path: No paths found.";
                return rpcError (rpcNO_PATH);
            }
            WriteLog (lsDEBUG, RPCHandler)
                    << "transactionSign: build_path: "
                    << spsPaths.getJson (0);

            if (!spsPaths.empty ())
                tx_json["Paths"] = spsPaths.getJson (0);
        }
    }
    return Json::Value();
}

static Json::Value transactionSubmitImpl (
    SerializedTransaction::pointer stpTrans,
    NetworkOPs::SubmitTxn submit,
    NetworkOPs::FailHard failType,
    NetworkOPs& netOps,
    Role role)
{
    Transaction::pointer tpTrans;
    try
    {
        tpTrans = std::make_shared<Transaction> (stpTrans, Validate::NO);
    }
    catch (std::exception&)
    {
        return RPC::make_error (rpcINTERNAL,
            "Exception occurred during transaction");
    }

    try
    {
        // FIXME: For performance, should use asynch interface
        tpTrans = netOps.submitTransactionSync (tpTrans,
            role == Role::ADMIN, true, failType, submit);

        if (!tpTrans)
        {
            return RPC::make_error (rpcINTERNAL,
                "Unable to sterilize transaction.");
        }
    }
    catch (std::exception&)
    {
        return RPC::make_error (rpcINTERNAL,
            "Exception occurred during transaction submission.");
    }

    try
    {
        Json::Value jvResult;
        jvResult["tx_json"] = tpTrans->getJson (0);
        jvResult["tx_blob"] = strHex (
            tpTrans->getSTransaction ()->getSerializer ().peekData ());

        if (temUNCERTAIN != tpTrans->getResult ())
        {
            std::string sToken;
            std::string sHuman;

            transResultInfo (tpTrans->getResult (), sToken, sHuman);

            jvResult["engine_result"]           = sToken;
            jvResult["engine_result_code"]      = tpTrans->getResult ();
            jvResult["engine_result_message"]   = sHuman;
        }

        return jvResult;
    }
    catch (std::exception&)
    {
        return RPC::make_error (rpcINTERNAL,
            "Exception occurred during JSON handling.");
    }
}

//------------------------------------------------------------------------------

// VFALCO TODO This function should take a reference to the params, modify it
//             as needed, and then there should be a separate function to
//             submit the tranaction
//
static Json::Value transactionProcessImpl (
    Json::Value params,
    NetworkOPs::SubmitTxn submit,
    NetworkOPs::FailHard failType,
    NetworkOPs& netOps,
    Role role,
    SigningAccountParams const& signingArgs = SigningAccountParams())
{
    if (! params.isMember ("secret"))
        return RPC::missing_field_error ("secret");

    {
        RippleAddress naSeed;

        if (! naSeed.setSeedGeneric (params["secret"].asString ()))
            return RPC::make_error (rpcBAD_SEED,
                RPC::invalid_field_message ("secret"));
    }

    if (! params.isMember ("tx_json"))
        return RPC::missing_field_error ("tx_json");

    Json::Value& tx_json (params ["tx_json"]);

    if (! tx_json.isObject ())
        return RPC::object_field_error ("tx_json");

    if (! tx_json.isMember ("TransactionType"))
        return RPC::missing_field_error ("tx_json.TransactionType");

    if (! tx_json.isMember ("Account"))
        return RPC::make_error (rpcSRC_ACT_MISSING,
            RPC::missing_field_message ("tx_json.Account"));

    RippleAddress raSrcAddressID;

    if (! raSrcAddressID.setAccountID (tx_json["Account"].asString ()))
        return RPC::make_error (rpcSRC_ACT_MALFORMED,
            RPC::invalid_field_message ("tx_json.Account"));

    bool const verify = !(params.isMember ("offline")
                          && params["offline"].asBool ());

    // This test covers the case where we're offline so the sequence number
    // cannot be determined locally.  If we're offline then the caller must
    // provide the sequence number.
    if (!verify && !tx_json.isMember ("Sequence"))
        return RPC::missing_field_error ("tx_json.Sequence");

    // Check for current ledger
    if (verify && !getConfig ().RUN_STANDALONE &&
        (getApp().getLedgerMaster().getValidatedLedgerAge() > 120))
        return rpcError (rpcNO_CURRENT);

    // Check for load
    if (getApp().getFeeTrack().isLoadedCluster() && (role != Role::ADMIN))
        return rpcError(rpcTOO_BUSY);

    Ledger::pointer lSnapshot = netOps.getCurrentLedger ();
    AccountState::pointer asSrc;
    if (verify) {
        asSrc = netOps.getAccountState (lSnapshot, raSrcAddressID);

        if (!asSrc)
        {
            // If not offline and did not find account, error.
            WriteLog (lsDEBUG, RPCHandler)
                << "transactionSign: Failed to find source account "
                << "in current ledger: "
                << raSrcAddressID.humanAccountID ();

            return rpcError (rpcSRC_ACT_NOT_FOUND);
        }
    }

    Json::Value jvResult;

    if (!signingArgs.isMultiSigning ())
    {
        // Only auto-fill the "Fee" for non-multisign stuff.  Multisign
        // must come in with the fee already in place or the signature
        // will be invalid later.
        autofill_fee (params, lSnapshot, jvResult, role == Role::ADMIN);
        if (RPC::contains_error (jvResult))
            return jvResult;
    }

    auto const& transactionType = tx_json["TransactionType"].asString ();

    if ("Payment" == transactionType)
    {
        auto e = signPayment(
            params,
            tx_json,
            raSrcAddressID,
            lSnapshot,
            role);
        if (contains_error(e))
            return e;
    }

    if (!tx_json.isMember ("Sequence"))
        tx_json["Sequence"] = asSrc->getSeq ();

    if (!tx_json.isMember ("Flags"))
        tx_json["Flags"] = tfFullyCanonicalSig;

    if (verify)
    {
        SLE::pointer sleAccountRoot = netOps.getSLEi (lSnapshot,
            Ledger::getAccountRootIndex (raSrcAddressID.getAccountID ()));

        if (!sleAccountRoot)
            // XXX Ignore transactions for accounts not created.
            return rpcError (rpcSRC_ACT_NOT_FOUND);
    }

    RippleAddress secret = RippleAddress::createSeedGeneric (
        params["secret"].asString ());
    RippleAddress masterGenerator = RippleAddress::createGeneratorPublic (
        secret);
    RippleAddress masterAccountPublic = RippleAddress::createAccountPublic (
        masterGenerator, 0);

    if (verify)
    {
        auto account = masterAccountPublic.getAccountID();
        auto const& sle = asSrc->peekSLE();

        WriteLog (lsWARNING, RPCHandler) <<
                "verify: " << masterAccountPublic.humanAccountID () <<
                " : " << raSrcAddressID.humanAccountID ();

        // There are two possible signature addresses: the multisign address or
        // the source address.  If it's not multisign, then use source address.
        RippleAddress raSignAddressId;
        if (signingArgs.isMultiSigning ())
            raSignAddressId = *signingArgs.getAddressID ();
        else
            raSignAddressId = raSrcAddressID;

        if (raSignAddressId.getAccountID () == account)
        {
            if (sle.isFlag(lsfDisableMaster))
                return rpcError (rpcMASTER_DISABLED);
        }
        else if (!sle.isFieldPresent(sfRegularKey) ||
                 account != sle.getFieldAccount160 (sfRegularKey))
        {
            return rpcError (rpcBAD_SECRET);
        }
    }

    STParsedJSONObject parsed ("tx_json", tx_json);
    if (!parsed.object.get())
    {
        jvResult ["error"] = parsed.error ["error"];
        jvResult ["error_code"] = parsed.error ["error_code"];
        jvResult ["error_message"] = parsed.error ["error_message"];
        return jvResult;
    }

    SerializedTransaction::pointer stpTrans;
    try
    {
        // If we're generating a multi-signature the SigningPubKey must be
        // empty:
        //  o If we're multi-signing, use an empty blob for the signingPubKey
        //  o Otherwise use the master account's public key.
        Blob emptyBlob;
        Blob const& signingPubKey = signingArgs.isMultiSigning () ?
            emptyBlob : masterAccountPublic.getAccountPublic ();

        std::unique_ptr<STObject> sopTrans = std::move (parsed.object);
        sopTrans->setFieldVL (sfSigningPubKey, signingPubKey);

        stpTrans = std::make_shared<SerializedTransaction> (*sopTrans);
    }
    catch (std::exception&)
    {
        return RPC::make_error (rpcINTERNAL,
            "Exception occurred during transaction");
    }

    std::string reason;
    if (!passesLocalChecks (*stpTrans, reason))
        return RPC::make_error (rpcINVALID_PARAMS, reason);

    if (params.isMember ("debug_signing"))
    {
        jvResult["tx_unsigned"] = strHex (
            stpTrans->getSerializer ().peekData ());
        jvResult["tx_signing_hash"] = to_string (stpTrans->getSigningHash ());
    }

    RippleAddress naAccountPrivate = RippleAddress::createAccountPrivate (
        masterGenerator, secret, 0);

    // If multisign, set SignerEntry field, else set TxnSignature field.
    if (signingArgs.isMultiSigning ())
        stpTrans->insertSigningAccount (
            *signingArgs.getAddressID (),
            *signingArgs.getPublicKey (),
            naAccountPrivate);
    else
        stpTrans->sign (naAccountPrivate);

    return transactionSubmitImpl (stpTrans, submit, failType, netOps, role);
}

} // namespace RPCDetail

//------------------------------------------------------------------------------

Json::Value transactionSign (
    Json::Value jvRequest,
    NetworkOPs::FailHard failType,
    NetworkOPs& netOps,
    Role role)
{
    WriteLog (lsDEBUG, RPCHandler) << "transactionSign: " << jvRequest;

    RippleAddress raNotMultisign;           // Default constructs to invalid

    // Get the signature
    return RPCDetail::transactionProcessImpl (
        jvRequest,
        NetworkOPs::SubmitTxn::no,
        failType,
        netOps,
        role);
}

Json::Value transactionSubmit (
    Json::Value jvRequest,
    NetworkOPs::FailHard failType,
    NetworkOPs& netOps,
    Role role)
{
    WriteLog (lsDEBUG, RPCHandler) << "transactionSubmit: " << jvRequest;

    RippleAddress raNotMultisign;           // Default constructs to invalid

    // Submit the transaction
    return RPCDetail::transactionProcessImpl (
        jvRequest,
        NetworkOPs::SubmitTxn::yes,
        failType,
        netOps,
        role);
}

Json::Value transactionGetSigningAccount (
    Json::Value jvRequest,
    NetworkOPs::FailHard failType,
    NetworkOPs& netOps,
    Role role)
{
    WriteLog (lsDEBUG, RPCHandler) <<
        "transactionGetSigningAccount: " << jvRequest;

    // Verify presence of the signer's account field
    const char accountField[] = "account";

    if (! jvRequest.isMember (accountField))
        return RPC::missing_field_error (accountField);

    // Verify presence of the signer's publickey field
    const char publickeyField[] = "publickey";

    if (! jvRequest.isMember (publickeyField))
        return RPC::missing_field_error (publickeyField);

    // Turn the signer's account into a RippleAddress for multisign
    RippleAddress multiSignAddressID;
    if (! multiSignAddressID.setAccountID (
        jvRequest[accountField].asString ()))
    {
        return RPC::make_error (rpcSRC_ACT_MALFORMED,
            RPC::invalid_field_message (accountField));
    }

    // Turn the signer's public key into a RippleAddress for multisign
    RippleAddress multiSignPublicKey;
    if (! multiSignPublicKey.setAccountPublic (
        jvRequest[publickeyField].asString ()))
    {
        return RPC::make_error (rpcSRC_ACT_MALFORMED,
            RPC::invalid_field_message (accountField));
    }

    // Get Multisignature
    using namespace RPCDetail;
    return transactionProcessImpl (
        jvRequest,
        NetworkOPs::SubmitTxn::no,
        failType,
        netOps,
        role,
        SigningAccountParams (multiSignAddressID, multiSignPublicKey));
}

// SSCHURR FIXME: transactionSubmit/Sign *must* be refactored.
//
// In this implementation the transactionProcessImpl() does a bunch of stuff
// in an organic sort of way.  The tests and adjustments in there are
// important, but not well structured.
//
// This function, transactionSubmitMultiSigned(), needs to be making many (but
// not all) of the same adjustments that transactionProcessImpl () does.  But
// it can't call transactionProcessImpl(), since that function does some of
// the wrong stuff.
//
// Note to reviewers: do not allow this code to be merged to develop until
// this refactoring issue is addressed.
Json::Value transactionSubmitMultiSigned (
    Json::Value jvRequest,
    NetworkOPs::FailHard failType,
    NetworkOPs& netOps,
    Role role)
{
    WriteLog (lsDEBUG, RPCHandler)
        << "transactionSubmitMultiSigned: " << jvRequest;

    // Validate tx_json and SigningAccounts before submitting the transaction.

    // We're validating against the serialized transaction.  So start out by
    // serializing tx_json
   if (! jvRequest.isMember ("tx_json"))
        return RPC::missing_field_error ("tx_json");

    Json::Value& tx_json (jvRequest ["tx_json"]);

    if (! tx_json.isObject ())
        return RPC::object_field_error ("tx_json");

    // Grind through the JSON in tx_json to produce a SerializedTransaction
    SerializedTransaction::pointer stpTrans;
    {
        STParsedJSONObject parsedTx_json ("tx_json", tx_json);
        if (!parsedTx_json.object)
        {
            Json::Value jvResult;
            jvResult ["error"] = parsedTx_json.error ["error"];
            jvResult ["error_code"] = parsedTx_json.error ["error_code"];
            jvResult ["error_message"] = parsedTx_json.error ["error_message"];
            return jvResult;
        }

        std::unique_ptr <STObject> sopTrans = std::move (parsedTx_json.object);

        try
        {
            stpTrans = std::make_shared<SerializedTransaction> (*sopTrans);
        }
        catch (std::exception&)
        {
            return RPC::make_error (rpcINTERNAL,
                "Exception while serializing transaction. Are fields missing?");
        }

        {
            std::string reason;
            if (!passesLocalChecks (*stpTrans, reason))
                return RPC::make_error (rpcINVALID_PARAMS, reason);
        }
    }

    // Validate the fields in the serialized transaction.
    {
        // We now have the transaction text serialized and in the right format.
        // Verify the presence and values of select fields.
        // The SigningPubKey must be present but empty.
        if (!stpTrans->isFieldPresent (sfSigningPubKey))
            return RPC::missing_field_error (sfSigningPubKey.getName ());

        if (!stpTrans->getFieldVL (sfSigningPubKey).empty ())
        {
            std::ostringstream err;
            err << "Invalid  " << sfSigningPubKey.fieldName
                << " field.  Field must be empty when multi-signing.";
            return RPC::make_error (rpcINVALID_PARAMS, err.str ());
        }

        // The Sequence field must be present.
        if (!stpTrans->isFieldPresent (sfSequence))
            return RPC::missing_field_error (sfSequence.getName ());

        // The Fee field must be present and greater than zero.
        if (!stpTrans->isFieldPresent (sfFee))
            return RPC::missing_field_error (sfFee.getName ());

        if (!stpTrans->getFieldAmount (sfFee) > 0)
        {
            std::ostringstream err;
            err << "Invalid " << sfFee.fieldName
                << " field.  Value must be greater than zero.";
            return RPC::make_error (rpcINVALID_PARAMS, err.str ());
        }

        // Save the Account for testing against the SigningAccounts.
        if (!stpTrans->isFieldPresent (sfAccount))
            return RPC::missing_field_error (sfAccount.getName ());
    }
    RippleAddress const txnAccount = stpTrans->getFieldAccount (sfAccount);

    // Check SigningAccounts.  For the moment I'm just going to return an
    // error if the signature is invalid.  Later (inside the transaction)
    // we'll need to sum the weights of the signatures and see whether we
    // equal or exceed the quorum.

    const char* signingAccocuntsArrayName {
        sfSigningAccounts.getJsonName ().c_str ()};

    std::unique_ptr <STArray> signingAccounts;
    {
        // Verify that the SigningAccounts field is present and an array.
        if (! jvRequest.isMember (signingAccocuntsArrayName))
            return RPC::missing_field_error (signingAccocuntsArrayName);

        Json::Value& signingAccountsValue (
            jvRequest [signingAccocuntsArrayName]);

        if (! signingAccountsValue.isArray ())
        {
            std::ostringstream err;
                err << "Expected "
                << signingAccocuntsArrayName << " to be an array";
            return RPC::make_param_error (err.str ());
        }

        // Convert the SigningAccounts into SerializedTypes.
        STParsedJSONArray parsedSigningAccounts (
            signingAccocuntsArrayName, signingAccountsValue);

        if (!parsedSigningAccounts.array)
        {
            Json::Value jvResult;
            jvResult ["error"] = parsedSigningAccounts.error ["error"];
            jvResult ["error_code"] =
                parsedSigningAccounts.error ["error_code"];
            jvResult ["error_message"] =
                parsedSigningAccounts.error ["error_message"];
            return jvResult;
        }
        signingAccounts = std::move (parsedSigningAccounts.array);
    }

    for (STObject const& signingAccount : *signingAccounts)
    {
        // We want to make sure the SigningAccounts contains the right fields,
        // and only the right fields,  So there should be exactly 3 fields and
        // there should be one each of the fields we need.
        if (signingAccount.getCount () != 3)
        {
            std::ostringstream err;
            err << "Expecting exactly three fields in "
                << signingAccocuntsArrayName << "."
                << sfSigningAccount.getName ();
            return RPC::make_param_error(err.str ());
        }

        if (!signingAccount.isFieldPresent (sfAccount))
        {
            // Return an error that we're expecting a
            // SigningAccounts.SigningAccount.Account
            std::ostringstream fieldName;
            fieldName << signingAccocuntsArrayName << "."
                << sfSigningAccount.getName () << "."
                << sfAccount.getName ();
            return RPC::missing_field_error (fieldName.str ());
        }

        if (!signingAccount.isFieldPresent (sfPublicKey))
        {
            // Return an error that we're expecting a
            // SigningAccounts.SigningAccount.Account.PublicKey
                std::ostringstream fieldName;
            fieldName << signingAccocuntsArrayName << "."
                << sfSigningAccount.getName () << "."
                << sfPublicKey.getName ();
            return RPC::missing_field_error (fieldName.str ());
        }

        if (!signingAccount.isFieldPresent (sfMultiSignature))
        {
            // Return an error that we're expecting a
            // SigningAccounts.SigningAccount.Account
            std::ostringstream fieldName;
            fieldName << signingAccocuntsArrayName << "."
                << sfSigningAccount.getName () << "."
                << sfAccount.getName ();
            return RPC::missing_field_error (fieldName.str ());
        }

        // All required fields are present.
        RippleAddress const signer =
            signingAccount.getFieldAccount (sfAccount);

        if (signer == txnAccount)
        {
            std::ostringstream err;
            err << "The transaction Account, " << signer.humanAccountPublic ()
                << ", may not be a signer of a multi-signed transaction.";
            return RPC::make_param_error(err.str ());
        }

        RippleAddress const pubKey = RippleAddress::createAccountPublic (
            signingAccount.getFieldVL (sfPublicKey));

        // Verify that the Account and PublicKey belong together.
        std::string const pubHumanAccount = pubKey.humanAccountID ();
        if (pubHumanAccount != signer.humanAccountID ())
        {
            std::ostringstream err;
            err << "The SignerEntry.Account \"" << signer.humanAccountPublic ()
                << "\" and the PublicKey do not correlate.";
            return RPC::make_param_error(err.str ());
        }

        Blob const signature =
            signingAccount.getFieldVL (sfMultiSignature);
        uint256 const trans_hash = stpTrans->getSigningHash ();

        bool validSig = false;
        try
        {
            validSig = pubKey.accountPublicVerify (
                trans_hash, signature, ECDSA::not_strict);
        }
        catch (...)
        {
            // We assume any problem lies with the signature.  That's better
            // than returning "internal error".
        }
        if (!validSig)
        {
            std::ostringstream err;
            err << "Invalid MultiSignature for account: "
                << signer.ToString () << ".";
            return RPC::make_error (rpcBAD_SIGNATURE, err.str ());
        }
    }

    // SigningAccounts are submitted sorted in Account order.  This
    // assures that the same list will always have the same hash.
    signingAccounts->sort (
        [] (STObject const& a, STObject const& b) {
            return (a.getFieldAccount (sfAccount).humanAccountID () <
                b.getFieldAccount (sfAccount).humanAccountID ()); });

    // There may be no duplicate Accounts in SigningAccounts
    auto const signingAccountsEnd = signingAccounts->end ();

    auto const dupAccountItr = std::adjacent_find (
        signingAccounts->begin (), signingAccountsEnd,
            [] (STObject const& a, STObject const& b) {
                return (a.getFieldAccount (sfAccount).humanAccountID () ==
                    b.getFieldAccount (sfAccount).humanAccountID ()); });

    if (dupAccountItr != signingAccountsEnd)
    {
        std::ostringstream err;
        err << "Duplicate multi-signing AccountIDs ("
            << dupAccountItr->getFieldAccount (sfAccount).humanAccountID ()
            << ") are not allowed.";
        return RPC::make_param_error(err.str ());
    }

    uint256 const preHash = stpTrans->getSigningHash (); // !!!! DEBUG !!!!

    // Insert the SigningAccounts into the transaction.
    stpTrans->setFieldArray (sfSigningAccounts, *signingAccounts);

    uint256 const postHash = stpTrans->getSigningHash (); // !!!! DEBUG !!!!

    // Finally, submit the transaction.
    return RPCDetail::transactionSubmitImpl (
        stpTrans, NetworkOPs::SubmitTxn::yes, failType, netOps, role);
}

//------------------------------------------------------------------------------

class JSONRPC_test : public beast::unit_test::suite
{
public:
    void testAutoFillFees ()
    {
        std::string const secret = "masterpassphrase";
        RippleAddress rootSeedMaster
                = RippleAddress::createSeedGeneric (secret);
//      std::cerr << "secret: " << secret << std::endl;

        RippleAddress rootGeneratorMaster
                = RippleAddress::createGeneratorPublic (rootSeedMaster);

//      RippleAddress publicKey
//              = RippleAddress::createAccountPublic (rootGeneratorMaster, 0);
//      std::cerr << "public key: " << publicKey.ToString () << std::endl;

//      RippleAddress privateKey
//              = RippleAddress::createAccountPrivate (
//                  rootGeneratorMaster, rootSeedMaster, 0);
//      std::cerr << "private key: " << privateKey.ToString () << std::endl;

        RippleAddress rootAddress
                = RippleAddress::createAccountPublic (rootGeneratorMaster, 0);
//      std::cerr << "account: " << rootAddress.humanAccountID () << std::endl;

        std::uint64_t startAmount (100000);
        Ledger::pointer ledger (std::make_shared <Ledger> (
            rootAddress, startAmount));

        {
            Json::Value req;
            Json::Value result;
            Json::Reader ().parse (
                "{ \"fee_mult_max\" : 1, \"tx_json\" : { } } "
                , req);
            RPCDetail::autofill_fee (req, ledger, result, true);

            expect (! contains_error (result));
        }

        {
            Json::Value req;
            Json::Value result;
            Json::Reader ().parse (
                "{ \"fee_mult_max\" : 0, \"tx_json\" : { } } "
                , req);
            RPCDetail::autofill_fee (req, ledger, result, true);

            expect (contains_error (result));
        }
    }

    void run ()
    {
        testAutoFillFees ();
    }
};

BEAST_DEFINE_TESTSUITE(JSONRPC,ripple_app,ripple);

} // RPC
} // ripple
