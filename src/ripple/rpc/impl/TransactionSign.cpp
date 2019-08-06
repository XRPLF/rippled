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
#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/ledger/OpenLedger.h>
#include <ripple/app/main/Application.h>
#include <ripple/app/misc/LoadFeeTrack.h>
#include <ripple/app/misc/Transaction.h>
#include <ripple/app/misc/TxQ.h>
#include <ripple/app/paths/Pathfinder.h>
#include <ripple/app/tx/apply.h>              // Validity::Valid
#include <ripple/basics/Log.h>
#include <ripple/basics/mulDiv.h>
#include <ripple/json/json_writer.h>
#include <ripple/net/RPCErr.h>
#include <ripple/protocol/Sign.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/STAccount.h>
#include <ripple/protocol/STParsedJSON.h>
#include <ripple/protocol/TxFlags.h>
#include <ripple/rpc/impl/LegacyPathFind.h>
#include <ripple/rpc/impl/RPCHelpers.h>
#include <ripple/rpc/impl/Tuning.h>
#include <algorithm>
#include <iterator>

namespace ripple {
namespace RPC {
namespace detail {

// Used to pass extra parameters used when returning a
// a SigningFor object.
class SigningForParams
{
private:
    AccountID const* const multiSigningAcctID_;
    PublicKey* const multiSignPublicKey_;
    Buffer* const multiSignature_;

public:
    explicit SigningForParams ()
    : multiSigningAcctID_ (nullptr)
    , multiSignPublicKey_ (nullptr)
    , multiSignature_ (nullptr)
    { }

    SigningForParams (SigningForParams const& rhs) = delete;

    SigningForParams (
        AccountID const& multiSigningAcctID,
        PublicKey& multiSignPublicKey,
        Buffer& multiSignature)
    : multiSigningAcctID_ (&multiSigningAcctID)
    , multiSignPublicKey_ (&multiSignPublicKey)
    , multiSignature_ (&multiSignature)
    { }

    bool isMultiSigning () const
    {
        return ((multiSigningAcctID_ != nullptr) &&
                (multiSignPublicKey_ != nullptr) &&
                (multiSignature_ != nullptr));
    }

    bool isSingleSigning () const
    {
        return !isMultiSigning();
    }

    // When multi-signing we should not edit the tx_json fields.
    bool editFields () const
    {
        return !isMultiSigning();
    }

    // Don't call this method unless isMultiSigning() returns true.
    AccountID const& getSigner ()
    {
        return *multiSigningAcctID_;
    }

    void setPublicKey (PublicKey const& multiSignPublicKey)
    {
        *multiSignPublicKey_ = multiSignPublicKey;
    }

