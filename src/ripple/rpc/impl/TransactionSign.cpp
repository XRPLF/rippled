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

#include <ripple/app/paths/FindPaths.h>
#include <ripple/basics/StringUtilities.h>
#include <ripple/protocol/TxFlags.h>
#include <ripple/rpc/impl/TransactionSign.h>
#include <beast/unit_test.h>

namespace ripple {

//------------------------------------------------------------------------------

namespace RPC {
namespace RPCDetail {

// LedgerFacade methods

void LedgerFacade::snapshotAccountState (RippleAddress const& accountID)
{
    if (!netOPs_) // Unit testing.
        return;

    ledger_ = netOPs_->getCurrentLedger ();
    accountID_ = accountID;
    accountState_ = netOPs_->getAccountState (ledger_, accountID_);
}

bool LedgerFacade::isValidAccount () const
{
    if (!ledger_) // Unit testing.
        return true;

    return static_cast <bool> (accountState_);
}

std::uint32_t LedgerFacade::getSeq () const
{
    if (!ledger_) // Unit testing.
        return 0;

    return accountState_->getSeq ();
}

Transaction::pointer LedgerFacade::submitTransactionSync (
    Transaction::ref tpTrans,
    bool bAdmin,
    bool bLocal,
    bool bFailHard,
    bool bSubmit)
{
    if (!netOPs_) // Unit testing.
        return tpTrans;

    return netOPs_->submitTransactionSync (
        tpTrans, bAdmin, bLocal, bFailHard, bSubmit);
}

bool LedgerFacade::findPathsForOneIssuer (
    RippleAddress const& dstAccountID,
    Issue const& srcIssue,
    STAmount const& dstAmount,
    int searchLevel,
    unsigned int const maxPaths,
    STPathSet& pathsOut,
    STPath& fullLiquidityPath) const
{
    if (!ledger_) // Unit testing.
        // Note that unit tests don't (yet) need pathsOut or fullLiquidityPath.
        return true;

    auto cache = std::make_shared<RippleLineCache> (ledger_);
    return ripple::findPathsForOneIssuer (
        cache,
        accountID_.getAccountID (),
        dstAccountID.getAccountID (),
        srcIssue,
        dstAmount,
        searchLevel,
        maxPaths,
        pathsOut,
        fullLiquidityPath);
}

std::uint64_t LedgerFacade::scaleFeeBase (std::uint64_t fee) const
{
    if (!ledger_) // Unit testing.
        return fee;

    return ledger_->scaleFeeBase (fee);
}

std::uint64_t LedgerFacade::scaleFeeLoad (std::uint64_t fee, bool bAdmin) const
{
    if (!ledger_) // Unit testing.
        return fee;

    return ledger_->scaleFeeLoad (fee, bAdmin);
}

bool LedgerFacade::hasAccountRoot () const
{
    if (!netOPs_) // Unit testing.
        return true;

    SLE::pointer const sleAccountRoot =
        netOPs_->getSLEi (ledger_, getAccountRootIndex (accountID_));

    return static_cast <bool> (sleAccountRoot);
}

bool LedgerFacade::accountMasterDisabled () const
{
    if (!accountState_) // Unit testing.
        return false;

    STLedgerEntry const& sle = accountState_->peekSLE ();
    return sle.isFlag(lsfDisableMaster);
}

bool LedgerFacade::accountMatchesRegularKey (Account account) const
{
    if (!accountState_) // Unit testing.
        return true;

    STLedgerEntry const& sle = accountState_->peekSLE ();
    return ((sle.isFieldPresent (sfRegularKey)) &&
        (account == sle.getFieldAccount160 (sfRegularKey)));
}

int LedgerFacade::getValidatedLedgerAge () const
{
    if (!netOPs_) // Unit testing.
        return 0;

    return getApp( ).getLedgerMaster ().getValidatedLedgerAge ();
}

bool LedgerFacade::isLoadedCluster () const
{
    if (!netOPs_) // Unit testing.
        return false;

    return getApp().getFeeTrack().isLoadedCluster();
}
} // namespace RPCDetail

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

