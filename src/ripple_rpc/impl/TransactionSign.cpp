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

#include "TransactionSign.h"

namespace ripple {

class LegacyPathFind
{
public:

    LegacyPathFind (bool isAdmin) : m_isOkay (false)
    {
        if (isAdmin)
            ++inProgress;
        else
        {
            if ((getApp().getJobQueue ().getJobCountGE (jtCLIENT) > 50) ||
                    getApp().getFeeTrack().isLoadedLocal ())
            return;

            do
            {
                int prevVal = inProgress.load();
                if (prevVal >= maxInProgress)
                    return;

                if (inProgress.compare_exchange_strong (prevVal, prevVal + 1,
                    std::memory_order_release, std::memory_order_relaxed))
                break;
            }
            while (1);
        }

        m_isOkay = true;
    }

    ~LegacyPathFind ()
    {
        if (m_isOkay)
            --inProgress;
    }

    bool isOkay ()
    {
        return m_isOkay;
    }

private:
    static std::atomic <int> inProgress;
    static int maxInProgress;

    bool m_isOkay;
};

std::atomic <int> LegacyPathFind::inProgress (0);
int LegacyPathFind::maxInProgress (2);

//------------------------------------------------------------------------------

// VFALCO TODO Move this to a Tuning.h or Defaults.h
enum
{
    defaultAutoFillFeeMultiplier        = 10
};

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

    int mult (defaultAutoFillFeeMultiplier);
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

    // Administrative endpoints are exempt from local fees
    std::uint64_t fee = ledger->scaleFeeLoad (
        getConfig().FEE_DEFAULT, admin);

    std::uint64_t const limit (mult * getConfig().FEE_DEFAULT);
    if (fee > limit)
    {
        std::stringstream ss;
        ss <<
            "Fee of " << fee <<
            " exceeds the requested tx limit of " << limit;
        RPC::inject_error (rpcHIGH_FEE, ss.str(), result);
        return;
    }

    tx ["Fee"] = (int) fee;
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

    WriteLog (lsDEBUG, RPCHandler) << boost::str (boost::format ("transactionSign: %s") % params);

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

    bool const verify = !(
        params.isMember ("offline") && params["offline"].asBool ());

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
    AccountState::pointer asSrc = !verify
                                  ? AccountState::pointer ()                              // Don't look up address if offline.
                                  : netOps.getAccountState (lSnapshot, raSrcAddressID);

    if (verify && !asSrc)
    {
        // If not offline and did not find account, error.
        WriteLog (lsDEBUG, RPCHandler) << boost::str (boost::format ("transactionSign: Failed to find source account in current ledger: %s")
                                       % raSrcAddressID.humanAccountID ());

        return rpcError (rpcSRC_ACT_NOT_FOUND);
    }

    autofill_fee (params, lSnapshot, jvResult, role == Config::ADMIN);
    if (RPC::contains_error (jvResult))
        return jvResult;

    if ("Payment" == sType)
    {
        RippleAddress dstAccountID;

        if (! tx_json.isMember ("Amount"))
            return RPC::missing_field_error ("tx_json.Amount");

        STAmount amount;

        if (! amount.bSetJson (tx_json ["Amount"]))
            return RPC::invalid_field_error ("tx_json.Amount");

        if (!tx_json.isMember ("Destination"))
            return RPC::missing_field_error ("tx_json.Destination");

        if (!dstAccountID.setAccountID (tx_json["Destination"].asString ()))
            return RPC::invalid_field_error ("tx_json.Destination");

        if (tx_json.isMember ("Paths") && params.isMember ("build_path"))
            return RPC::make_error (rpcINVALID_PARAMS,
                "Cannot specify both 'tx_json.Paths' and 'tx_json.build_path'");

        if (!tx_json.isMember ("Paths") && tx_json.isMember ("Amount") && params.isMember ("build_path"))
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
                saSendMax       = amount;
                saSendMax.setIssuer (raSrcAddressID.getAccountID ());
            }

            if (saSendMax.isNative () && amount.isNative ())
                return RPC::make_error (rpcINVALID_PARAMS,
                    "Cannot build XRP to XRP paths.");

