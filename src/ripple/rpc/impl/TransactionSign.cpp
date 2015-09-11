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

#include <BeastConfig.h>
#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/main/Application.h>
#include <ripple/app/paths/FindPaths.h>
#include <ripple/basics/Log.h>
#include <ripple/basics/StringUtilities.h>
#include <ripple/core/LoadFeeTrack.h>
#include <ripple/json/json_reader.h>
#include <ripple/json/json_writer.h>
#include <ripple/net/RPCErr.h>
#include <ripple/protocol/Sign.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/STParsedJSON.h>
#include <ripple/protocol/TxFlags.h>
#include <ripple/protocol/UintTypes.h>
#include <ripple/rpc/impl/KeypairForSignature.h>
#include <ripple/rpc/impl/LegacyPathFind.h>
#include <ripple/rpc/impl/TransactionSign.h>
#include <ripple/rpc/impl/Tuning.h>

namespace ripple {
namespace RPC {
namespace detail {

// A local class used to pass extra parameters used when returning a
// a SigningFor object.
class SigningForParams
{
private:
    AccountID const* const multiSigningAcctID_;
    RippleAddress* const multiSignPublicKey_;
    Blob* const multiSignature_;
public:
    explicit SigningForParams ()
    : multiSigningAcctID_ (nullptr)
    , multiSignPublicKey_ (nullptr)
    , multiSignature_ (nullptr)
    { }

    SigningForParams (SigningForParams const& rhs) = delete;

    SigningForParams (
        AccountID const& multiSigningAcctID,
        RippleAddress& multiSignPublicKey,
        Blob& multiSignature)
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

    void setPublicKey (RippleAddress const& multiSignPublicKey)
    {
        *multiSignPublicKey_ = multiSignPublicKey;
    }