    @param tx       The JSON corresponding to the transaction to fill in
    @param ledger   A ledger for retrieving the current fee schedule
    @param result   A JSON object for injecting error results, if any
    @param admin    `true` if this is called by an administrative endpoint.
*/
static void autofill_fee (
    Json::Value& request,
    RPCDetail::LedgerFacade& ledgerFacade,
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

    // Default fee in fee units.
    std::uint64_t const feeDefault = getConfig().TRANSACTION_FEE_BASE;

    // Administrative endpoints are exempt from local fees.
    std::uint64_t const fee = ledgerFacade.scaleFeeLoad (feeDefault, admin);
    std::uint64_t const limit = mult * ledgerFacade.scaleFeeBase (feeDefault);

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
    RPCDetail::LedgerFacade& ledgerFacade,
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
            "Cannot specify both 'tx_json.Paths' and 'build_path'");

    if (!tx_json.isMember ("Paths")
        && tx_json.isMember ("Amount")
        && params.isMember ("build_path"))
    {
        // Need a ripple path.
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

            STPathSet spsPaths;
            STPath fullLiquidityPath;
            bool valid = ledgerFacade.findPathsForOneIssuer (
                dstAccountID,
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

//------------------------------------------------------------------------------

// VFALCO TODO This function should take a reference to the params, modify it
//             as needed, and then there should be a separate function to
//             submit the transaction.
//
Json::Value
transactionSign (
    Json::Value params,
    bool bSubmit,
    bool bFailHard,
    RPCDetail::LedgerFacade& ledgerFacade,
    Role role)
{
    Json::Value jvResult;

    WriteLog (lsDEBUG, RPCHandler) << "transactionSign: " << params;

    if (! params.isMember ("secret"))
        return RPC::missing_field_error ("secret");

    if (! params.isMember ("tx_json"))
        return RPC::missing_field_error ("tx_json");

    RippleAddress naSeed;

    if (! naSeed.setSeedGeneric (params["secret"].asString ()))
        return RPC::make_error (rpcBAD_SEED,
            RPC::invalid_field_message ("secret"));

    Json::Value& tx_json (params ["tx_json"]);

    if (! tx_json.isObject ())
        return RPC::object_field_error ("tx_json");

    if (! tx_json.isMember ("TransactionType"))
        return RPC::missing_field_error ("tx_json.TransactionType");

    std::string const sType = tx_json ["TransactionType"].asString ();

    if (! tx_json.isMember ("Account"))
        return RPC::make_error (rpcSRC_ACT_MISSING,
            RPC::missing_field_message ("tx_json.Account"));

    RippleAddress raSrcAddressID;

    if (! raSrcAddressID.setAccountID (tx_json["Account"].asString ()))
        return RPC::make_error (rpcSRC_ACT_MALFORMED,
            RPC::invalid_field_message ("tx_json.Account"));

    bool const verify = !(params.isMember ("offline")
                          && params["offline"].asBool ());

    if (!tx_json.isMember ("Sequence") && !verify)
        return RPC::missing_field_error ("tx_json.Sequence");

    // Check for current ledger.
    if (verify && !getConfig ().RUN_STANDALONE &&
        (ledgerFacade.getValidatedLedgerAge () > 120))
        return rpcError (rpcNO_CURRENT);

    // Check for load.
    if (ledgerFacade.isLoadedCluster () && (role != Role::ADMIN))
        return rpcError (rpcTOO_BUSY);

    ledgerFacade.snapshotAccountState (raSrcAddressID);

    if (verify) {
        if (!ledgerFacade.isValidAccount ())
        {
            // If not offline and did not find account, error.
            WriteLog (lsDEBUG, RPCHandler)
                << "transactionSign: Failed to find source account "
                << "in current ledger: "
                << raSrcAddressID.humanAccountID ();

            return rpcError (rpcSRC_ACT_NOT_FOUND);
        }
    }

    autofill_fee (params, ledgerFacade, jvResult, role == Role::ADMIN);
    if (RPC::contains_error (jvResult))
        return jvResult;

    if ("Payment" == sType)
    {
        auto e = signPayment(
            params,
            tx_json,
            raSrcAddressID,
            ledgerFacade,
            role);
        if (contains_error(e))
            return e;
    }

    if (!tx_json.isMember ("Sequence"))
        tx_json["Sequence"] = ledgerFacade.getSeq ();

    if (!tx_json.isMember ("Flags"))
        tx_json["Flags"] = tfFullyCanonicalSig;

    if (verify)
    {
        if (!ledgerFacade.hasAccountRoot ())
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
        WriteLog (lsTRACE, RPCHandler) <<
                "verify: " << masterAccountPublic.humanAccountID () <<
                " : " << raSrcAddressID.humanAccountID ();

        auto const secretAccountID = masterAccountPublic.getAccountID();
        if (raSrcAddressID.getAccountID () == secretAccountID)
        {
            if (ledgerFacade.accountMasterDisabled ())
                return rpcError (rpcMASTER_DISABLED);
        }
        else if (!ledgerFacade.accountMatchesRegularKey (secretAccountID))
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
    std::unique_ptr<STObject> sopTrans = std::move(parsed.object);
    sopTrans->setFieldVL (
        sfSigningPubKey,
        masterAccountPublic.getAccountPublic ());

    STTx::pointer stpTrans;

    try
    {
        stpTrans = std::make_shared<STTx> (*sopTrans);
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

    // FIXME: For performance, transactions should not be signed in this code
    // path.
    RippleAddress naAccountPrivate = RippleAddress::createAccountPrivate (
        masterGenerator, secret, 0);

    stpTrans->sign (naAccountPrivate);

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
        // FIXME: For performance, should use asynch interface.
        tpTrans = ledgerFacade.submitTransactionSync (tpTrans,
            role == Role::ADMIN, true, bFailHard, bSubmit);

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

// Struct used to test calls to transactionSign and transactionSubmit.
struct TxnTestData
{
    // Gah, without constexpr I can't make this an enum and initialize
    // OR operators at compile time.  Punting with integer constants.
    static unsigned int const allGood         = 0x0;
    static unsigned int const signFail        = 0x1;
    static unsigned int const submitFail      = 0x2;

    char const* const json;
    unsigned int result;

    TxnTestData () = delete;
    TxnTestData (TxnTestData const&) = delete;
    TxnTestData& operator= (TxnTestData const&) = delete;
    TxnTestData (char const* jsonIn, unsigned int resultIn)
    : json (jsonIn)
    , result (resultIn)
    { }
};

// Declare storage for statics to avoid link errors.
unsigned int const TxnTestData::allGood;
unsigned int const TxnTestData::signFail;
unsigned int const TxnTestData::submitFail;


static TxnTestData const txnTestArray [] =
{

// Minimal payment.
{R"({
    "command": "submit",
    "secret": "masterpassphrase",
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})", TxnTestData::allGood},

// Pass in Fee with minimal payment.
{R"({
    "command": "submit",
    "secret": "masterpassphrase",
    "tx_json": {
        "Fee": 10,
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})", TxnTestData::allGood},

// Pass in Sequence.
{R"({
    "command": "submit",
    "secret": "masterpassphrase",
    "tx_json": {
        "Sequence": 0,
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})", TxnTestData::allGood},

// Pass in Sequence and Fee with minimal payment.
{R"({
    "command": "submit",
    "secret": "masterpassphrase",
    "tx_json": {
        "Sequence": 0,
        "Fee": 10,
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})", TxnTestData::allGood},

// Add "fee_mult_max" field.
{R"({
    "command": "submit",
    "secret": "masterpassphrase",
    "fee_mult_max": 7,
    "tx_json": {
        "Sequence": 0,
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})", TxnTestData::allGood},

// "fee_mult_max is ignored if "Fee" is present.
{R"({
    "command": "submit",
    "secret": "masterpassphrase",
    "fee_mult_max": 0,
    "tx_json": {
        "Sequence": 0,
        "Fee": 10,
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})", TxnTestData::allGood},

// Invalid "fee_mult_max" field.
{R"({
    "command": "submit",
    "secret": "masterpassphrase",
    "fee_mult_max": "NotAFeeMultiplier",
    "tx_json": {
        "Sequence": 0,
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})", TxnTestData::signFail | TxnTestData::submitFail},

// Invalid value for "fee_mult_max" field.
{R"({
    "command": "submit",
    "secret": "masterpassphrase",
    "fee_mult_max": 0,
    "tx_json": {
        "Sequence": 0,
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})", TxnTestData::signFail | TxnTestData::submitFail},

// Missing "Amount".
{R"({
    "command": "submit",
    "secret": "masterpassphrase",
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})", TxnTestData::signFail | TxnTestData::submitFail},

// Invalid "Amount".
{R"({
    "command": "submit",
    "secret": "masterpassphrase",
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "NotAnAmount",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})", TxnTestData::signFail | TxnTestData::submitFail},

// Missing "Destination".
{R"({
    "command": "submit",
    "secret": "masterpassphrase",
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "TransactionType": "Payment"
    }
})", TxnTestData::signFail | TxnTestData::submitFail},

// Invalid "Destination".
{R"({
    "command": "submit",
    "secret": "masterpassphrase",
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "NotADestination",
        "TransactionType": "Payment"
    }
})", TxnTestData::signFail | TxnTestData::submitFail},

// Cannot create XRP to XRP paths.
{R"({
    "command": "submit",
    "secret": "masterpassphrase",
    "build_path": 1,
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})", TxnTestData::signFail | TxnTestData::submitFail},