    void moveMultiSignature (Buffer&& multiSignature)
    {
        *multiSignature_ = std::move (multiSignature);
    }
};

//------------------------------------------------------------------------------

static error_code_i acctMatchesPubKey (
    std::shared_ptr<SLE const> accountState,
    AccountID const& accountID,
    PublicKey const& publicKey)
{
    auto const publicKeyAcctID = calcAccountID(publicKey);
    bool const isMasterKey = publicKeyAcctID == accountID;

    // If we can't get the accountRoot, but the accountIDs match, that's
    // good enough.
    if (!accountState)
    {
        if (isMasterKey)
            return rpcSUCCESS;
        return rpcBAD_SECRET;
    }

    // If we *can* get to the accountRoot, check for MASTER_DISABLED.
    auto const& sle = *accountState;
    if (isMasterKey)
    {
        if (sle.isFlag(lsfDisableMaster))
            return rpcMASTER_DISABLED;
        return rpcSUCCESS;
    }

    // The last gasp is that we have public Regular key.
    if ((sle.isFieldPresent (sfRegularKey)) &&
        (publicKeyAcctID == sle.getAccountID (sfRegularKey)))
    {
        return rpcSUCCESS;
    }
    return rpcBAD_SECRET;
}

static Json::Value checkPayment(
    Json::Value const& params,
    Json::Value& tx_json,
    AccountID const& srcAddressID,
    Role const role,
    Application& app,
    std::shared_ptr<ReadView const> const& ledger,
    bool doPath)
{
    // Only path find for Payments.
    if (tx_json[jss::TransactionType].asString () != jss::Payment)
        return Json::Value();

    if (!tx_json.isMember (jss::Amount))
        return RPC::missing_field_error ("tx_json.Amount");

    STAmount amount;

    if (! amountFromJsonNoThrow (amount, tx_json [jss::Amount]))
        return RPC::invalid_field_error ("tx_json.Amount");

    if (!tx_json.isMember (jss::Destination))
        return RPC::missing_field_error ("tx_json.Destination");

    auto const dstAccountID = parseBase58<AccountID>(
        tx_json[jss::Destination].asString());
    if (! dstAccountID)
        return RPC::invalid_field_error ("tx_json.Destination");

    if ((doPath == false) && params.isMember (jss::build_path))
        return RPC::make_error (rpcINVALID_PARAMS,
            "Field 'build_path' not allowed in this context.");

    if (tx_json.isMember (jss::Paths) && params.isMember (jss::build_path))
        return RPC::make_error (rpcINVALID_PARAMS,
            "Cannot specify both 'tx_json.Paths' and 'build_path'");

    if (!tx_json.isMember (jss::Paths) && params.isMember (jss::build_path))
    {
        STAmount    sendMax;

        if (tx_json.isMember (jss::SendMax))
        {
            if (! amountFromJsonNoThrow (sendMax, tx_json [jss::SendMax]))
                return RPC::invalid_field_error ("tx_json.SendMax");
        }
        else
        {
            // If no SendMax, default to Amount with sender as issuer.
            sendMax = amount;
            sendMax.setIssuer (srcAddressID);
        }

        if (sendMax.native () && amount.native ())
            return RPC::make_error (rpcINVALID_PARAMS,
                "Cannot build XRP to XRP paths.");

        {
            LegacyPathFind lpf (isUnlimited (role), app);
            if (!lpf.isOk ())
                return rpcError (rpcTOO_BUSY);

            STPathSet result;
            if (ledger)
            {
                Pathfinder pf(std::make_shared<RippleLineCache>(ledger),
                    srcAddressID, *dstAccountID, sendMax.issue().currency,
                        sendMax.issue().account, amount, boost::none, app);
                if (pf.findPaths(app.config().PATH_SEARCH_OLD))
                {
                    // 4 is the maxium paths
                    pf.computePathRanks(4);
                    STPath fullLiquidityPath;
                    STPathSet paths;
                    result = pf.getBestPaths(4, fullLiquidityPath, paths,
                        sendMax.issue().account);
                }
            }

            auto j = app.journal ("RPCHandler");
            JLOG (j.debug())
                << "transactionSign: build_path: "
                << result.getJson (JsonOptions::none);

            if (! result.empty ())
                tx_json[jss::Paths] = result.getJson (JsonOptions::none);
        }
    }
    return Json::Value();
}

//------------------------------------------------------------------------------

// Validate (but don't modify) the contents of the tx_json.
//
// Returns a pair<Json::Value, AccountID>.  The Json::Value will contain error
// information if there was an error. On success, the account ID is returned
// and the Json::Value will be empty.
//
// This code does not check the "Sequence" field, since the expectations
// for that field are particularly context sensitive.
static std::pair<Json::Value, AccountID>
checkTxJsonFields (
    Json::Value const& tx_json,
    Role const role,
    bool const verify,
    std::chrono::seconds validatedLedgerAge,
    Config const& config,
    LoadFeeTrack const& feeTrack)
{
    std::pair<Json::Value, AccountID> ret;

    if (!tx_json.isObject())
    {
        ret.first = RPC::object_field_error (jss::tx_json);
        return ret;
    }

    if (! tx_json.isMember (jss::TransactionType))
    {
        ret.first = RPC::missing_field_error ("tx_json.TransactionType");
        return ret;
    }

    if (! tx_json.isMember (jss::Account))
    {
        ret.first = RPC::make_error (rpcSRC_ACT_MISSING,
            RPC::missing_field_message ("tx_json.Account"));
        return ret;
    }

    auto const srcAddressID = parseBase58<AccountID>(
        tx_json[jss::Account].asString());

    if (! srcAddressID)
    {
        ret.first = RPC::make_error (rpcSRC_ACT_MALFORMED,
            RPC::invalid_field_message ("tx_json.Account"));
        return ret;
    }

    // Check for current ledger.
    if (verify && !config.standalone() &&
        (validatedLedgerAge > Tuning::maxValidatedLedgerAge))
    {
        ret.first = rpcError (rpcNO_CURRENT);
        return ret;
    }

    // Check for load.
    if (feeTrack.isLoadedCluster() && !isUnlimited (role))
    {
        ret.first = rpcError (rpcTOO_BUSY);
        return ret;
    }

    // It's all good.  Return the AccountID.
    ret.second = *srcAddressID;
    return ret;
}

//------------------------------------------------------------------------------

// A move-only struct that makes it easy to return either a Json::Value or a
// std::shared_ptr<STTx const> from transactionPreProcessImpl ().
struct transactionPreProcessResult
{
    Json::Value const first;
    std::shared_ptr<STTx> const second;