            {
                LegacyPathFind lpf (role == Config::ADMIN);
                if (!lpf.isOkay ())
                    return rpcError (rpcTOO_BUSY);

                bool bValid;
                RippleLineCache::pointer cache = boost::make_shared<RippleLineCache> (lSnapshot);
                Pathfinder pf (cache, raSrcAddressID, dstAccountID,
                               saSendMax.getCurrency (), saSendMax.getIssuer (), amount, bValid);

                STPath extraPath;
                if (!bValid || !pf.findPaths (getConfig ().PATH_SEARCH_OLD, 4, spsPaths, extraPath))
                {
                    WriteLog (lsDEBUG, RPCHandler) << "transactionSign: build_path: No paths found.";

                    return rpcError (rpcNO_PATH);
                }
                else
                {
                    WriteLog (lsDEBUG, RPCHandler) << "transactionSign: build_path: " << spsPaths.getJson (0);
                }

                if (!spsPaths.isEmpty ())
                {
                    tx_json["Paths"] = spsPaths.getJson (0);
                }
            }
        }
    }

    if (!tx_json.isMember ("Fee")
            && (
                "AccountSet" == tx_json["TransactionType"].asString ()
                || "OfferCreate" == tx_json["TransactionType"].asString ()
                || "OfferCancel" == tx_json["TransactionType"].asString ()
                || "TrustSet" == tx_json["TransactionType"].asString ()))
    {
        tx_json["Fee"] = (int) getConfig ().FEE_DEFAULT;
    }

    if (!tx_json.isMember ("Sequence"))
    {
        if (!verify)
        {
            // If offline, Sequence is mandatory.
            // TODO: duplicates logic above.
            return rpcError (rpcINVALID_PARAMS);
        }
        else
        {
            tx_json["Sequence"] = asSrc->getSeq ();
        }
    }

    if (!tx_json.isMember ("Flags")) tx_json["Flags"] = tfFullyCanonicalSig;

    if (verify)
    {
        SLE::pointer sleAccountRoot = netOps.getSLEi (lSnapshot,
            Ledger::getAccountRootIndex (raSrcAddressID.getAccountID ()));

        if (!sleAccountRoot)
        {
            // XXX Ignore transactions for accounts not created.
            return rpcError (rpcSRC_ACT_NOT_FOUND);
        }
    }

    bool            bHaveAuthKey    = false;
    RippleAddress   naAuthorizedPublic;

    RippleAddress   naSecret = RippleAddress::createSeedGeneric (
        params["secret"].asString ());
    RippleAddress   naMasterGenerator = RippleAddress::createGeneratorPublic (
        naSecret);

    if (verify)
    {
        auto masterAccountPublic = RippleAddress::createAccountPublic (
            naMasterGenerator, 0);
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

    // Use the generator to determine the associated public and private keys.
    RippleAddress naGenerator = RippleAddress::createGeneratorPublic (
        naSecret);
    RippleAddress naAccountPublic = RippleAddress::createAccountPublic (
        naGenerator, 0);
    RippleAddress naAccountPrivate = RippleAddress::createAccountPrivate (
        naGenerator, naSecret, 0);

    if (bHaveAuthKey
            // The generated pair must match authorized...
            && naAuthorizedPublic.getAccountID () != naAccountPublic.getAccountID ()
            // ... or the master key must have been used.
            && raSrcAddressID.getAccountID () != naAccountPublic.getAccountID ())
    {
        // TODO: we can't ever get here!
        // Log::out() << "iIndex: " << iIndex;
        // Log::out() << "sfAuthorizedKey: " << strHex(asSrc->getAuthorizedKey().getAccountID());
        // Log::out() << "naAccountPublic: " << strHex(naAccountPublic.getAccountID());

        return rpcError (rpcSRC_ACT_NOT_FOUND);
    }

    std::unique_ptr<STObject> sopTrans;

    {
        STParsedJSON parsed ("tx_json", tx_json);
        if (parsed.object.get() != nullptr)
        {
            // VFALCO NOTE No idea why this doesn't compile.
            //sopTrans = parsed.object;
            sopTrans.reset (parsed.object.release());
        }
        else
        {
            jvResult ["error"] = parsed.error ["error"];
            jvResult ["error_code"] = parsed.error ["error_code"];
            jvResult ["error_message"] = parsed.error ["error_message"];
            return jvResult;
        }
    }

    sopTrans->setFieldVL (sfSigningPubKey, naAccountPublic.getAccountPublic ());

    SerializedTransaction::pointer stpTrans;

    try
    {
        stpTrans = boost::make_shared<SerializedTransaction> (*sopTrans);
    }
    catch (std::exception&)
    {
        return RPC::make_error (rpcINTERNAL,
            "Exception occurred during transaction");
    }

    std::string reason;
    if (!passesLocalChecks (*stpTrans, reason))
    {
        return RPC::make_error (rpcINVALID_PARAMS,
            reason);
    }

    if (params.isMember ("debug_signing"))
    {
        jvResult["tx_unsigned"] = strHex (
            stpTrans->getSerializer ().peekData ());
        jvResult["tx_signing_hash"] = stpTrans->getSigningHash ().ToString ();
    }

    // FIXME: For performance, transactions should not be signed in this code path.
    stpTrans->sign (naAccountPrivate);

    Transaction::pointer tpTrans;

    try
    {
        tpTrans     = boost::make_shared<Transaction> (stpTrans, false);
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
        Ledger::pointer ledger (boost::make_shared <Ledger> (
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