// Successful "build_path".
{R"({
    "command": "submit",
    "secret": "masterpassphrase",
    "build_path": 1,
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": {
            "value": "10",
            "currency": "USD",
            "issuer": "0123456789012345678901234567890123456789"
        },
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})", TxnTestData::allGood},

// Not valid to include both "Paths" and "build_path".
{R"({
    "command": "submit",
    "secret": "masterpassphrase",
    "build_path": 1,
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": {
            "value": "10",
            "currency": "USD",
            "issuer": "0123456789012345678901234567890123456789"
        },
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "Paths": "",
        "TransactionType": "Payment"
    }
})", TxnTestData::signFail | TxnTestData::submitFail},

// Successful "SendMax".
{R"({
    "command": "submit",
    "secret": "masterpassphrase",
    "build_path": 1,
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": {
            "value": "10",
            "currency": "USD",
            "issuer": "0123456789012345678901234567890123456789"
        },
        "SendMax": {
            "value": "5",
            "currency": "USD",
            "issuer": "0123456789012345678901234567890123456789"
        },
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})", TxnTestData::allGood},

// Even though "Amount" may not be XRP for pathfinding, "SendMax" may be XRP.
{R"({
    "command": "submit",
    "secret": "masterpassphrase",
    "build_path": 1,
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": {
            "value": "10",
            "currency": "USD",
            "issuer": "0123456789012345678901234567890123456789"
        },
        "SendMax": 10000,
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})", TxnTestData::allGood},