    transactionPreProcessResult () = delete;
    transactionPreProcessResult (transactionPreProcessResult const&) = delete;
    transactionPreProcessResult (transactionPreProcessResult&& rhs)
    : first (std::move (rhs.first))    // VS2013 won't default this.
    , second (std::move (rhs.second))
    { }

    transactionPreProcessResult& operator=
        (transactionPreProcessResult const&) = delete;
     transactionPreProcessResult& operator=
        (transactionPreProcessResult&&) = delete;

    transactionPreProcessResult (Json::Value&& json)
    : first (std::move (json))
    , second ()
    { }

    explicit transactionPreProcessResult (std::shared_ptr<STTx>&& st)
    : first ()
    , second (std::move (st))
    { }
};

static
transactionPreProcessResult
transactionPreProcessImpl (
    Json::Value& params,
    Role role,
    SigningForParams& signingArgs,
    std::chrono::seconds validatedLedgerAge,
    Application& app,
    std::shared_ptr<OpenView const> const& ledger)
{
    auto j = app.journal ("RPCHandler");

    Json::Value jvResult;
    auto const [pk, sk] = keypairForSignature (params, jvResult);
    if (contains_error (jvResult))
        return std::move (jvResult);

    bool const verify = !(params.isMember (jss::offline)
                          && params[jss::offline].asBool());

    if (! params.isMember (jss::tx_json))
        return RPC::missing_field_error (jss::tx_json);

    Json::Value& tx_json (params [jss::tx_json]);

    // Check tx_json fields, but don't add any.
    auto [txJsonResult, srcAddressID] = checkTxJsonFields (
        tx_json, role, verify, validatedLedgerAge,
        app.config(), app.getFeeTrack());

    if (RPC::contains_error (txJsonResult))
        return std::move (txJsonResult);

    // This test covers the case where we're offline so the sequence number
    // cannot be determined locally.  If we're offline then the caller must
    // provide the sequence number.
    if (!verify && !tx_json.isMember (jss::Sequence))
        return RPC::missing_field_error ("tx_json.Sequence");

    std::shared_ptr<SLE const> sle = ledger->read(
            keylet::account(srcAddressID));

    if (verify && !sle)
    {
        // If not offline and did not find account, error.
        JLOG (j.debug())
            << "transactionSign: Failed to find source account "
            << "in current ledger: "
            << toBase58(srcAddressID);

        return rpcError (rpcSRC_ACT_NOT_FOUND);
    }

    {
        Json::Value err = checkFee (
            params,
            role,
            verify && signingArgs.editFields(),
            app.config(),
            app.getFeeTrack(),
            app.getTxQ(),
            ledger);

        if (RPC::contains_error (err))
            return std::move (err);

        err = checkPayment (
            params,
            tx_json,
            srcAddressID,
            role,
            app,
            ledger,
            verify && signingArgs.editFields());

        if (RPC::contains_error(err))
            return std::move (err);
    }

    if (signingArgs.editFields())
    {
        if (!tx_json.isMember (jss::Sequence))
        {
            if (! sle)
            {
                JLOG (j.debug())
                << "transactionSign: Failed to find source account "
                << "in current ledger: "
                << toBase58(srcAddressID);

                return rpcError (rpcSRC_ACT_NOT_FOUND);
            }

            auto seq = (*sle)[sfSequence];
            auto const queued = app.getTxQ().getAccountTxs(srcAddressID,
                *ledger);
            // If the account has any txs in the TxQ, skip those sequence
            // numbers (accounting for possible gaps).
            for(auto const& tx : queued)
            {
                if (tx.first == seq)
                    ++seq;
                else if (tx.first > seq)
                    break;
            }
            tx_json[jss::Sequence] = seq;
        }

        if (!tx_json.isMember (jss::Flags))
            tx_json[jss::Flags] = tfFullyCanonicalSig;
    }

    // If multisigning there should not be a single signature and vice versa.
    if (signingArgs.isMultiSigning())
    {
        if (tx_json.isMember (sfTxnSignature.jsonName))
            return rpcError (rpcALREADY_SINGLE_SIG);

        // If multisigning then we need to return the public key.
        signingArgs.setPublicKey (pk);
    }
    else if (signingArgs.isSingleSigning())
    {
        if (tx_json.isMember (sfSigners.jsonName))
            return rpcError (rpcALREADY_MULTISIG);
    }

    if (verify)
    {
        if (! sle)
            // XXX Ignore transactions for accounts not created.
            return rpcError (rpcSRC_ACT_NOT_FOUND);

        JLOG (j.trace())
            << "verify: " << toBase58(calcAccountID(pk))
            << " : " << toBase58(srcAddressID);

        // Don't do this test if multisigning since the account and secret
        // probably don't belong together in that case.
        if (!signingArgs.isMultiSigning())
        {
            // Make sure the account and secret belong together.
            auto const err = acctMatchesPubKey (
                sle, srcAddressID, pk);

            if (err != rpcSUCCESS)
                return rpcError (err);
        }
    }

    STParsedJSONObject parsed (std::string (jss::tx_json), tx_json);
    if (parsed.object == boost::none)
    {
        Json::Value err;
        err [jss::error] = parsed.error [jss::error];
        err [jss::error_code] = parsed.error [jss::error_code];
        err [jss::error_message] = parsed.error [jss::error_message];
        return std::move (err);
    }

    std::shared_ptr<STTx> stpTrans;
    try
    {
        // If we're generating a multi-signature the SigningPubKey must be
        // empty, otherwise it must be the master account's public key.
        parsed.object->setFieldVL (sfSigningPubKey,
            signingArgs.isMultiSigning()
                ? Slice (nullptr, 0)
                : pk.slice());

        stpTrans = std::make_shared<STTx> (
            std::move (parsed.object.get()));
    }
    catch (STObject::FieldErr& err)
    {
        return RPC::make_error (rpcINVALID_PARAMS, err.what());
    }
    catch (std::exception&)
    {
        return RPC::make_error (rpcINTERNAL,
            "Exception occurred constructing serialized transaction");
    }

    std::string reason;
    if (!passesLocalChecks (*stpTrans, reason))
        return RPC::make_error (rpcINVALID_PARAMS, reason);

    // If multisign then return multiSignature, else set TxnSignature field.
    if (signingArgs.isMultiSigning ())
    {
        Serializer s = buildMultiSigningData (*stpTrans,
            signingArgs.getSigner ());

        auto multisig = ripple::sign (
            pk,
            sk,
            s.slice());

        signingArgs.moveMultiSignature (std::move (multisig));
    }
    else if (signingArgs.isSingleSigning())
    {
        stpTrans->sign (pk, sk);
    }

    return transactionPreProcessResult {std::move (stpTrans)};
}

static
std::pair <Json::Value, Transaction::pointer>
transactionConstructImpl (std::shared_ptr<STTx const> const& stpTrans,
    Rules const& rules, Application& app)
{
    std::pair <Json::Value, Transaction::pointer> ret;

    // Turn the passed in STTx into a Transaction.
    Transaction::pointer tpTrans;
    {
        std::string reason;
        tpTrans = std::make_shared<Transaction>(
            stpTrans, reason, app);
        if (tpTrans->getStatus () != NEW)
        {
            ret.first = RPC::make_error (rpcINTERNAL,
                "Unable to construct transaction: " + reason);
            return ret;
        }
    }
    try
    {
        // Make sure the Transaction we just built is legit by serializing it
        // and then de-serializing it.  If the result isn't equivalent
        // to the initial transaction then there's something wrong with the
        // passed-in STTx.
        {
            Serializer s;
            tpTrans->getSTransaction ()->add (s);
            Blob transBlob = s.getData ();
            SerialIter sit {makeSlice(transBlob)};

            // Check the signature if that's called for.
            auto sttxNew = std::make_shared<STTx const> (sit);
            if (!app.checkSigs())
                forceValidity(app.getHashRouter(),
                    sttxNew->getTransactionID(), Validity::SigGoodOnly);
            if (checkValidity(app.getHashRouter(),
                *sttxNew, rules, app.config()).first != Validity::Valid)
            {
                ret.first = RPC::make_error (rpcINTERNAL,
                    "Invalid signature.");
                return ret;
            }

            std::string reason;
            auto tpTransNew =
                std::make_shared<Transaction> (sttxNew, reason, app);

            if (tpTransNew)
            {
                if (!tpTransNew->getSTransaction()->isEquivalent (
                        *tpTrans->getSTransaction()))
                {
                    tpTransNew.reset ();
                }
                tpTrans = std::move (tpTransNew);
            }
        }
    }
    catch (std::exception&)
    {
        // Assume that any exceptions are related to transaction sterilization.
        tpTrans.reset ();
    }

    if (!tpTrans)
    {
        ret.first = RPC::make_error (rpcINTERNAL,
            "Unable to sterilize transaction.");
        return ret;
    }
    ret.second = std::move (tpTrans);
    return ret;
}

static Json::Value transactionFormatResultImpl (Transaction::pointer tpTrans)
{
    Json::Value jvResult;
    try
    {
        jvResult[jss::tx_json] = tpTrans->getJson (JsonOptions::none);
        jvResult[jss::tx_blob] = strHex (
            tpTrans->getSTransaction ()->getSerializer ().peekData ());

        if (temUNCERTAIN != tpTrans->getResult ())
        {
            std::string sToken;
            std::string sHuman;

            transResultInfo (tpTrans->getResult (), sToken, sHuman);

            jvResult[jss::engine_result]           = sToken;
            jvResult[jss::engine_result_code]      = tpTrans->getResult ();
            jvResult[jss::engine_result_message]   = sHuman;
        }
    }
    catch (std::exception&)
    {
        jvResult = RPC::make_error (rpcINTERNAL,
            "Exception occurred during JSON handling.");
    }
    return jvResult;
}

} // detail

//------------------------------------------------------------------------------

Json::Value checkFee (
    Json::Value& request,
    Role const role,
    bool doAutoFill,
    Config const& config,
    LoadFeeTrack const& feeTrack,
    TxQ const& txQ,
    std::shared_ptr<OpenView const> const& ledger)
{
    Json::Value& tx (request[jss::tx_json]);
    if (tx.isMember (jss::Fee))
        return Json::Value();

    if (! doAutoFill)
        return RPC::missing_field_error ("tx_json.Fee");

    int mult = Tuning::defaultAutoFillFeeMultiplier;
    int div = Tuning::defaultAutoFillFeeDivisor;
    if (request.isMember (jss::fee_mult_max))
    {
        if (request[jss::fee_mult_max].isInt())
        {
            mult = request[jss::fee_mult_max].asInt();
            if (mult < 0)
                return RPC::make_error(rpcINVALID_PARAMS,
                    RPC::expected_field_message(jss::fee_mult_max,
                        "a positive integer"));
        }
        else
        {
            return RPC::make_error (rpcHIGH_FEE,
                RPC::expected_field_message (jss::fee_mult_max,
                    "a positive integer"));
        }
    }
    if (request.isMember(jss::fee_div_max))
    {
        if (request[jss::fee_div_max].isInt())
        {
            div = request[jss::fee_div_max].asInt();
            if (div <= 0)
                return RPC::make_error(rpcINVALID_PARAMS,
                    RPC::expected_field_message(jss::fee_div_max,
                        "a positive integer"));
        }
        else
        {
            return RPC::make_error(rpcHIGH_FEE,
                RPC::expected_field_message(jss::fee_div_max,
                    "a positive integer"));
        }
    }

    // Default fee in fee units.
    std::uint64_t const feeDefault = config.TRANSACTION_FEE_BASE;

    // Administrative and identified endpoints are exempt from local fees.
    std::uint64_t const loadFee =
        scaleFeeLoad (feeDefault, feeTrack,
            ledger->fees(), isUnlimited (role));
    std::uint64_t fee = loadFee;
    {
        auto const metrics = txQ.getMetrics(*ledger);
        auto const baseFee = ledger->fees().base;
        auto escalatedFee = mulDiv(
            metrics.openLedgerFeeLevel, baseFee,
                metrics.referenceFeeLevel).second;
        if (mulDiv(escalatedFee, metrics.referenceFeeLevel,
                baseFee).second < metrics.openLedgerFeeLevel)
            ++escalatedFee;
        fee = std::max(fee, escalatedFee);
    }

    auto const limit = [&]()
    {
        // Scale fee units to drops:
        auto const drops = mulDiv (feeDefault,
            ledger->fees().base, ledger->fees().units);
        if (!drops.first)
            Throw<std::overflow_error>("mulDiv");
        auto const result = mulDiv (drops.second, mult, div);
        if (!result.first)
            Throw<std::overflow_error>("mulDiv");
        return result.second;
    }();

    if (fee > limit)
    {
        std::stringstream ss;
        ss << "Fee of " << fee
            << " exceeds the requested tx limit of " << limit;
        return RPC::make_error (rpcHIGH_FEE, ss.str());
    }

    tx [jss::Fee] = static_cast<unsigned int>(fee);
    return Json::Value();
}

//------------------------------------------------------------------------------

/** Returns a Json::objectValue. */
Json::Value transactionSign (
    Json::Value jvRequest,
    NetworkOPs::FailHard failType,
    Role role,
    std::chrono::seconds validatedLedgerAge,
    Application& app)
{
    using namespace detail;

    auto const& ledger = app.openLedger().current();
    auto j = app.journal ("RPCHandler");
    JLOG (j.debug()) << "transactionSign: " << jvRequest;

    // Add and amend fields based on the transaction type.
    SigningForParams signForParams;
    transactionPreProcessResult preprocResult = transactionPreProcessImpl (
        jvRequest, role, signForParams,
        validatedLedgerAge, app, ledger);

    if (!preprocResult.second)
        return preprocResult.first;

    // Make sure the STTx makes a legitimate Transaction.
    std::pair <Json::Value, Transaction::pointer> txn =
        transactionConstructImpl (
            preprocResult.second, ledger->rules(), app);

    if (!txn.second)
        return txn.first;

    return transactionFormatResultImpl (txn.second);
}

/** Returns a Json::objectValue. */
Json::Value transactionSubmit (
    Json::Value jvRequest,
    NetworkOPs::FailHard failType,
    Role role,
    std::chrono::seconds validatedLedgerAge,
    Application& app,
    ProcessTransactionFn const& processTransaction)
{
    using namespace detail;

    auto const& ledger = app.openLedger().current();
    auto j = app.journal ("RPCHandler");
    JLOG (j.debug()) << "transactionSubmit: " << jvRequest;


    // Add and amend fields based on the transaction type.
    SigningForParams signForParams;
    transactionPreProcessResult preprocResult = transactionPreProcessImpl (
        jvRequest, role, signForParams, validatedLedgerAge, app, ledger);

    if (!preprocResult.second)
        return preprocResult.first;

    // Make sure the STTx makes a legitimate Transaction.
    std::pair <Json::Value, Transaction::pointer> txn =
        transactionConstructImpl (
            preprocResult.second, ledger->rules(), app);

    if (!txn.second)
        return txn.first;

    // Finally, submit the transaction.
    try
    {
        // FIXME: For performance, should use asynch interface
        processTransaction (
            txn.second, isUnlimited (role), true, failType);
    }
    catch (std::exception&)
    {
        return RPC::make_error (rpcINTERNAL,
            "Exception occurred during transaction submission.");
    }

    return transactionFormatResultImpl (txn.second);
}

namespace detail
{
// There are a some field checks shared by transactionSignFor
// and transactionSubmitMultiSigned.  Gather them together here.
static Json::Value checkMultiSignFields (Json::Value const& jvRequest)
{
   if (! jvRequest.isMember (jss::tx_json))
        return RPC::missing_field_error (jss::tx_json);

    Json::Value const& tx_json (jvRequest [jss::tx_json]);

    if (!tx_json.isObject())
        return RPC::invalid_field_message (jss::tx_json);

    // There are a couple of additional fields we need to check before
    // we serialize.  If we serialize first then we generate less useful
    //error messages.
    if (!tx_json.isMember (jss::Sequence))
        return RPC::missing_field_error ("tx_json.Sequence");

    if (!tx_json.isMember (sfSigningPubKey.getJsonName()))
        return RPC::missing_field_error ("tx_json.SigningPubKey");

    if (!tx_json[sfSigningPubKey.getJsonName()].asString().empty())
        return RPC::make_error (rpcINVALID_PARAMS,
            "When multi-signing 'tx_json.SigningPubKey' must be empty.");

    return Json::Value ();
}

// Sort and validate an stSigners array.
//
// Returns a null Json::Value if there are no errors.
static Json::Value sortAndValidateSigners (
    STArray& signers, AccountID const& signingForID)
{
    if (signers.empty ())
        return RPC::make_param_error ("Signers array may not be empty.");

    // Signers must be sorted by Account.
    std::sort (signers.begin(), signers.end(),
        [](STObject const& a, STObject const& b)
    {
        return (a[sfAccount] < b[sfAccount]);
    });

    // Signers may not contain any duplicates.
    auto const dupIter = std::adjacent_find (
        signers.begin(), signers.end(),
        [] (STObject const& a, STObject const& b)
        {
            return (a[sfAccount] == b[sfAccount]);
        });

    if (dupIter != signers.end())
    {
        std::ostringstream err;
        err << "Duplicate Signers:Signer:Account entries ("
            << toBase58((*dupIter)[sfAccount])
            << ") are not allowed.";
        return RPC::make_param_error(err.str ());
    }

    // An account may not sign for itself.
    if (signers.end() != std::find_if (signers.begin(), signers.end(),
        [&signingForID](STObject const& elem)
        {
            return elem[sfAccount] == signingForID;
        }))
    {
        std::ostringstream err;
        err << "A Signer may not be the transaction's Account ("
            << toBase58(signingForID) << ").";
        return RPC::make_param_error(err.str ());
    }
    return {};
}

} // detail

/** Returns a Json::objectValue. */
Json::Value transactionSignFor (
    Json::Value jvRequest,
    NetworkOPs::FailHard failType,
    Role role,
    std::chrono::seconds validatedLedgerAge,
    Application& app)
{
    auto const& ledger = app.openLedger().current();
    auto j = app.journal ("RPCHandler");
    JLOG (j.debug()) << "transactionSignFor: " << jvRequest;

    // Verify presence of the signer's account field.
    const char accountField[] = "account";

    if (! jvRequest.isMember (accountField))
        return RPC::missing_field_error (accountField);

    // Turn the signer's account into an AccountID for multi-sign.
    auto const signerAccountID = parseBase58<AccountID>(
        jvRequest[accountField].asString());
    if (! signerAccountID)
    {
        return RPC::make_error (rpcSRC_ACT_MALFORMED,
            RPC::invalid_field_message (accountField));
    }

   if (! jvRequest.isMember (jss::tx_json))
        return RPC::missing_field_error (jss::tx_json);

    {
        Json::Value& tx_json (jvRequest [jss::tx_json]);

        if (!tx_json.isObject())
            return RPC::object_field_error (jss::tx_json);

        // If the tx_json.SigningPubKey field is missing,
        // insert an empty one.
        if (!tx_json.isMember (sfSigningPubKey.getJsonName()))
            tx_json[sfSigningPubKey.getJsonName()] = "";
    }

    // When multi-signing, the "Sequence" and "SigningPubKey" fields must
    // be passed in by the caller.
    using namespace detail;
    {
        Json::Value err = checkMultiSignFields (jvRequest);
        if (RPC::contains_error (err))
            return err;
    }

    // Add and amend fields based on the transaction type.
    Buffer multiSignature;
    PublicKey multiSignPubKey;
    SigningForParams signForParams(
        *signerAccountID, multiSignPubKey, multiSignature);

    transactionPreProcessResult preprocResult = transactionPreProcessImpl (
        jvRequest,
        role,
        signForParams,
        validatedLedgerAge,
        app,
        ledger);

    if (!preprocResult.second)
        return preprocResult.first;

    {
        std::shared_ptr<SLE const> account_state = ledger->read(
                keylet::account(*signerAccountID));
        // Make sure the account and secret belong together.
        auto const err = acctMatchesPubKey (
            account_state,
            *signerAccountID,
            multiSignPubKey);

        if (err != rpcSUCCESS)
            return rpcError (err);
    }

    // Inject the newly generated signature into tx_json.Signers.
    auto& sttx = preprocResult.second;
    {
        // Make the signer object that we'll inject.
        STObject signer (sfSigner);
        signer[sfAccount] = *signerAccountID;
        signer.setFieldVL (sfTxnSignature, multiSignature);
        signer.setFieldVL (sfSigningPubKey, multiSignPubKey.slice());

        // If there is not yet a Signers array, make one.
        if (!sttx->isFieldPresent (sfSigners))
            sttx->setFieldArray (sfSigners, {});

        auto& signers = sttx->peekFieldArray (sfSigners);
        signers.emplace_back (std::move (signer));

        // The array must be sorted and validated.
        auto err = sortAndValidateSigners (signers, (*sttx)[sfAccount]);
        if (RPC::contains_error (err))
            return err;
    }

    // Make sure the STTx makes a legitimate Transaction.
    std::pair <Json::Value, Transaction::pointer> txn =
        transactionConstructImpl (sttx, ledger->rules(), app);

    if (!txn.second)
        return txn.first;

    return transactionFormatResultImpl (txn.second);
}

/** Returns a Json::objectValue. */
Json::Value transactionSubmitMultiSigned (
    Json::Value jvRequest,
    NetworkOPs::FailHard failType,
    Role role,
    std::chrono::seconds validatedLedgerAge,
    Application& app,
    ProcessTransactionFn const& processTransaction)
{
    auto const& ledger = app.openLedger().current();
    auto j = app.journal ("RPCHandler");
    JLOG (j.debug())
        << "transactionSubmitMultiSigned: " << jvRequest;

    // When multi-signing, the "Sequence" and "SigningPubKey" fields must
    // be passed in by the caller.
    using namespace detail;
    {
        Json::Value err = checkMultiSignFields (jvRequest);
        if (RPC::contains_error (err))
            return err;
    }

    Json::Value& tx_json (jvRequest ["tx_json"]);

    auto [txJsonResult, srcAddressID] = checkTxJsonFields (
        tx_json, role, true, validatedLedgerAge,
        app.config(), app.getFeeTrack());

    if (RPC::contains_error (txJsonResult))
        return std::move (txJsonResult);

    std::shared_ptr<SLE const> sle = ledger->read(
            keylet::account(srcAddressID));

    if (!sle)
    {
        // If did not find account, error.
        JLOG (j.debug())
            << "transactionSubmitMultiSigned: Failed to find source account "
            << "in current ledger: "
            << toBase58(srcAddressID);

        return rpcError (rpcSRC_ACT_NOT_FOUND);
    }

    {
        Json::Value err = checkFee (
            jvRequest, role, false, app.config(), app.getFeeTrack(),
                app.getTxQ(), ledger);

        if (RPC::contains_error(err))
            return err;

        err = checkPayment (
            jvRequest,
            tx_json,
            srcAddressID,
            role,
            app,
            ledger,
            false);

        if (RPC::contains_error(err))
            return err;
    }

    // Grind through the JSON in tx_json to produce a STTx.
    std::shared_ptr<STTx> stpTrans;
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
        try
        {
            stpTrans = std::make_shared<STTx>(
                std::move(parsedTx_json.object.get()));
        }
        catch (STObject::FieldErr& err)
        {
            return RPC::make_error (rpcINVALID_PARAMS, err.what());
        }
        catch (std::exception& ex)
        {
            std::string reason (ex.what ());
            return RPC::make_error (rpcINTERNAL,
                "Exception while serializing transaction: " + reason);
        }
        std::string reason;
        if (!passesLocalChecks (*stpTrans, reason))
            return RPC::make_error (rpcINVALID_PARAMS, reason);
    }