    void moveMultiSignature (Blob&& multiSignature)
    {
        *multiSignature_ = std::move (multiSignature);
    }
};

//------------------------------------------------------------------------------

// TxnSignApiFacade methods

void TxnSignApiFacade::snapshotAccountState (AccountID const& accountID)
{
    if (!netOPs_) // Unit testing.
        return;

    ledger_ = getApp().getLedgerMaster ().getCurrentLedger ();
    accountID_ = accountID;
    sle_ = cachedRead(*ledger_,
        keylet::account(accountID_).key, ltACCOUNT_ROOT);
}

bool TxnSignApiFacade::isValidAccount () const
{
    if (!ledger_) // Unit testing.
        return true;

    return static_cast <bool> (sle_);
}

std::uint32_t TxnSignApiFacade::getSeq () const
{
    if (!ledger_) // Unit testing.
        return 0;

    return sle_->getFieldU32(sfSequence);
}

void TxnSignApiFacade::processTransaction (
    Transaction::pointer& transaction,
    bool bAdmin,
    bool bLocal,
    NetworkOPs::FailHard failType)
{
    if (!netOPs_) // Unit testing.
        return;

    netOPs_->processTransaction(transaction, bAdmin, bLocal, failType);
}

boost::optional<STPathSet>
TxnSignApiFacade::findPathsForOneIssuer (
    AccountID const& dstAccountID,
    Issue const& srcIssue,
    STAmount const& dstAmount,
    int searchLevel,
    unsigned int const maxPaths,
    STPathSet const& paths,
    STPath& fullLiquidityPath) const
{
    if (! ledger_) // Unit testing.
    {
        // Note that unit tests don't (yet) need paths or fullLiquidityPath.
        boost::optional<STPathSet> result;
        result.emplace();
        return result;
    }

    auto cache = std::make_shared<RippleLineCache> (ledger_);
    return ripple::findPathsForOneIssuer(
        cache,
        accountID_,
        dstAccountID,
        srcIssue,
        dstAmount,
        searchLevel,
        maxPaths,
        paths,
        fullLiquidityPath);
}

std::uint64_t TxnSignApiFacade::scaleFeeBase (std::uint64_t fee) const
{
    if (!ledger_) // Unit testing.
        return fee;

    // VFALCO Audit
    return getApp().getFeeTrack().scaleFeeBase(
        fee, ledger_->fees().base, ledger_->fees().units);
}

std::uint64_t
TxnSignApiFacade::scaleFeeLoad (std::uint64_t fee, bool bAdmin) const
{
    if (!ledger_) // Unit testing.
        return fee;

    // VFALCO Audit
    return getApp().getFeeTrack().scaleFeeLoad(
        fee, ledger_->fees().base, ledger_->fees().units,
            bAdmin);
}

bool TxnSignApiFacade::hasAccountRoot () const
{
    if (!netOPs_) // Unit testing.
        return true;
    return ledger_->exists(
        keylet::account(accountID_));
}

error_code_i acctMatchesPubKey (
    std::shared_ptr<SLE const> accountState,
    AccountID const& accountID,
    RippleAddress const& publicKey)
{
    AccountID const publicKeyAcctID = calcAccountID(publicKey);
    bool const isMasterKey = publicKeyAcctID == accountID;

    // If we can't get the accountRoot, but the accountIDs match, that's
    // good enough.
    if (!accountState)
    {
        if (isMasterKey)
            return rpcSUCCESS;
        return rpcBAD_SECRET;
    }

    // If we *can* get to the accountRoot, check for MASTER_DISABLED
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

error_code_i TxnSignApiFacade::singleAcctMatchesPubKey (
    RippleAddress const& publicKey) const
{
    if (!netOPs_) // Unit testing.
        return rpcSUCCESS;

    return acctMatchesPubKey (sle_, accountID_, publicKey);
}

error_code_i TxnSignApiFacade::multiAcctMatchesPubKey (
    AccountID const& accountID,
    RippleAddress const& publicKey) const
{
    std::shared_ptr<SLE const> accountState;
    // VFALCO Do we need to check netOPs_?
    if (netOPs_ && ledger_)
    {
        // If it's available, get the account root for the multi-signer's
        // accountID.  It's okay if the account root is not available,
        // since they might be signing with a phantom (unfunded) account.
        accountState = cachedRead(*ledger_,
            keylet::account(accountID).key, ltACCOUNT_ROOT);
    }

    return acctMatchesPubKey (accountState, accountID, publicKey);
}

int TxnSignApiFacade::getValidatedLedgerAge () const
{
    if (!netOPs_) // Unit testing.
        return 0;

    return getApp().getLedgerMaster ().getValidatedLedgerAge ();
}

bool TxnSignApiFacade::isLoadedCluster () const
{
    if (!netOPs_) // Unit testing.
        return false;

    return getApp().getFeeTrack().isLoadedCluster();
}

//------------------------------------------------------------------------------

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

    @param tx       The JSON corresponding to the transaction to fill in.
    @param ledger   A ledger for retrieving the current fee schedule.
    @param roll     Identifies if this is called by an administrative endpoint.

    @return         A JSON object containing the error results, if any
*/

Json::Value checkFee (
    Json::Value& request,
    TxnSignApiFacade& apiFacade,
    Role const role,
    AutoFill const doAutoFill)
{
    Json::Value& tx (request[jss::tx_json]);
    if (tx.isMember (jss::Fee))
        return Json::Value();

    if (doAutoFill == AutoFill::dont)
        return RPC::missing_field_error ("tx_json.Fee");

    int mult = Tuning::defaultAutoFillFeeMultiplier;
    if (request.isMember (jss::fee_mult_max))
    {
        if (request[jss::fee_mult_max].isNumeric ())
        {
            mult = request[jss::fee_mult_max].asInt();
        }
        else
        {
            return RPC::make_error (rpcHIGH_FEE,
                RPC::expected_field_message (jss::fee_mult_max, "a number"));
        }
    }

    // Default fee in fee units.
    std::uint64_t const feeDefault = getConfig().TRANSACTION_FEE_BASE;

    // Administrative endpoints are exempt from local fees.
    std::uint64_t const fee =
        apiFacade.scaleFeeLoad (feeDefault, role == Role::ADMIN);

    std::uint64_t const limit = mult * apiFacade.scaleFeeBase (feeDefault);

    if (fee > limit)
    {
        std::stringstream ss;
        ss << "Fee of " << fee
            << " exceeds the requested tx limit of " << limit;
        return RPC::make_error (rpcHIGH_FEE, ss.str());
    }

    tx [jss::Fee] = static_cast<int>(fee);
    return Json::Value();
}

enum class PathFind : unsigned char
{
    dont,
    might
};

static Json::Value checkPayment(
    Json::Value const& params,
    Json::Value& tx_json,
    AccountID const& srcAddressID,
    TxnSignApiFacade const& apiFacade,
    Role const role,
    PathFind const doPath)
{
    // Only path find for Payments.
    if (tx_json[jss::TransactionType].asString () != "Payment")
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

    if ((doPath == PathFind::dont) && params.isMember (jss::build_path))
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
            LegacyPathFind lpf (role == Role::ADMIN);
            if (!lpf.isOk ())
                return rpcError (rpcTOO_BUSY);

            STPath fullLiquidityPath;
            auto result = apiFacade.findPathsForOneIssuer (
                *dstAccountID,
                sendMax.issue (),
                amount,
                getConfig ().PATH_SEARCH_OLD,
                4,  // iMaxPaths
                {},
                fullLiquidityPath);

            if (! result)
            {
                WriteLog (lsDEBUG, RPCHandler)
                    << "transactionSign: build_path: No paths found.";
                return rpcError (rpcNO_PATH);
            }
            WriteLog (lsDEBUG, RPCHandler)
                << "transactionSign: build_path: "
                << result->getJson (0);

            if (! result->empty ())
                tx_json[jss::Paths] = result->getJson (0);
        }
    }
    return Json::Value();
}

//------------------------------------------------------------------------------

// Validate (but don't modify) the contents of the tx_json.
//
// Returns a pair<Json::Value, AccountID>.  The Json::Value is non-empty
// and contains as error if there was an error.  The returned RippleAddress
// is the "Account" addressID if there was no error.
//
// This code does not check the "Sequence" field, since the expectations
// for that field are particularly context sensitive.
static std::pair<Json::Value, AccountID>
checkTxJsonFields (
    Json::Value const& tx_json,
    TxnSignApiFacade const& apiFacade,
    Role const role,
    bool const verify)
{
    std::pair<Json::Value, AccountID> ret;

    if (! tx_json.isObject ())
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
    if (verify && !getConfig ().RUN_STANDALONE &&
        (apiFacade.getValidatedLedgerAge() >
          Tuning::maxValidatedLedgerAge))
    {
        ret.first = rpcError (rpcNO_CURRENT);
        return ret;
    }

    // Check for load.
    if (apiFacade.isLoadedCluster() && (role != Role::ADMIN))
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
// STTx::pointer from transactionPreProcessImpl ().
struct transactionPreProcessResult
{
    Json::Value const first;
    STTx::pointer const second;

    transactionPreProcessResult () = delete;
    transactionPreProcessResult (transactionPreProcessResult const&) = delete;
    transactionPreProcessResult (transactionPreProcessResult&& rhs)
    : first (std::move (rhs.first))    // VS2013 won't default this
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

    transactionPreProcessResult (STTx::pointer&& st)
    : first ()
    , second (std::move (st))
    { }
};

static
transactionPreProcessResult
transactionPreProcessImpl (
    Json::Value& params,
    TxnSignApiFacade& apiFacade,
    Role role,
    SigningForParams& signingArgs)
{
    KeyPair keypair;
    {
        Json::Value jvResult;
        keypair = keypairForSignature (params, jvResult);
        if (contains_error (jvResult))
            return std::move (jvResult);
    }

    bool const verify = !(params.isMember (jss::offline)
                          && params[jss::offline].asBool());

    if (! params.isMember (jss::tx_json))
        return RPC::missing_field_error (jss::tx_json);

    Json::Value& tx_json (params [jss::tx_json]);

    // Check tx_json fields, but don't add any.
    auto txJsonResult = checkTxJsonFields (tx_json, apiFacade, role, verify);
    if (RPC::contains_error (txJsonResult.first))
        return std::move (txJsonResult.first);

    auto const srcAddressID = txJsonResult.second;

    // This test covers the case where we're offline so the sequence number
    // cannot be determined locally.  If we're offline then the caller must
    // provide the sequence number.
    if (!verify && !tx_json.isMember (jss::Sequence))
        return RPC::missing_field_error ("tx_json.Sequence");

    apiFacade.snapshotAccountState (srcAddressID);

    if (verify) {
        if (!apiFacade.isValidAccount())
        {
            // If not offline and did not find account, error.
            WriteLog (lsDEBUG, RPCHandler)
                << "transactionSign: Failed to find source account "
                << "in current ledger: "
                << toBase58(srcAddressID);

            return rpcError (rpcSRC_ACT_NOT_FOUND);
        }
    }

    {
        Json::Value err = checkFee (
            params,
            apiFacade,
            role,
            signingArgs.editFields() ? AutoFill::might : AutoFill::dont);

        if (RPC::contains_error (err))
            return std::move (err);

        err = checkPayment (
            params,
            tx_json,
            srcAddressID,
            apiFacade,
            role,
            signingArgs.editFields() ? PathFind::might : PathFind::dont);

        if (RPC::contains_error(err))
            return std::move (err);
    }

    if (signingArgs.editFields())
    {
        if (!tx_json.isMember (jss::Sequence))
            tx_json[jss::Sequence] = apiFacade.getSeq();

        if (!tx_json.isMember (jss::Flags))
            tx_json[jss::Flags] = tfFullyCanonicalSig;
    }

    if (verify)
    {
        if (!apiFacade.hasAccountRoot())
            // XXX Ignore transactions for accounts not created.
            return rpcError (rpcSRC_ACT_NOT_FOUND);

        WriteLog (lsTRACE, RPCHandler)
            << "verify: " << toBase58(calcAccountID(keypair.publicKey))
            << " : " << toBase58(srcAddressID);

        // If multisigning then we need to return the public key.
        if (signingArgs.isMultiSigning())
        {
            signingArgs.setPublicKey (keypair.publicKey);
        }
        else
        {
            // Make sure the account and secret belong together.
            error_code_i const err =
                apiFacade.singleAcctMatchesPubKey (keypair.publicKey);

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

    STTx::pointer stpTrans;
    try
    {
        // If we're generating a multi-signature the SigningPubKey must be
        // empty:
        //  o If we're multi-signing, use an empty blob for the signingPubKey
        //  o Otherwise use the master account's public key.
        Blob emptyBlob;
        Blob const& signingPubKey = signingArgs.isMultiSigning() ?
            emptyBlob : keypair.publicKey.getAccountPublic();

        parsed.object->setFieldVL (sfSigningPubKey, signingPubKey);

        stpTrans = std::make_shared<STTx> (std::move (parsed.object.get()));
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
        Serializer s =
            buildMultiSigningData (*stpTrans, signingArgs.getSigner ());
        Blob multiSignature = keypair.secretKey.accountPrivateSign(s.getData());
        signingArgs.moveMultiSignature (std::move (multiSignature));
    }
    else
    {
        stpTrans->sign (keypair.secretKey);
    }

    return std::move (stpTrans);
}

static
std::pair <Json::Value, Transaction::pointer>
transactionConstructImpl (STTx::pointer stpTrans)
{
    std::pair <Json::Value, Transaction::pointer> ret;

    // Turn the passed in STTx into a Transaction
    Transaction::pointer tpTrans;
    {
        std::string reason;
        tpTrans = std::make_shared<Transaction>(stpTrans, Validate::NO,
            directSigVerify, reason);
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

            Transaction::pointer tpTransNew =
                Transaction::sharedTransaction (s.getData (), Validate::YES);

            if (tpTransNew && (
                !tpTransNew->getSTransaction ()->isEquivalent (
                    *tpTrans->getSTransaction ())))
            {
                tpTransNew.reset ();
            }
            tpTrans = std::move (tpTransNew);
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

Json::Value transactionFormatResultImpl (Transaction::pointer tpTrans)
{
    Json::Value jvResult;
    try
    {
        jvResult[jss::tx_json] = tpTrans->getJson (0);
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

/** Returns a Json::objectValue. */
Json::Value transactionSign (
    Json::Value jvRequest,
    NetworkOPs::FailHard failType,
    detail::TxnSignApiFacade& apiFacade,
    Role role)
{
    WriteLog (lsDEBUG, RPCHandler) << "transactionSign: " << jvRequest;

    using namespace detail;

    // Add and amend fields based on the transaction type.
    SigningForParams signForParams;
    transactionPreProcessResult preprocResult =
        transactionPreProcessImpl (jvRequest, apiFacade, role, signForParams);

    if (!preprocResult.second)
        return preprocResult.first;

    // Make sure the STTx makes a legitimate Transaction.
    std::pair <Json::Value, Transaction::pointer> txn =
        transactionConstructImpl (preprocResult.second);

    if (!txn.second)
        return txn.first;

    return transactionFormatResultImpl (txn.second);
}

/** Returns a Json::objectValue. */
Json::Value transactionSubmit (
    Json::Value jvRequest,
    NetworkOPs::FailHard failType,
    detail::TxnSignApiFacade& apiFacade,
    Role role)
{
    WriteLog (lsDEBUG, RPCHandler) << "transactionSubmit: " << jvRequest;

    using namespace detail;

    // Add and amend fields based on the transaction type.
    SigningForParams signForParams;
    transactionPreProcessResult preprocResult =
        transactionPreProcessImpl (jvRequest, apiFacade, role, signForParams);

    if (!preprocResult.second)
        return preprocResult.first;

    // Make sure the STTx makes a legitimate Transaction.
    std::pair <Json::Value, Transaction::pointer> txn =
        transactionConstructImpl (preprocResult.second);

    if (!txn.second)
        return txn.first;

    // Finally, submit the transaction.
    try
    {
        // FIXME: For performance, should use asynch interface
        apiFacade.processTransaction (
            txn.second, role == Role::ADMIN, true, failType);
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
Json::Value checkMultiSignFields (Json::Value const& jvRequest)
{
   if (! jvRequest.isMember (jss::tx_json))
        return RPC::missing_field_error (jss::tx_json);

    Json::Value const& tx_json (jvRequest [jss::tx_json]);

    // There are a couple of additional fields we need to check before
    // we serialize.  If we serialize first then we generate less useful
    //error messages.
    if (!tx_json.isMember (jss::Sequence))
        return RPC::missing_field_error ("tx_json.Sequence");

    if (!tx_json.isMember ("SigningPubKey"))
        return RPC::missing_field_error ("tx_json.SigningPubKey");

    if (!tx_json["SigningPubKey"].asString().empty())
        return RPC::make_error (rpcINVALID_PARAMS,
            "When multi-signing 'tx_json.SigningPubKey' must be empty.");

    return Json::Value ();
}

} // detail

/** Returns a Json::objectValue. */
Json::Value transactionSignFor (
    Json::Value jvRequest,
    NetworkOPs::FailHard failType,
    detail::TxnSignApiFacade& apiFacade,
    Role role)
{
    WriteLog (lsDEBUG, RPCHandler) << "transactionSignFor: " << jvRequest;

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

    // When multi-signing, the "Sequence" and "SigningPubKey" fields must
    // be passed in by the caller.
    using namespace detail;
    {
        Json::Value err = checkMultiSignFields (jvRequest);
        if (RPC::contains_error (err))
            return std::move (err);
    }

    // Add and amend fields based on the transaction type.
    Blob multiSignature;
    RippleAddress multiSignPubKey;
    SigningForParams signForParams(
        *signerAccountID, multiSignPubKey, multiSignature);

    transactionPreProcessResult preprocResult =
        transactionPreProcessImpl (
            jvRequest,
            apiFacade,
            role,
            signForParams);

    if (!preprocResult.second)
        return preprocResult.first;

    // Make sure the multiSignAddrID can legitimately multi-sign.
    {
        error_code_i const err =
            apiFacade.multiAcctMatchesPubKey (*signerAccountID, multiSignPubKey);

        if (err != rpcSUCCESS)
            return rpcError (err);
    }

    // Make sure the STTx makes a legitimate Transaction.
    std::pair <Json::Value, Transaction::pointer> txn =
        transactionConstructImpl (preprocResult.second);

    if (!txn.second)
        return txn.first;

    Json::Value json = transactionFormatResultImpl (txn.second);
    if (RPC::contains_error (json))
        return json;

    // Finally, do what we were called for: return a Signers array.  Build
    // a Signer object to insert into the Signers array.
    Json::Value signer (Json::objectValue);

    signer[sfAccount.getJsonName ()] = toBase58 (*signerAccountID);

    signer[sfSigningPubKey.getJsonName ()] =
        strHex (multiSignPubKey.getAccountPublic ());

    signer[sfTxnSignature.getJsonName ()] = strHex (multiSignature);

    // Give the Signer an object name and put it in the Signers array.
    Json::Value nameSigner (Json::objectValue);
    nameSigner[sfSigner.getJsonName ()] = std::move (signer);

    Json::Value signers (Json::arrayValue);
    signers.append (std::move (nameSigner));

    // Inject the Signers into the json.
    json[sfSigners.getName ()] = std::move(signers);

    return json;
}

/** Returns a Json::objectValue. */
Json::Value transactionSubmitMultiSigned (
    Json::Value jvRequest,
    NetworkOPs::FailHard failType,
    detail::TxnSignApiFacade& apiFacade,
    Role role)
{
    WriteLog (lsDEBUG, RPCHandler)
        << "transactionSubmitMultiSigned: " << jvRequest;

    // When multi-signing, the "Sequence" and "SigningPubKey" fields must
    // be passed in by the caller.
    using namespace detail;
    {
        Json::Value err = checkMultiSignFields (jvRequest);
        if (RPC::contains_error (err))
            return std::move (err);
    }

    Json::Value& tx_json (jvRequest ["tx_json"]);

    auto const txJsonResult = checkTxJsonFields(tx_json, apiFacade, role, true);
    if (RPC::contains_error (txJsonResult.first))
        return std::move (txJsonResult.first);

    auto const srcAddressID = txJsonResult.second;

    apiFacade.snapshotAccountState (srcAddressID);
    if (!apiFacade.isValidAccount ())
    {
        // If not offline and did not find account, error.
        WriteLog (lsDEBUG, RPCHandler)
            << "transactionSubmitMultiSigned: Failed to find source account "
            << "in current ledger: "
            << toBase58(srcAddressID);

        return rpcError (rpcSRC_ACT_NOT_FOUND);
    }

    {
        Json::Value err = checkFee (jvRequest, apiFacade, role, AutoFill::dont);
        if (RPC::contains_error(err))
            return std::move (err);

        err = checkPayment (
            jvRequest,
            tx_json,
            srcAddressID,
            apiFacade,
            role,
            PathFind::dont);

        if (RPC::contains_error(err))
            return std::move (err);
    }

    // Grind through the JSON in tx_json to produce a STTx
    STTx::pointer stpTrans;
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
            stpTrans =
                std::make_shared<STTx> (std::move(parsedTx_json.object.get()));
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

    // Check Signers for valid entries.
    STArray signers;
    {
        // Verify that the Signers field is present and an array.
        char const* signersArrayName {sfSigners.getJsonName ().c_str ()};
        if (! jvRequest.isMember (signersArrayName))
            return RPC::missing_field_error (signersArrayName);

        Json::Value& signersValue (
            jvRequest [signersArrayName]);

        if (! signersValue.isArray ())
        {
            std::ostringstream err;
            err << "Expected "
                << signersArrayName << " to be an array";
            return RPC::make_param_error (err.str ());
        }

        // Convert signers into SerializedTypes.
        STParsedJSONArray parsedSigners (signersArrayName, signersValue);

        if (!parsedSigners.array)
        {
            Json::Value jvResult;
            jvResult ["error"] = parsedSigners.error ["error"];
            jvResult ["error_code"] = parsedSigners.error ["error_code"];
            jvResult ["error_message"] = parsedSigners.error ["error_message"];
            return jvResult;
        }
        signers = std::move (parsedSigners.array.get());
    }

    if (signers.empty ())
        return RPC::make_param_error ("Signers array may not be empty.");

    // Signers must be sorted by Account.
    signers.sort ([] (STObject const& a, STObject const& b)
    {
        return (a.getAccountID (sfAccount) < b.getAccountID (sfAccount));
    });

    // Signers may not contain any duplicates.
    auto const dupIter = std::adjacent_find (
        signers.begin(), signers.end(),
        [] (STObject const& a, STObject const& b)
        {
            return (a.getAccountID (sfAccount) == b.getAccountID (sfAccount));
        });

    if (dupIter != signers.end())
    {
        std::ostringstream err;
        err << "Duplicate Signers:Signer:Account entries ("
            << getApp().accountIDCache().toBase58(
                dupIter->getAccountID(sfAccount))
            << ") are not allowed.";
        return RPC::make_param_error(err.str ());
    }

    // An account may not sign for itself.
    if (signers.end() != std::find_if (signers.begin(), signers.end(),
        [&srcAddressID](STObject const& elem)
        {
            return elem.getAccountID (sfAccount) == srcAddressID;
        }))
    {
        std::ostringstream err;
        err << "A Signer may not be the transaction's Account ("
            << getApp().accountIDCache().toBase58(srcAddressID) << ").";
        return RPC::make_param_error(err.str ());
    }

    // Insert signers into the transaction.
    stpTrans->setFieldArray (sfSigners, std::move(signers));

    // Make sure the SerializedTransaction makes a legitimate Transaction.
    std::pair <Json::Value, Transaction::pointer> txn =
        transactionConstructImpl (stpTrans);

    if (!txn.second)
        return txn.first;

    // Finally, submit the transaction.
    try
    {
        // FIXME: For performance, should use asynch interface
        apiFacade.processTransaction (
            txn.second, role == Role::ADMIN, true, failType);
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