// "secret" must be present.
{R"({
    "command": "submit",
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})", TxnTestData::signFail | TxnTestData::submitFail},

// "secret" must be non-empty.
{R"({
    "command": "submit",
    "secret": "",
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})", TxnTestData::signFail | TxnTestData::submitFail},

// "tx_json" must be present.
{R"({
    "command": "submit",
    "secret": "masterpassphrase",
    "rx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})", TxnTestData::signFail | TxnTestData::submitFail},

// "TransactionType" must be present.
{R"({
    "command": "submit",
    "secret": "masterpassphrase",
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
    }
})", TxnTestData::signFail | TxnTestData::submitFail},

// The "TransactionType" must be one of the pre-established transaction types.
{R"({
    "command": "submit",
    "secret": "masterpassphrase",
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "tt"
    }
})", TxnTestData::signFail | TxnTestData::submitFail},

// The "TransactionType", however, may be represented with an integer.
{R"({
    "command": "submit",
    "secret": "masterpassphrase",
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": 0
    }
})", TxnTestData::allGood},

// "Account" must be present.
{R"({
    "command": "submit",
    "secret": "masterpassphrase",
    "tx_json": {
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})", TxnTestData::signFail | TxnTestData::submitFail},

// "Account" must be well formed.
{R"({
    "command": "submit",
    "secret": "masterpassphrase",
    "tx_json": {
        "Account": "NotAnAccount",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})", TxnTestData::signFail | TxnTestData::submitFail},