    // Validate the fields in the serialized transaction.
    {
        // We now have the transaction text serialized and in the right format.
        // Verify the values of select fields.
        //
        // The SigningPubKey must be present but empty.
        if (!stpTrans->getFieldVL (sfSigningPubKey).empty ())
        {
            std::ostringstream err;
            err << "Invalid  " << sfSigningPubKey.fieldName
                << " field.  Field must be empty when multi-signing.";
            return RPC::make_error (rpcINVALID_PARAMS, err.str ());
        }

        // There may not be a TxnSignature field.
        if (stpTrans->isFieldPresent (sfTxnSignature))
            return rpcError (rpcSIGNING_MALFORMED);

        // The Fee field must be in XRP and greater than zero.
        auto const fee = stpTrans->getFieldAmount (sfFee);

        if (!isLegalNet (fee))
        {
            std::ostringstream err;
            err << "Invalid " << sfFee.fieldName
                << " field.  Fees must be specified in XRP.";
            return RPC::make_error (rpcINVALID_PARAMS, err.str ());
        }
        if (fee <= 0)
        {
            std::ostringstream err;
            err << "Invalid " << sfFee.fieldName
                << " field.  Fees must be greater than zero.";
            return RPC::make_error (rpcINVALID_PARAMS, err.str ());
        }
    }

