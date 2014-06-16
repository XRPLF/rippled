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

#include <ripple/module/rpc/impl/TransactionSign.h>
#include <beast/unit_test.h>

namespace ripple {

//------------------------------------------------------------------------------


namespace RPC {

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
static void autofill_fee (Json::Value& request,
    Ledger::pointer ledger, Json::Value& result, bool admin)
{
    Json::Value& tx (request["tx_json"]);
    if (tx.isMember ("Fee"))
        return;

    int mult = DEFAULT_AUTO_FILL_FEE_MULTIPLIER;
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

    std::uint64_t const feeDefault = getConfig().FEE_DEFAULT;

    // Administrative endpoints are exempt from local fees
    std::uint64_t const fee = ledger->scaleFeeLoad (feeDefault, admin);
    std::uint64_t const limit = mult * feeDefault;

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
    int role)
{
    RippleAddress dstAccountID;

    if (!tx_json.isMember ("Amount"))
        return RPC::missing_field_error ("tx_json.Amount");

    STAmount amount;

    if (!amount.bSetJson (tx_json ["Amount"]))
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
        uint160     uSrcCurrencyID;
        uint160     uSrcIssuerID;

        STAmount    saSendMax;

        if (tx_json.isMember ("SendMax"))
        {
            if (!saSendMax.bSetJson (tx_json ["SendMax"]))
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
            LegacyPathFind lpf (role == Config::ADMIN);
            if (!lpf.isOk ())
                return rpcError (rpcTOO_BUSY);

            bool bValid;
            auto cache = std::make_shared<RippleLineCache> (lSnapshot);
            Pathfinder pf (cache, raSrcAddressID, dstAccountID,
                           saSendMax.getCurrency (), saSendMax.getIssuer (),
                           amount, bValid);

            STPath extraPath;
            if (!bValid || !pf.findPaths (getConfig ().PATH_SEARCH_OLD, 4, spsPaths, extraPath))
            {
                WriteLog (lsDEBUG, RPCHandler)
                        << "transactionSign: build_path: No paths found.";
                return rpcError (rpcNO_PATH);
            }
            WriteLog (lsDEBUG, RPCHandler)
                    << "transactionSign: build_path: "
                    << spsPaths.getJson (0);

            if (!spsPaths.isEmpty ())
                tx_json["Paths"] = spsPaths.getJson (0);
        }
    }
    return Json::Value();
}

//------------------------------------------------------------------------------

// VFALCO TODO This function should take a reference to the params, modify it
//             as needed, and then there should be a separate function to
//             submit the tranaction
//
Json::Value transactionSign (
    Json::Value params, bool bSubmit, bool bFailHard,
    Application::ScopedLockType& mlh, NetworkOPs& netOps, int role)
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

    // Check for current ledger
    if (verify && !getConfig ().RUN_STANDALONE &&
        (getApp().getLedgerMaster().getValidatedLedgerAge() > 120))
        return rpcError (rpcNO_CURRENT);

    // Check for load
    if (getApp().getFeeTrack().isLoadedCluster() && (role != Config::ADMIN))
        return rpcError(rpcTOO_BUSY);

    Ledger::pointer lSnapshot = netOps.getCurrentLedger ();
    AccountState::pointer asSrc;
    if (verify) {
        asSrc = netOps.getAccountState (lSnapshot, raSrcAddressID);

        if (!asSrc)
        {
            // If not offline and did not find account, error.
            WriteLog (lsDEBUG, RPCHandler)
                    << "transactionSign: Failed to find source account in current ledger: "
                    << raSrcAddressID.humanAccountID ();

            return rpcError (rpcSRC_ACT_NOT_FOUND);
        }
    }

    autofill_fee (params, lSnapshot, jvResult, role == Config::ADMIN);
    if (RPC::contains_error (jvResult))
        return jvResult;

    if ("Payment" == sType)
    {
        auto e = signPayment(params, tx_json, raSrcAddressID, lSnapshot, role);
        if (contains_error(e))
            return e;
    }

    if (!tx_json.isMember ("Fee")) {
        auto const& transactionType = tx_json["TransactionType"].asString ();
        if ("AccountSet" == transactionType
            || "OfferCreate" == transactionType
            || "OfferCancel" == transactionType
            || "TrustSet" == transactionType)
        {
            tx_json["Fee"] = (int) getConfig ().FEE_DEFAULT;
        }
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

    RippleAddress   naSecret = RippleAddress::createSeedGeneric (
        params["secret"].asString ());
    RippleAddress   naMasterGenerator = RippleAddress::createGeneratorPublic (
        naSecret);
    RippleAddress masterAccountPublic = RippleAddress::createAccountPublic (
        naMasterGenerator, 0);

    if (verify)
    {
        auto account = masterAccountPublic.getAccountID();
        auto const& sle = asSrc->peekSLE();

        WriteLog (lsWARNING, RPCHandler) <<
                "verify: " << masterAccountPublic.humanAccountID () <<
                " : " << raSrcAddressID.humanAccountID ();
        if (raSrcAddressID.getAccountID () == account)
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

    STParsedJSON parsed ("tx_json", tx_json);
    if (!parsed.object.get())
    {
        jvResult ["error"] = parsed.error ["error"];
        jvResult ["error_code"] = parsed.error ["error_code"];
        jvResult ["error_message"] = parsed.error ["error_message"];
        return jvResult;
    }
    std::unique_ptr<STObject> sopTrans = std::move(parsed.object);
    sopTrans->setFieldVL (sfSigningPubKey, masterAccountPublic.getAccountPublic ());

    SerializedTransaction::pointer stpTrans;

    try
    {
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

    // FIXME: For performance, transactions should not be signed in this code path.
    RippleAddress naAccountPrivate = RippleAddress::createAccountPrivate (
        naMasterGenerator, naSecret, 0);

    stpTrans->sign (naAccountPrivate);

    Transaction::pointer tpTrans;

    try
    {
        tpTrans     = std::make_shared<Transaction> (stpTrans, false);
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
            role == Config::ADMIN, true, bFailHard, bSubmit);

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

class JSONRPC_test : public beast::unit_test::suite
{
public:
    void testAutoFillFees ()
    {
        RippleAddress rootSeedMaster      = RippleAddress::createSeedGeneric ("masterpassphrase");
        RippleAddress rootGeneratorMaster = RippleAddress::createGeneratorPublic (rootSeedMaster);
        RippleAddress rootAddress         = RippleAddress::createAccountPublic (rootGeneratorMaster, 0);
        std::uint64_t startAmount (100000);
        Ledger::pointer ledger (std::make_shared <Ledger> (
            rootAddress, startAmount));

        {
            Json::Value req;
            Json::Value result;
            Json::Reader ().parse (
                "{ \"fee_mult_max\" : 1, \"tx_json\" : { } } "
                , req);
            autofill_fee (req, ledger, result, true);

            expect (! contains_error (result));
        }

        {
            Json::Value req;
            Json::Value result;
            Json::Reader ().parse (
                "{ \"fee_mult_max\" : 0, \"tx_json\" : { } } "
                , req);
            autofill_fee (req, ledger, result, true);

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