// The "offline" tag may be added to the transaction.
{R"({
    "command": "submit",
    "secret": "masterpassphrase",
    "offline": 0,
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})", TxnTestData::allGood},

// If "offline" is true then a "Sequence" field must be supplied.
{R"({
    "command": "submit",
    "secret": "masterpassphrase",
    "offline": 1,
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})", TxnTestData::signFail | TxnTestData::submitFail},

// Valid transaction if "offline" is true.
{R"({
    "command": "submit",
    "secret": "masterpassphrase",
    "offline": 1,
    "tx_json": {
        "Sequence": 0,
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})", TxnTestData::allGood},

// A "Flags' field may be specified.
{R"({
    "command": "submit",
    "secret": "masterpassphrase",
    "tx_json": {
        "Flags": 0,
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})", TxnTestData::allGood},

// The "Flags" field must be numeric.
{R"({
    "command": "submit",
    "secret": "masterpassphrase",
    "tx_json": {
        "Flags": "NotGoodFlags",
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})", TxnTestData::signFail | TxnTestData::submitFail},

// It's okay to add a "debug_signing" field.
{R"({
    "command": "submit",
    "secret": "masterpassphrase",
    "debug_signing": 0,
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})", TxnTestData::allGood},

};

class JSONRPC_test : public beast::unit_test::suite
{
public:
    void testAutoFillFees ()
    {
        RippleAddress rootSeedMaster
                = RippleAddress::createSeedGeneric ("masterpassphrase");
        RippleAddress rootGeneratorMaster
                = RippleAddress::createGeneratorPublic (rootSeedMaster);
        RippleAddress rootAddress
                = RippleAddress::createAccountPublic (rootGeneratorMaster, 0);
        std::uint64_t startAmount (100000);
        Ledger::pointer ledger (std::make_shared <Ledger> (
            rootAddress, startAmount));

        using namespace RPCDetail;
        LedgerFacade facade (LedgerFacade::noNetOPs, ledger);

       {
            Json::Value req;
            Json::Value result;
            Json::Reader ().parse (
                R"({ "fee_mult_max" : 1, "tx_json" : { } } )"
                , req);
            autofill_fee (req, facade, result, true);

            expect (! contains_error (result));
        }

        {
            Json::Value req;
            Json::Value result;
            Json::Reader ().parse (
                R"({ "fee_mult_max" : 0, "tx_json" : { } } )"
                , req);
            autofill_fee (req, facade, result, true);

            expect (contains_error (result));
        }
    }

    void testTransactionRPC ()
    {
        // This loop is forward-looking for when there are separate
        // transactionSign () and transcationSubmit () functions.  For now
        // they just have a bool (false = sign, true = submit) and a flag
        // to help classify failure types.
        using TestStuff = std::pair <bool, unsigned int>;
        static TestStuff const testFuncs [] =
        {
            TestStuff {false, TxnTestData::signFail},
            TestStuff {true,  TxnTestData::submitFail},
        };

        for (auto testFunc : testFuncs)
        {
            // For each JSON test.
            for (auto const& txnTest : txnTestArray)
            {
                Json::Value req;
                Json::Reader ().parse (txnTest.json, req);
                if (contains_error (req))
                    throw std::runtime_error (
                        "Internal JSONRPC_test error.  Bad test JSON.");

                static Role const testedRoles[] =
                    {Role::GUEST, Role::USER, Role::ADMIN, Role::FORBID};

                for (Role testRole : testedRoles)
                {
                    // Mock so we can run without a ledger.
                    RPCDetail::LedgerFacade fakeNetOPs (
                        RPCDetail::LedgerFacade::noNetOPs);

                    Json::Value result = transactionSign (
                        req,
                        testFunc.first,
                        true,
                        fakeNetOPs,
                        testRole);

                    expect (contains_error (result) ==
                        static_cast <bool> (txnTest.result & testFunc.second));
                }
            }
        }
    }

    void run ()
    {
        testAutoFillFees ();
        testTransactionRPC ();
    }
};

BEAST_DEFINE_TESTSUITE(JSONRPC,ripple_app,ripple);

} // RPC
} // ripple