    // Verify that the Signers field is present.
    if (! stpTrans->isFieldPresent (sfSigners))
        return RPC::missing_field_error ("tx_json.Signers");

    // If the Signers field is present the SField guarantees it to be an array.
    // Get a reference to the Signers array so we can verify and sort it.
    auto& signers = stpTrans->peekFieldArray (sfSigners);

    if (signers.empty ())
        return RPC::make_param_error("tx_json.Signers array may not be empty.");

    // The Signers array may only contain Signer objects.
    if (std::find_if_not(signers.begin(), signers.end(), [](STObject const& obj)
        {
            return (
                // A Signer object always contains these fields and no others.
                obj.isFieldPresent (sfAccount) &&
                obj.isFieldPresent (sfSigningPubKey) &&
                obj.isFieldPresent (sfTxnSignature) &&
                obj.getCount() == 3);
        }) != signers.end())
    {
        return RPC::make_param_error (
            "Signers array may only contain Signer entries.");
    }

    // The array must be sorted and validated.
    auto err = sortAndValidateSigners (signers, srcAddressID);
    if (RPC::contains_error (err))
        return err;

    // Make sure the SerializedTransaction makes a legitimate Transaction.
    std::pair <Json::Value, Transaction::pointer> txn =
        transactionConstructImpl (stpTrans, ledger->rules(), app);

    if (!txn.second)
        return txn.first;

    // Finally, submit the transaction.
    try
    {
        // FIXME: For performance, should use asynch interface
        processTransaction (
            txn.second, isUnlimited (role), true, failType);
    }
    catch (std::exception&)
    {
        return RPC::make_error (rpcINTERNAL,
            "Exception occurred during transaction submission.");
    }

    return transactionFormatResultImpl (txn.second);
}

} // RPC
} // ripple
