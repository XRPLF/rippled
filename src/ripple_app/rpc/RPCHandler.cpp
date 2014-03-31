//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#include "../../beast/beast/unit_test/suite.h"

namespace ripple {

//
// Carries out the RPC.
//

SETUP_LOG (RPCHandler)

RPCHandler::RPCHandler (NetworkOPs* netOps)
    : mNetOps (netOps)
    , mRole (Config::FORBID)
{
}

RPCHandler::RPCHandler (NetworkOPs* netOps, InfoSub::pointer infoSub)
    : mNetOps (netOps)
    , mInfoSub (infoSub)
    , mRole (Config::FORBID)
{
}

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
Json::Value RPCHandler::transactionSign (Json::Value params,
    bool bSubmit, bool bFailHard, Application::ScopedLockType& mlh)
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

    bool const bOffline (
        params.isMember ("offline") && params["offline"].asBool ());

    if (! tx_json.isMember ("Sequence") && bOffline)
        return RPC::missing_field_error ("tx_json.Sequence");

    // Check for current ledger
    if (!bOffline && !getConfig ().RUN_STANDALONE &&
        (getApp().getLedgerMaster().getValidatedLedgerAge() > 120))
        return rpcError (rpcNO_CURRENT);

    // Check for load
    if (getApp().getFeeTrack().isLoadedCluster() && (mRole != Config::ADMIN))
        return rpcError(rpcTOO_BUSY);

    Ledger::pointer lSnapshot = mNetOps->getCurrentLedger ();
    AccountState::pointer asSrc = bOffline
                                  ? AccountState::pointer ()                              // Don't look up address if offline.
                                  : mNetOps->getAccountState (lSnapshot, raSrcAddressID);

    if (!bOffline && !asSrc)
    {
        // If not offline and did not find account, error.
        WriteLog (lsDEBUG, RPCHandler) << boost::str (boost::format ("transactionSign: Failed to find source account in current ledger: %s")
                                       % raSrcAddressID.humanAccountID ());

        return rpcError (rpcSRC_ACT_NOT_FOUND);
    }

    autofill_fee (params, lSnapshot, jvResult, mRole == Config::ADMIN);
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
                LegacyPathFind lpf (mRole == Config::ADMIN);
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
        if (bOffline)
        {
            // If offline, Sequence is mandatory.
            return rpcError (rpcINVALID_PARAMS);
        }
        else
        {
            tx_json["Sequence"] = asSrc->getSeq ();
        }
    }

    if (!tx_json.isMember ("Flags")) tx_json["Flags"] = tfFullyCanonicalSig;

    if (!bOffline)
    {
        SLE::pointer sleAccountRoot = mNetOps->getSLEi (lSnapshot, 
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

    // Find the index of Account from the master generator, so we can generate
    // the public and private keys.
    RippleAddress naMasterAccountPublic;
    unsigned int iIndex  = 0;
    bool bFound  = false;

    // Don't look at ledger entries to determine if the account exists.
    // Don't want to leak to thin server that these accounts are related.
    while (!bFound && iIndex != getConfig ().ACCOUNT_PROBE_MAX)
    {
        naMasterAccountPublic.setAccountPublic (naMasterGenerator, iIndex);

        WriteLog (lsWARNING, RPCHandler) <<
            "authorize: " << iIndex <<
            " : " << naMasterAccountPublic.humanAccountID () <<
            " : " << raSrcAddressID.humanAccountID ();

        bFound = raSrcAddressID.getAccountID () == naMasterAccountPublic.getAccountID ();

        if (!bFound)
            ++iIndex;
    }

    if (!bFound)
    {
        return rpcError (rpcBAD_SECRET);
    }

    // Use the generator to determine the associated public and private keys.
    RippleAddress naGenerator = RippleAddress::createGeneratorPublic (
        naSecret);
    RippleAddress naAccountPublic = RippleAddress::createAccountPublic (
        naGenerator, iIndex);
    RippleAddress naAccountPrivate = RippleAddress::createAccountPrivate (
        naGenerator, naSecret, iIndex);

    if (bHaveAuthKey
            // The generated pair must match authorized...
            && naAuthorizedPublic.getAccountID () != naAccountPublic.getAccountID ()
            // ... or the master key must have been used.
            && raSrcAddressID.getAccountID () != naAccountPublic.getAccountID ())
    {
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
        tpTrans = mNetOps->submitTransactionSync (tpTrans, 
            mRole == Config::ADMIN, true, bFailHard, bSubmit);

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

// Look up the master public generator for a regular seed so we may index source accounts ids.
// --> naRegularSeed
// <-- naMasterGenerator
Json::Value RPCHandler::getMasterGenerator (Ledger::ref lrLedger, const RippleAddress& naRegularSeed, RippleAddress& naMasterGenerator)
{
    RippleAddress       na0Public;      // To find the generator's index.
    RippleAddress       na0Private;     // To decrypt the master generator's cipher.
    RippleAddress       naGenerator = RippleAddress::createGeneratorPublic (naRegularSeed);

    na0Public.setAccountPublic (naGenerator, 0);
    na0Private.setAccountPrivate (naGenerator, naRegularSeed, 0);

    SLE::pointer        sleGen          = mNetOps->getGenerator (lrLedger, na0Public.getAccountID ());

    if (!sleGen)
    {
        // No account has been claimed or has had it password set for seed.
        return rpcError (rpcNO_ACCOUNT);
    }

    Blob    vucCipher           = sleGen->getFieldVL (sfGenerator);
    Blob    vucMasterGenerator  = na0Private.accountPrivateDecrypt (na0Public, vucCipher);

    if (vucMasterGenerator.empty ())
    {
        return rpcError (rpcFAIL_GEN_DECRYPT);
    }

    naMasterGenerator.setGenerator (vucMasterGenerator);

    return Json::Value (Json::objectValue);
}

// Given a seed and a source account get the regular public and private key for authorizing transactions.
// - Make sure the source account can pay.
// --> naRegularSeed : To find the generator
// --> naSrcAccountID : Account we want the public and private regular keys to.
// <-- naAccountPublic : Regular public key for naSrcAccountID
// <-- naAccountPrivate : Regular private key for naSrcAccountID
// <-- saSrcBalance: Balance minus fee.
// --> naVerifyGenerator : If provided, the found master public generator must match.
// XXX Be more lenient, allow use of master generator on claimed accounts.
Json::Value RPCHandler::authorize (Ledger::ref lrLedger,
                                   const RippleAddress& naRegularSeed, const RippleAddress& naSrcAccountID,
                                   RippleAddress& naAccountPublic, RippleAddress& naAccountPrivate,
                                   STAmount& saSrcBalance, const STAmount& saFee, AccountState::pointer& asSrc,
                                   const RippleAddress& naVerifyGenerator)
{
    // Source/paying account must exist.
    asSrc   = mNetOps->getAccountState (lrLedger, naSrcAccountID);

    if (!asSrc)
    {
        return rpcError (rpcSRC_ACT_NOT_FOUND);
    }

    RippleAddress   naMasterGenerator;

    if (asSrc->haveAuthorizedKey ())
    {
        Json::Value obj = getMasterGenerator (lrLedger, naRegularSeed, naMasterGenerator);

        if (!obj.empty ())
            return obj;
    }
    else
    {
        // Try the seed as a master seed.
        naMasterGenerator   = RippleAddress::createGeneratorPublic (naRegularSeed);
    }

    // If naVerifyGenerator is provided, make sure it is the master generator.
    if (naVerifyGenerator.isValid () && naMasterGenerator != naVerifyGenerator)
    {
        return rpcError (rpcWRONG_SEED);
    }

    // Find the index of the account from the master generator, so we can generate the public and private keys.
    RippleAddress       naMasterAccountPublic;
    unsigned int        iIndex  = 0;
    bool                bFound  = false;

    // Don't look at ledger entries to determine if the account exists.  Don't want to leak to thin server that these accounts are
    // related.
    while (!bFound && iIndex != getConfig ().ACCOUNT_PROBE_MAX)
    {
        naMasterAccountPublic.setAccountPublic (naMasterGenerator, iIndex);

        WriteLog (lsDEBUG, RPCHandler) << "authorize: " << iIndex << " : " << naMasterAccountPublic.humanAccountID () << " : " << naSrcAccountID.humanAccountID ();

        bFound  = naSrcAccountID.getAccountID () == naMasterAccountPublic.getAccountID ();

        if (!bFound)
            ++iIndex;
    }

    if (!bFound)
    {
        return rpcError (rpcACT_NOT_FOUND);
    }

    // Use the regular generator to determine the associated public and private keys.
    RippleAddress       naGenerator = RippleAddress::createGeneratorPublic (naRegularSeed);

    naAccountPublic.setAccountPublic (naGenerator, iIndex);
    naAccountPrivate.setAccountPrivate (naGenerator, naRegularSeed, iIndex);

    if (asSrc->haveAuthorizedKey () && (asSrc->getAuthorizedKey ().getAccountID () != naAccountPublic.getAccountID ()))
    {
        // Log::out() << "iIndex: " << iIndex;
        // Log::out() << "sfAuthorizedKey: " << strHex(asSrc->getAuthorizedKey().getAccountID());
        // Log::out() << "naAccountPublic: " << strHex(naAccountPublic.getAccountID());

        return rpcError (rpcPASSWD_CHANGED);
    }

    saSrcBalance    = asSrc->getBalance ();

    if (saSrcBalance < saFee)
    {
        WriteLog (lsINFO, RPCHandler) << "authorize: Insufficient funds for fees: fee=" << saFee.getText () << " balance=" << saSrcBalance.getText ();

        return rpcError (rpcINSUF_FUNDS);
    }
    else
    {
        saSrcBalance -= saFee;
    }

    return Json::Value ();
}

// --> strIdent: public key, account ID, or regular seed.
// --> bStrict: Only allow account id or public key.
// <-- bIndex: true if iIndex > 0 and used the index.
Json::Value RPCHandler::accountFromString (Ledger::ref lrLedger, RippleAddress& naAccount, bool& bIndex, const std::string& strIdent, const int iIndex, const bool bStrict)
{
    RippleAddress   naSeed;

    if (naAccount.setAccountPublic (strIdent) || naAccount.setAccountID (strIdent))
    {
        // Got the account.
        bIndex  = false;
    }
    else if (bStrict)
    {
        return naAccount.setAccountID (strIdent, Base58::getBitcoinAlphabet ())
               ? rpcError (rpcACT_BITCOIN)
               : rpcError (rpcACT_MALFORMED);
    }
    // Must be a seed.
    else if (!naSeed.setSeedGeneric (strIdent))
    {
        return rpcError (rpcBAD_SEED);
    }
    else
    {
        // We allow the use of the seeds to access #0.
        // This is poor practice and merely for debuging convenience.
        RippleAddress       naRegular0Public;
        RippleAddress       naRegular0Private;

        RippleAddress       naGenerator     = RippleAddress::createGeneratorPublic (naSeed);

        naRegular0Public.setAccountPublic (naGenerator, 0);
        naRegular0Private.setAccountPrivate (naGenerator, naSeed, 0);

        //      uint160             uGeneratorID    = naRegular0Public.getAccountID();
        SLE::pointer        sleGen          = mNetOps->getGenerator (lrLedger, naRegular0Public.getAccountID ());

        if (!sleGen)
        {
            // Didn't find a generator map, assume it is a master generator.
            nothing ();
        }
        else
        {
            // Found master public key.
            Blob    vucCipher               = sleGen->getFieldVL (sfGenerator);
            Blob    vucMasterGenerator      = naRegular0Private.accountPrivateDecrypt (naRegular0Public, vucCipher);

            if (vucMasterGenerator.empty ())
            {
                rpcError (rpcNO_GEN_DECRYPT);
            }

            naGenerator.setGenerator (vucMasterGenerator);
        }

        bIndex  = !iIndex;

        naAccount.setAccountPublic (naGenerator, iIndex);
    }

    return Json::Value (Json::objectValue);
}

Json::Value RPCHandler::doAccountCurrencies (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& masterLockHolder)
{
    masterLockHolder.unlock ();
    // Get the current ledger
    Ledger::pointer lpLedger;
    Json::Value jvResult (lookupLedger (params, lpLedger));
    if (!lpLedger)
        return jvResult;

    if (! params.isMember ("account") && ! params.isMember ("ident"))
        return RPC::missing_field_error ("account");

    std::string const strIdent (params.isMember ("account")
        ? params["account"].asString ()
        : params["ident"].asString ());

    int const iIndex (params.isMember ("account_index")
        ? params["account_index"].asUInt ()
        : 0);
    bool const bStrict (params.isMember ("strict") && params["strict"].asBool ());

    // Get info on account.
    bool bIndex; // out param
    RippleAddress naAccount; // out param
    Json::Value jvAccepted (accountFromString (
        lpLedger, naAccount, bIndex, strIdent, iIndex, bStrict));

    if (!jvAccepted.empty ())
        return jvAccepted;

    std::set<uint160> send, receive;
    AccountItems rippleLines (naAccount.getAccountID (), lpLedger, AccountItem::pointer (new RippleState ()));
    BOOST_FOREACH(AccountItem::ref item, rippleLines.getItems ())
    {
        RippleState* rspEntry = (RippleState*) item.get ();
        const STAmount& saBalance = rspEntry->getBalance ();

        if (saBalance < rspEntry->getLimit ())
            receive.insert (saBalance.getCurrency ());
        if ((-saBalance) < rspEntry->getLimitPeer ())
            send.insert (saBalance.getCurrency ());
    }


    send.erase (CURRENCY_BAD);
    receive.erase (CURRENCY_BAD);

    Json::Value& sendCurrencies = (jvResult["send_currencies"] = Json::arrayValue);
    BOOST_FOREACH(uint160 const& c, send)
    {
        sendCurrencies.append (STAmount::createHumanCurrency (c));
    }

    Json::Value& recvCurrencies = (jvResult["receive_currencies"] = Json::arrayValue);
    BOOST_FOREACH(uint160 const& c, receive)
    {
        recvCurrencies.append (STAmount::createHumanCurrency (c));
    }
    

    return jvResult;
}

// {
//   account: <indent>,
//   account_index : <index> // optional
//   strict: <bool>                 // true, only allow public keys and addresses. false, default.
//   ledger_hash : <ledger>
//   ledger_index : <ledger_index>
// }
Json::Value RPCHandler::doAccountInfo (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& masterLockHolder)
{
    masterLockHolder.unlock ();

    Ledger::pointer     lpLedger;
    Json::Value         jvResult    = lookupLedger (params, lpLedger);

    if (!lpLedger)
        return jvResult;

    if (!params.isMember ("account") && !params.isMember ("ident"))
        return RPC::missing_field_error ("account");

    std::string     strIdent    = params.isMember ("account") ? params["account"].asString () : params["ident"].asString ();
    bool            bIndex;
    int             iIndex      = params.isMember ("account_index") ? params["account_index"].asUInt () : 0;
    bool            bStrict     = params.isMember ("strict") && params["strict"].asBool ();
    RippleAddress   naAccount;

    // Get info on account.

    Json::Value     jvAccepted      = accountFromString (lpLedger, naAccount, bIndex, strIdent, iIndex, bStrict);

    if (!jvAccepted.empty ())
        return jvAccepted;

    AccountState::pointer asAccepted    = mNetOps->getAccountState (lpLedger, naAccount);

    if (asAccepted)
    {
        asAccepted->addJson (jvAccepted);

        jvResult["account_data"]    = jvAccepted;
    }
    else
    {
        jvResult["account"] = naAccount.humanAccountID ();
        jvResult            = rpcError (rpcACT_NOT_FOUND, jvResult);
    }

    return jvResult;
}

Json::Value RPCHandler::doBlackList (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& masterLockHolder)
{
    masterLockHolder.unlock();
    if (params.isMember("threshold"))
        return getApp().getResourceManager().getJson(params["threshold"].asInt());
    else
        return getApp().getResourceManager().getJson();
}

// {
//   ip: <string>,
//   port: <number>
// }
// XXX Might allow domain for manual connections.
Json::Value RPCHandler::doConnect (Json::Value params,
                                   Resource::Charge& loadType,
                                   Application::ScopedLockType& masterLockHolder)
{
    if (getConfig ().RUN_STANDALONE)
        return "cannot connect in standalone mode";

    if (!params.isMember ("ip"))
        return RPC::missing_field_error ("ip");

    if (params.isMember ("port") && !params["port"].isConvertibleTo (Json::intValue))
        return rpcError (rpcINVALID_PARAMS);

    int iPort;

    if(params.isMember ("port"))
        iPort = params["port"].asInt ();
    else
        iPort = SYSTEM_PEER_PORT;

    beast::IP::Endpoint ip (beast::IP::Endpoint::from_string(params["ip"].asString ()));

    if (! is_unspecified (ip))
        getApp().getPeers ().connect (ip.at_port(iPort));

    return "connecting";
}

#if ENABLE_INSECURE
// {
//   key: <string>
// }
Json::Value RPCHandler::doDataDelete (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& masterLockHolder)
{
    if (!params.isMember ("key"))
        return RPC::missing_field_error ("key");

    std::string strKey = params["key"].asString ();

    Json::Value ret = Json::Value (Json::objectValue);

    if (getApp().getLocalCredentials ().dataDelete (strKey))
    {
        ret["key"]      = strKey;
    }
    else
    {
        ret = rpcError (rpcINTERNAL);
    }

    return ret;
}
#endif

#if ENABLE_INSECURE
// {
//   key: <string>
// }
Json::Value RPCHandler::doDataFetch (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& masterLockHolder)
{
    if (!params.isMember ("key"))
        return RPC::missing_field_error ("key");

    std::string strKey = params["key"].asString ();
    std::string strValue;

    Json::Value ret = Json::Value (Json::objectValue);

    ret["key"]      = strKey;

    if (getApp().getLocalCredentials ().dataFetch (strKey, strValue))
        ret["value"]    = strValue;

    return ret;
}
#endif

#if ENABLE_INSECURE
// {
//   key: <string>
//   value: <string>
// }
Json::Value RPCHandler::doDataStore (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& masterLockHolder)
{
    if (!params.isMember ("key")
        return RPC::missing_field_error ("key");
    if (!params.isMember ("value")
        return RPC::missing_field_error ("value");

    std::string strKey      = params["key"].asString ();
    std::string strValue    = params["value"].asString ();

    Json::Value ret = Json::Value (Json::objectValue);

    if (getApp().getLocalCredentials ().dataStore (strKey, strValue))
    {
        ret["key"]      = strKey;
        ret["value"]    = strValue;
    }
    else
    {
        ret = rpcError (rpcINTERNAL);
    }

    return ret;
}
#endif

#if 0
// XXX Needs to be revised for new paradigm
// nickname_info <nickname>
// Note: Nicknames are not automatically looked up by commands as they are advisory and can be changed.
Json::Value RPCHandler::doNicknameInfo (Json::Value params)
{
    std::string strNickname = params[0u].asString ();
    boost::trim (strNickname);

    if (strNickname.empty ())
    {
        return rpcError (rpcNICKNAME_MALFORMED);
    }

    NicknameState::pointer  nsSrc   = mNetOps->getNicknameState (uint256 (0), strNickname);

    if (!nsSrc)
    {
        return rpcError (rpcNICKNAME_MISSING);
    }

    Json::Value ret (Json::objectValue);

    ret["nickname"] = strNickname;

    nsSrc->addJson (ret);

    return ret;
}
#endif

// {
//   'ident' : <indent>,
//   'account_index' : <index> // optional
// }
// XXX This would be better if it took the ledger.
Json::Value RPCHandler::doOwnerInfo (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& masterLockHolder)
{
    if (!params.isMember ("account") && !params.isMember ("ident"))
        return RPC::missing_field_error ("account");

    std::string     strIdent    = params.isMember ("account") ? params["account"].asString () : params["ident"].asString ();
    bool            bIndex;
    int             iIndex      = params.isMember ("account_index") ? params["account_index"].asUInt () : 0;
    RippleAddress   raAccount;

    Json::Value     ret;

    // Get info on account.

    Json::Value     jAccepted   = accountFromString (mNetOps->getClosedLedger (), raAccount, bIndex, strIdent, iIndex, false);

    ret["accepted"] = jAccepted.empty () ? mNetOps->getOwnerInfo (mNetOps->getClosedLedger (), raAccount) : jAccepted;

    Json::Value     jCurrent    = accountFromString (mNetOps->getCurrentLedger (), raAccount, bIndex, strIdent, iIndex, false);

    ret["current"]  = jCurrent.empty () ? mNetOps->getOwnerInfo (mNetOps->getCurrentLedger (), raAccount) : jCurrent;

    return ret;
}

Json::Value RPCHandler::doPeers (Json::Value, Resource::Charge& loadType, Application::ScopedLockType& masterLockHolder)
{
    Json::Value jvResult (Json::objectValue);

    jvResult["peers"]   = getApp().getPeers ().json ();

    getApp().getUNL().addClusterStatus(jvResult);

    return jvResult;
}

Json::Value RPCHandler::doPing (Json::Value, Resource::Charge& loadType, Application::ScopedLockType& masterLockHolder)
{
    return Json::Value (Json::objectValue);
}

Json::Value RPCHandler::doPrint (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& masterLockHolder)
{
    masterLockHolder.unlock ();

    JsonPropertyStream stream;
    if (params.isObject() && params["params"].isArray() && params["params"][0u].isString ())
        getApp().write (stream, params["params"][0u].asString());
    else
        getApp().write (stream);

    return stream.top();
}

// profile offers <pass_a> <account_a> <currency_offer_a> <account_b> <currency_offer_b> <count> [submit]
// profile 0:offers 1:pass_a 2:account_a 3:currency_offer_a 4:account_b 5:currency_offer_b 6:<count> 7:[submit]
// issuer is the offering account
// --> submit: 'submit|true|false': defaults to false
// Prior to running allow each to have a credit line of what they will be getting from the other account.
Json::Value RPCHandler::doProfile (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& masterLockHolder)
{
    /* need to fix now that sharedOfferCreate is gone
    int             iArgs   = params.size();
    RippleAddress   naSeedA;
    RippleAddress   naAccountA;
    uint160         uCurrencyOfferA;
    RippleAddress   naSeedB;
    RippleAddress   naAccountB;
    uint160         uCurrencyOfferB;
    uint32          iCount  = 100;
    bool            bSubmit = false;

    if (iArgs < 6 || "offers" != params[0u].asString())
    {
        return rpcError(rpcINVALID_PARAMS);
    }

    if (!naSeedA.setSeedGeneric(params[1u].asString()))                          // <pass_a>
        return rpcError(rpcINVALID_PARAMS);

    naAccountA.setAccountID(params[2u].asString());                              // <account_a>

    if (!STAmount::currencyFromString(uCurrencyOfferA, params[3u].asString()))   // <currency_offer_a>
        return rpcError(rpcINVALID_PARAMS);

    naAccountB.setAccountID(params[4u].asString());                              // <account_b>
    if (!STAmount::currencyFromString(uCurrencyOfferB, params[5u].asString()))   // <currency_offer_b>
        return rpcError(rpcINVALID_PARAMS);

    iCount  = lexicalCast <uint32>(params[6u].asString());

    if (iArgs >= 8 && "false" != params[7u].asString())
        bSubmit = true;

    LogSink::get()->setMinSeverity(lsFATAL,true);

    boost::posix_time::ptime            ptStart(boost::posix_time::microsec_clock::local_time());

    for(unsigned int n=0; n<iCount; n++)
    {
        RippleAddress           naMasterGeneratorA;
        RippleAddress           naAccountPublicA;
        RippleAddress           naAccountPrivateA;
        AccountState::pointer   asSrcA;
        STAmount                saSrcBalanceA;

        Json::Value             jvObjA      = authorize(uint256(0), naSeedA, naAccountA, naAccountPublicA, naAccountPrivateA,
            saSrcBalanceA, getConfig ().FEE_DEFAULT, asSrcA, naMasterGeneratorA);

        if (!jvObjA.empty())
            return jvObjA;

        Transaction::pointer    tpOfferA    = Transaction::sharedOfferCreate(
            naAccountPublicA, naAccountPrivateA,
            naAccountA,                                                 // naSourceAccount,
            asSrcA->getSeq(),                                           // uSeq
            getConfig ().FEE_DEFAULT,
            0,                                                          // uSourceTag,
            false,                                                      // bPassive
            STAmount(uCurrencyOfferA, naAccountA.getAccountID(), 1),    // saTakerPays
            STAmount(uCurrencyOfferB, naAccountB.getAccountID(), 1+n),  // saTakerGets
            0);                                                         // uExpiration

        if (bSubmit)
            tpOfferA    = mNetOps->submitTransactionSync(tpOfferA); // FIXME: Don't use synch interface
    }

    boost::posix_time::ptime            ptEnd(boost::posix_time::microsec_clock::local_time());
    boost::posix_time::time_duration    tdInterval      = ptEnd-ptStart;
    long                                lMicroseconds   = tdInterval.total_microseconds();
    int                                 iTransactions   = iCount;
    float                               fRate           = lMicroseconds ? iTransactions/(lMicroseconds/1000000.0) : 0.0;

    Json::Value obj(Json::objectValue);

    obj["transactions"]     = iTransactions;
    obj["submit"]           = bSubmit;
    obj["start"]            = boost::posix_time::to_simple_string(ptStart);
    obj["end"]              = boost::posix_time::to_simple_string(ptEnd);
    obj["interval"]         = boost::posix_time::to_simple_string(tdInterval);
    obj["rate_per_second"]  = fRate;
    */
    Json::Value obj (Json::objectValue);
    return obj;
}

// {
//   // if either of these parameters is set, a custom generator is used
//   difficulty: <number>       // optional
//   secret: <secret>           // optional
// }
Json::Value RPCHandler::doProofCreate (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& masterLockHolder)
{
    masterLockHolder.unlock ();
    // XXX: Add ability to create proof with arbitrary time

    Json::Value     jvResult (Json::objectValue);

    if (params.isMember ("difficulty") || params.isMember ("secret"))
    {
        // VFALCO TODO why aren't we using the app's factory?
        std::unique_ptr <ProofOfWorkFactory> pgGen (ProofOfWorkFactory::New ());

        if (params.isMember ("difficulty"))
        {
            if (!params["difficulty"].isIntegral ())
                return RPC::invalid_field_error ("difficulty");

            int const iDifficulty (params["difficulty"].asInt ());

            if (iDifficulty < 0 || iDifficulty > ProofOfWorkFactory::kMaxDifficulty)
                return RPC::invalid_field_error ("difficulty");

            pgGen->setDifficulty (iDifficulty);
        }

        if (params.isMember ("secret"))
        {
            uint256     uSecret (params["secret"].asString ());
            pgGen->setSecret (uSecret);
        }

        jvResult["token"]   = pgGen->getProof ().getToken ();
        jvResult["secret"]  = pgGen->getSecret ().GetHex ();
    }
    else
    {
        jvResult["token"]   = getApp().getProofOfWorkFactory ().getProof ().getToken ();
    }

    return jvResult;
}

// {
//   token: <token>
// }
Json::Value RPCHandler::doProofSolve (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& masterLockHolder)
{
    masterLockHolder.unlock ();

    Json::Value         jvResult;

    if (!params.isMember ("token"))
        return RPC::missing_field_error ("token");

    std::string         strToken        = params["token"].asString ();

    if (!ProofOfWork::validateToken (strToken))
        return RPC::invalid_field_error ("token");

    ProofOfWork         powProof (strToken);
    uint256             uSolution       = powProof.solve ();

    jvResult["solution"]                = uSolution.GetHex ();

    return jvResult;
}


// {
//   token: <token>
//   solution: <solution>
//   // if either of these parameters is set, a custom verifier is used
//   difficulty: <number>       // optional
//   secret: <secret>           // optional
// }
Json::Value RPCHandler::doProofVerify (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& masterLockHolder)
{
    masterLockHolder.unlock ();
    // XXX Add ability to check proof against arbitrary time

    Json::Value         jvResult;

    if (!params.isMember ("token"))
        return RPC::missing_field_error ("token");

    if (!params.isMember ("solution"))
        return RPC::missing_field_error ("solution");

    std::string     strToken    = params["token"].asString ();
    uint256         uSolution (params["solution"].asString ());

    PowResult       prResult;

    if (params.isMember ("difficulty") || params.isMember ("secret"))
    {
        // VFALCO TODO why aren't we using the app's factory?
        std::unique_ptr <ProofOfWorkFactory> pgGen (ProofOfWorkFactory::New ());

        if (params.isMember ("difficulty"))
        {
            if (!params["difficulty"].isIntegral ())
                return RPC::invalid_field_error ("difficulty");

            int iDifficulty = params["difficulty"].asInt ();

            if (iDifficulty < 0 || iDifficulty > ProofOfWorkFactory::kMaxDifficulty)
                return RPC::missing_field_error ("difficulty");

            pgGen->setDifficulty (iDifficulty);
        }

        if (params.isMember ("secret"))
        {
            uint256     uSecret (params["secret"].asString ());
            pgGen->setSecret (uSecret);
        }

        prResult                = pgGen->checkProof (strToken, uSolution);

        jvResult["secret"]      = pgGen->getSecret ().GetHex ();
    }
    else
    {
        // XXX Proof should not be marked as used from this
        prResult = getApp().getProofOfWorkFactory ().checkProof (strToken, uSolution);
    }

    std::string sToken;
    std::string sHuman;

    ProofOfWork::calcResultInfo (prResult, sToken, sHuman);

    jvResult["proof_result"]            = sToken;
    jvResult["proof_result_code"]       = prResult;
    jvResult["proof_result_message"]    = sHuman;

    return jvResult;
}

// {
//   account: <account>|<nickname>|<account_public_key>
//   account_index: <number>        // optional, defaults to 0.
//   ledger_hash : <ledger>
//   ledger_index : <ledger_index>
// }
Json::Value RPCHandler::doAccountLines (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& masterLockHolder)
{
    masterLockHolder.unlock ();

    Ledger::pointer     lpLedger;
    Json::Value         jvResult    = lookupLedger (params, lpLedger);

    if (!lpLedger)
        return jvResult;

    if (!params.isMember ("account"))
        return RPC::missing_field_error ("account");

    std::string     strIdent    = params["account"].asString ();
    bool            bIndex      = params.isMember ("account_index");
    int             iIndex      = bIndex ? params["account_index"].asUInt () : 0;

    RippleAddress   raAccount;

    jvResult    = accountFromString (lpLedger, raAccount, bIndex, strIdent, iIndex, false);

    if (!jvResult.empty ())
        return jvResult;

    std::string     strPeer     = params.isMember ("peer") ? params["peer"].asString () : "";
    bool            bPeerIndex      = params.isMember ("peer_index");
    int             iPeerIndex      = bIndex ? params["peer_index"].asUInt () : 0;

    RippleAddress   raPeer;

    if (!strPeer.empty ())
    {
        jvResult["peer"]    = raAccount.humanAccountID ();

        if (bPeerIndex)
            jvResult["peer_index"]  = iPeerIndex;

        jvResult    = accountFromString (lpLedger, raPeer, bPeerIndex, strPeer, iPeerIndex, false);

        if (!jvResult.empty ())
            return jvResult;
    }

    if (lpLedger->hasAccount (raAccount))
    {
        AccountItems rippleLines (raAccount.getAccountID (), lpLedger, AccountItem::pointer (new RippleState ()));

        jvResult["account"] = raAccount.humanAccountID ();
        Json::Value& jsonLines = (jvResult["lines"] = Json::arrayValue);


        BOOST_FOREACH (AccountItem::ref item, rippleLines.getItems ())
        {
            RippleState* line = (RippleState*)item.get ();

            if (!raPeer.isValid () || raPeer.getAccountID () == line->getAccountIDPeer ())
            {
                const STAmount&     saBalance   = line->getBalance ();
                const STAmount&     saLimit     = line->getLimit ();
                const STAmount&     saLimitPeer = line->getLimitPeer ();

                Json::Value&    jPeer   = jsonLines.append (Json::objectValue);

                jPeer["account"]        = RippleAddress::createHumanAccountID (line->getAccountIDPeer ());
                // Amount reported is positive if current account holds other account's IOUs.
                // Amount reported is negative if other account holds current account's IOUs.
                jPeer["balance"]        = saBalance.getText ();
                jPeer["currency"]       = saBalance.getHumanCurrency ();
                jPeer["limit"]          = saLimit.getText ();
                jPeer["limit_peer"]     = saLimitPeer.getText ();
                jPeer["quality_in"]     = static_cast<Json::UInt> (line->getQualityIn ());
                jPeer["quality_out"]    = static_cast<Json::UInt> (line->getQualityOut ());
                if (line->getAuth())
                    jPeer["authorized"] = true;
                if (line->getAuthPeer())
                    jPeer["peer_authorized"] = true;
                if (line->getNoRipple())
                    jPeer["no_ripple"]  = true;
                if (line->getNoRipplePeer())
                    jPeer["no_ripple_peer"] = true;
            }
        }

        loadType = Resource::feeMediumBurdenRPC;
    }
    else
    {
        jvResult    = rpcError (rpcACT_NOT_FOUND);
    }

    return jvResult;
}

static void offerAdder (Json::Value& jvLines, SLE::ref offer)
{
    if (offer->getType () == ltOFFER)
    {
        Json::Value&    obj = jvLines.append (Json::objectValue);
        offer->getFieldAmount (sfTakerPays).setJson (obj["taker_pays"]);
        offer->getFieldAmount (sfTakerGets).setJson (obj["taker_gets"]);
        obj["seq"] = offer->getFieldU32 (sfSequence);
        obj["flags"] = offer->getFieldU32 (sfFlags);
    }
}

// {
//   account: <account>|<nickname>|<account_public_key>
//   account_index: <number>        // optional, defaults to 0.
//   ledger_hash : <ledger>
//   ledger_index : <ledger_index>
// }
Json::Value RPCHandler::doAccountOffers (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& masterLockHolder)
{
    masterLockHolder.unlock ();

    Ledger::pointer     lpLedger;
    Json::Value         jvResult    = lookupLedger (params, lpLedger);

    if (!lpLedger)
        return jvResult;

    if (!params.isMember ("account"))
        return RPC::missing_field_error ("account");

    std::string     strIdent    = params["account"].asString ();
    bool            bIndex      = params.isMember ("account_index");
    int             iIndex      = bIndex ? params["account_index"].asUInt () : 0;

    RippleAddress   raAccount;

    jvResult    = accountFromString (lpLedger, raAccount, bIndex, strIdent, iIndex, false);

    if (!jvResult.empty ())
        return jvResult;

    // Get info on account.

    jvResult["account"] = raAccount.humanAccountID ();

    if (bIndex)
        jvResult["account_index"]   = iIndex;

    if (!lpLedger->hasAccount (raAccount))
        return rpcError (rpcACT_NOT_FOUND);

    Json::Value& jvsOffers = (jvResult["offers"] = Json::arrayValue);
    lpLedger->visitAccountItems (raAccount.getAccountID (), BIND_TYPE (&offerAdder, boost::ref (jvsOffers), P_1));

    loadType = Resource::feeMediumBurdenRPC;

    return jvResult;
}

template <class UnsignedInteger>
inline bool is_xrp (UnsignedInteger const& value)
{
    return value.isZero();
}

template <class UnsignedInteger>
inline bool is_not_xrp (UnsignedInteger const& value)
{
    return ! is_xrp (value);
}

inline uint160 const& xrp_issuer ()
{
    return ACCOUNT_XRP;
}

inline uint160 const& xrp_currency ()
{
    return CURRENCY_XRP;
}

inline uint160 const& neutral_issuer ()
{
    return ACCOUNT_ONE;
}

// {
//   "ledger_hash" : ledger,             // Optional.
//   "ledger_index" : ledger_index,      // Optional.
//   "taker_gets" : { "currency": currency, "issuer" : address },
//   "taker_pays" : { "currency": currency, "issuer" : address },
//   "taker" : address,                  // Optional.
//   "marker" : element,                 // Optional.
//   "limit" : integer,                  // Optional.
//   "proof" : boolean                   // Defaults to false.
// }
Json::Value RPCHandler::doBookOffers (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& masterLockHolder)
{
    masterLockHolder.unlock ();

    // VFALCO TODO Here is a terrible place for this kind of business
    //             logic. It needs to be moved elsewhere and documented,
    //             and encapsulated into a function.
    if (getApp().getJobQueue ().getJobCountGE (jtCLIENT) > 200)
        return rpcError (rpcTOO_BUSY);

    Ledger::pointer lpLedger;
    Json::Value jvResult (lookupLedger (params, lpLedger));

    if (!lpLedger)
        return jvResult;

    if (!params.isMember ("taker_pays"))
        return RPC::missing_field_error ("taker_pays");

    if (!params.isMember ("taker_gets"))
        return RPC::missing_field_error ("taker_gets");

    if (!params["taker_pays"].isObject ())
        return RPC::object_field_error ("taker_pays");

    if (!params["taker_gets"].isObject ())
        return RPC::object_field_error ("taker_gets");

    Json::Value const& taker_pays (params["taker_pays"]);

    if (!taker_pays.isMember ("currency"))
        return RPC::missing_field_error ("taker_pays.currency");

    if (! taker_pays ["currency"].isString ())
        return RPC::expected_field_error ("taker_pays.currency", "string");

    Json::Value const& taker_gets = params["taker_gets"];

    if (! taker_gets.isMember ("currency"))
        return RPC::missing_field_error ("taker_gets.currency");

    if (! taker_gets ["currency"].isString ())
        return RPC::expected_field_error ("taker_gets.currency", "string");

    uint160 pay_currency;

    if (! STAmount::currencyFromString (
        pay_currency, taker_pays ["currency"].asString ()))
    {
        WriteLog (lsINFO, RPCHandler) << "Bad taker_pays currency.";
        return RPC::make_error (rpcSRC_CUR_MALFORMED,
            "Invalid field 'taker_pays.currency', bad currency.");
    }

    uint160 get_currency;

    if (! STAmount::currencyFromString (
        get_currency, taker_gets ["currency"].asString ()))
    {
        WriteLog (lsINFO, RPCHandler) << "Bad taker_gets currency.";
        return RPC::make_error (rpcDST_AMT_MALFORMED,
            "Invalid field 'taker_gets.currency', bad currency.");
    }

    uint160 pay_issuer;

    if (taker_pays.isMember ("issuer"))
    {
        if (! taker_pays ["issuer"].isString())
            return RPC::expected_field_error ("taker_pays.issuer", "string");

        if (! STAmount::issuerFromString (
            pay_issuer, taker_pays ["issuer"].asString ()))
            return RPC::make_error (rpcSRC_ISR_MALFORMED,
                "Invalid field 'taker_pays.issuer', bad issuer.");

        if (pay_issuer == neutral_issuer ())
            return RPC::make_error (rpcSRC_ISR_MALFORMED,
                "Invalid field 'taker_pays.issuer', bad issuer account one.");
    }
    else
    {
        pay_issuer = xrp_issuer ();
    }

    if (is_xrp (pay_currency) && ! is_xrp (pay_issuer))
        return RPC::make_error (rpcSRC_ISR_MALFORMED,
            "Unneeded field 'taker_pays.issuer' for XRP currency specification.");

    if (is_not_xrp (pay_currency) && is_xrp (pay_issuer))
        return RPC::make_error (rpcSRC_ISR_MALFORMED,
            "Invalid field 'taker_pays.issuer', expected non-XRP issuer.");

    uint160 get_issuer;

    if (taker_gets.isMember ("issuer"))
    {
        if (! taker_gets ["issuer"].isString())
            return RPC::expected_field_error ("taker_gets.issuer", "string");

        if (! STAmount::issuerFromString (
            get_issuer, taker_gets ["issuer"].asString ()))
            return RPC::make_error (rpcDST_ISR_MALFORMED,
                "Invalid field 'taker_gets.issuer', bad issuer.");

        if (get_issuer == neutral_issuer ())
            return RPC::make_error (rpcDST_ISR_MALFORMED,
                "Invalid field 'taker_gets.issuer', bad issuer account one.");
    }
    else
    {
        get_issuer = xrp_issuer ();
    }


    if (is_xrp (get_currency) && ! is_xrp (get_issuer))
        return RPC::make_error (rpcDST_ISR_MALFORMED,
            "Unneeded field 'taker_gets.issuer' for XRP currency specification.");

    if (is_not_xrp (get_currency) && is_xrp (get_issuer))
        return RPC::make_error (rpcDST_ISR_MALFORMED,
            "Invalid field 'taker_gets.issuer', expected non-XRP issuer.");

    RippleAddress raTakerID;

    if (params.isMember ("taker"))
    {
        if (! params ["taker"].isString ())
            return RPC::expected_field_error ("taker", "string");
        
        if (! raTakerID.setAccountID (params ["taker"].asString ()))
            return RPC::invalid_field_error ("taker");
    }
    else
    {
        raTakerID.setAccountID (ACCOUNT_ONE);
    }

    if (pay_currency == get_currency && pay_issuer == get_issuer)
    {
        WriteLog (lsINFO, RPCHandler) << "taker_gets same as taker_pays.";
        return RPC::make_error (rpcBAD_MARKET);
    }

    if (params.isMember ("limit") && ! params ["limit"].isIntegral())
        return RPC::expected_field_error (
        "limit", "integer");

    unsigned int const iLimit (params.isMember ("limit")
        ? params ["limit"].asUInt ()
        : 0);

    bool const bProof (params.isMember ("proof"));

    Json::Value const jvMarker (params.isMember ("marker")
        ? params["marker"]
        : Json::Value (Json::nullValue));

    mNetOps->getBookPage (lpLedger, pay_currency, pay_issuer,
        get_currency, get_issuer, raTakerID.getAccountID (),
            bProof, iLimit, jvMarker, jvResult);

    loadType = Resource::feeMediumBurdenRPC;

    return jvResult;
}

// Result:
// {
//   random: <uint256>
// }
Json::Value RPCHandler::doRandom (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& masterLockHolder)
{
    masterLockHolder.unlock ();
    uint256         uRandom;

    try
    {
        RandomNumbers::getInstance ().fillBytes (uRandom.begin (), uRandom.size ());

        Json::Value jvResult;

        jvResult["random"]  = uRandom.ToString ();

        return jvResult;
    }
    catch (...)
    {
        return rpcError (rpcINTERNAL);
    }
}

Json::Value RPCHandler::doPathFind (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& masterLockHolder)
{
    Ledger::pointer lpLedger = mNetOps->getClosedLedger();
    masterLockHolder.unlock();

    if (!params.isMember ("subcommand") || !params["subcommand"].isString ())
        return rpcError (rpcINVALID_PARAMS);

    if (!mInfoSub)
        return rpcError (rpcNO_EVENTS);

    std::string sSubCommand = params["subcommand"].asString ();

    if (sSubCommand == "create")
    {
        loadType = Resource::feeHighBurdenRPC;
        mInfoSub->clearPathRequest ();
        return getApp().getPathRequests().makePathRequest (mInfoSub, lpLedger, params);
    }

    if (sSubCommand == "close")
    {
        PathRequest::pointer request = mInfoSub->getPathRequest ();

        if (!request)
            return rpcError (rpcNO_PF_REQUEST);

        mInfoSub->clearPathRequest ();
        return request->doClose (params);
    }

    if (sSubCommand == "status")
    {
        PathRequest::pointer request = mInfoSub->getPathRequest ();

        if (!request)
            return rpcNO_PF_REQUEST;

        return request->doStatus (params);
    }

    return rpcError (rpcINVALID_PARAMS);
}

// This interface is deprecated.
Json::Value RPCHandler::doRipplePathFind (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& masterLockHolder)
{
    masterLockHolder.unlock ();

    LegacyPathFind lpf (mRole == Config::ADMIN);
    if (!lpf.isOkay ())
        return rpcError (rpcTOO_BUSY);

    loadType = Resource::feeHighBurdenRPC;

    RippleAddress   raSrc;
    RippleAddress   raDst;
    STAmount        saDstAmount;
    Ledger::pointer lpLedger;

    Json::Value     jvResult;

    if (getConfig().RUN_STANDALONE || params.isMember("ledger") || params.isMember("ledger_index") || params.isMember("ledger_hash"))
    { // The caller specified a ledger
        jvResult = lookupLedger (params, lpLedger);
        if (!lpLedger)
            return jvResult;
    }

    if (!params.isMember ("source_account"))
    {
        jvResult    = rpcError (rpcSRC_ACT_MISSING);
    }
    else if (!params["source_account"].isString ()
             || !raSrc.setAccountID (params["source_account"].asString ()))
    {
        jvResult    = rpcError (rpcSRC_ACT_MALFORMED);
    }
    else if (!params.isMember ("destination_account"))
    {
        jvResult    = rpcError (rpcDST_ACT_MISSING);
    }
    else if (!params["destination_account"].isString ()
             || !raDst.setAccountID (params["destination_account"].asString ()))
    {
        jvResult    = rpcError (rpcDST_ACT_MALFORMED);
    }
    else if (
        // Parse saDstAmount.
        !params.isMember ("destination_amount")
        || !saDstAmount.bSetJson (params["destination_amount"])
        || !saDstAmount.isPositive()
        || (!!saDstAmount.getCurrency () && (!saDstAmount.getIssuer () || ACCOUNT_ONE == saDstAmount.getIssuer ())))
    {
        WriteLog (lsINFO, RPCHandler) << "Bad destination_amount.";
        jvResult    = rpcError (rpcINVALID_PARAMS);
    }
    else if (
        // Checks on source_currencies.
        params.isMember ("source_currencies")
        && (!params["source_currencies"].isArray ()
            || !params["source_currencies"].size ()) // Don't allow empty currencies.
    )
    {
        WriteLog (lsINFO, RPCHandler) << "Bad source_currencies.";
        jvResult    = rpcError (rpcINVALID_PARAMS);
    }
    else
    {
        loadType = Resource::feeHighBurdenRPC;
        RippleLineCache::pointer cache;

        if (lpLedger)
        { // The caller specified a ledger
            lpLedger = boost::make_shared<Ledger> (boost::ref (*lpLedger), false);
            cache = boost::make_shared<RippleLineCache>(lpLedger);
        }
        else
        { // Use the default ledger and cache
            lpLedger = mNetOps->getValidatedLedger();
            cache = getApp().getPathRequests().getLineCache(lpLedger, false);
        }

        Json::Value     jvSrcCurrencies;

        if (params.isMember ("source_currencies"))
        {
            jvSrcCurrencies = params["source_currencies"];
        }
        else
        {
            boost::unordered_set<uint160>   usCurrencies    = usAccountSourceCurrencies (raSrc, cache, true);

            jvSrcCurrencies             = Json::Value (Json::arrayValue);

            BOOST_FOREACH (const uint160 & uCurrency, usCurrencies)
            {
                Json::Value jvCurrency (Json::objectValue);

                jvCurrency["currency"]  = STAmount::createHumanCurrency (uCurrency);

                jvSrcCurrencies.append (jvCurrency);
            }
        }

        // Fill in currencies destination will accept
        Json::Value jvDestCur (Json::arrayValue);

        boost::unordered_set<uint160> usDestCurrID = usAccountDestCurrencies (raDst, cache, true);
        BOOST_FOREACH (const uint160 & uCurrency, usDestCurrID)
        jvDestCur.append (STAmount::createHumanCurrency (uCurrency));

        jvResult["destination_currencies"] = jvDestCur;
        jvResult["destination_account"] = raDst.humanAccountID ();

        Json::Value jvArray (Json::arrayValue);

        for (unsigned int i = 0; i != jvSrcCurrencies.size (); ++i)
        {
            Json::Value jvSource        = jvSrcCurrencies[i];

            uint160     uSrcCurrencyID;
            uint160     uSrcIssuerID;

            if (!jvSource.isObject ())
                return rpcError (rpcINVALID_PARAMS);

            // Parse mandatory currency.
            if (!jvSource.isMember ("currency")
                    || !STAmount::currencyFromString (uSrcCurrencyID, jvSource["currency"].asString ()))
            {
                WriteLog (lsINFO, RPCHandler) << "Bad currency.";

                return rpcError (rpcSRC_CUR_MALFORMED);
            }

            if (uSrcCurrencyID.isNonZero ())
                uSrcIssuerID = raSrc.getAccountID ();

            // Parse optional issuer.
            if (jvSource.isMember ("issuer") &&
                    ((!jvSource["issuer"].isString () ||
                      !STAmount::issuerFromString (uSrcIssuerID, jvSource["issuer"].asString ())) ||
                     (uSrcIssuerID.isZero () != uSrcCurrencyID.isZero ()) ||
                     (ACCOUNT_ONE == uSrcIssuerID)))
            {
                WriteLog (lsINFO, RPCHandler) << "Bad issuer.";

                return rpcError (rpcSRC_ISR_MALFORMED);
            }

            STPathSet   spsComputed;
            bool        bValid;
            Pathfinder  pf (cache, raSrc, raDst, uSrcCurrencyID, uSrcIssuerID, saDstAmount, bValid);

            int level = getConfig().PATH_SEARCH_OLD;
            if ((getConfig().PATH_SEARCH_MAX > level) && !getApp().getFeeTrack().isLoadedLocal())
                ++level;
            if (params.isMember("depth") && params["depth"].isIntegral())
            {
                int rLev = params["search_depth"].asInt ();
                if ((rLev < level) || (mRole == Config::ADMIN))
                    level = rLev;
            }

            if (params.isMember("paths"))
            {
                STParsedJSON paths ("paths", params["paths"]);
                if (paths.object.get() == nullptr)
                    return paths.error;
                else
                    spsComputed = paths.object.get()->downcast<STPathSet> ();
            }

            STPath extraPath;
            if (!bValid || !pf.findPaths (level, 4, spsComputed, extraPath))
            {
                WriteLog (lsWARNING, RPCHandler) << "ripple_path_find: No paths found.";
            }
            else
            {
                std::vector<PathState::pointer> vpsExpanded;
                STAmount                        saMaxAmountAct;
                STAmount                        saDstAmountAct;
                STAmount                        saMaxAmount (
                    uSrcCurrencyID,
                    !!uSrcIssuerID
                    ? uSrcIssuerID      // Use specifed issuer.
                    : !!uSrcCurrencyID  // Default to source account.
                    ? raSrc.getAccountID ()
                    : ACCOUNT_XRP,
                    1);
                saMaxAmount.negate ();

                LedgerEntrySet  lesSandbox (lpLedger, tapNONE);

                TER terResult   =
                    RippleCalc::rippleCalc (
                        lesSandbox,
                        saMaxAmountAct,         // <--
                        saDstAmountAct,         // <--
                        vpsExpanded,            // <--
                        saMaxAmount,            // --> Amount to send is unlimited to get an estimate.
                        saDstAmount,            // --> Amount to deliver.
                        raDst.getAccountID (),  // --> Account to deliver to.
                        raSrc.getAccountID (),  // --> Account sending from.
                        spsComputed,            // --> Path set.
                        false,                  // --> Don't allow partial payment. This is for normal fill or kill payments.
                        // Must achieve delivery goal.
                        false,                  // --> Don't limit quality. Average quality is wanted for normal payments.
                        false,                  // --> Allow direct ripple to be added to path set. to path set.
                        true);                  // --> Stand alone mode, no point in deleting unfundeds.

                // WriteLog (lsDEBUG, RPCHandler) << "ripple_path_find: PATHS IN: " << spsComputed.size() << " : " << spsComputed.getJson(0);
                // WriteLog (lsDEBUG, RPCHandler) << "ripple_path_find: PATHS EXP: " << vpsExpanded.size();

                WriteLog (lsWARNING, RPCHandler)
                        << boost::str (boost::format ("ripple_path_find: saMaxAmount=%s saDstAmount=%s saMaxAmountAct=%s saDstAmountAct=%s")
                                       % saMaxAmount
                                       % saDstAmount
                                       % saMaxAmountAct
                                       % saDstAmountAct);

                if ((extraPath.size() > 0) && ((terResult == terNO_LINE) || (terResult == tecPATH_PARTIAL)))
                {
                    WriteLog (lsDEBUG, PathRequest) << "Trying with an extra path element";
                    spsComputed.addPath(extraPath);
                    vpsExpanded.clear ();
                    lesSandbox.clear ();
                    terResult = RippleCalc::rippleCalc (lesSandbox, saMaxAmountAct, saDstAmountAct,
                                                        vpsExpanded, saMaxAmount, saDstAmount,
                                                        raDst.getAccountID (), raSrc.getAccountID (),
                                                        spsComputed, false, false, false, true);
                    WriteLog (lsDEBUG, PathRequest) << "Extra path element gives " << transHuman (terResult);
                }

                if (tesSUCCESS == terResult)
                {
                    Json::Value jvEntry (Json::objectValue);

                    STPathSet   spsCanonical;

                    // Reuse the expanded as it would need to be calcuated anyway to produce the canonical.
                    // (At least unless we make a direct canonical.)
                    // RippleCalc::setCanonical(spsCanonical, vpsExpanded, false);

                    jvEntry["source_amount"]    = saMaxAmountAct.getJson (0);
                    //                  jvEntry["paths_expanded"]   = vpsExpanded.getJson(0);
                    jvEntry["paths_canonical"]  = Json::arrayValue; // spsCanonical.getJson(0);
                    jvEntry["paths_computed"]   = spsComputed.getJson (0);

                    jvArray.append (jvEntry);
                }
                else
                {
                    std::string strToken;
                    std::string strHuman;

                    transResultInfo (terResult, strToken, strHuman);

                    WriteLog (lsDEBUG, RPCHandler)
                            << boost::str (boost::format ("ripple_path_find: %s %s %s")
                                           % strToken
                                           % strHuman
                                           % spsComputed.getJson (0));
                }
            }
        }

        // Each alternative differs by source currency.
        jvResult["alternatives"] = jvArray;
    }

    WriteLog (lsDEBUG, RPCHandler)
            << boost::str (boost::format ("ripple_path_find< %s")
                           % jvResult);

    return jvResult;
}

// {
//   tx_json: <object>,
//   secret: <secret>
// }
Json::Value RPCHandler::doSign (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& masterLockHolder)
{
    masterLockHolder.unlock ();

    loadType = Resource::feeHighBurdenRPC;
    bool bFailHard = params.isMember ("fail_hard") && params["fail_hard"].asBool ();
    return transactionSign (params, false, bFailHard, masterLockHolder);
}

// {
//   tx_json: <object>,
//   secret: <secret>
// }
Json::Value RPCHandler::doSubmit (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& masterLockHolder)
{
    masterLockHolder.unlock ();

    loadType = Resource::feeMediumBurdenRPC;

    if (!params.isMember ("tx_blob"))
    {
        bool bFailHard = params.isMember ("fail_hard") && params["fail_hard"].asBool ();
        return transactionSign (params, true, bFailHard, masterLockHolder);
    }

    Json::Value                 jvResult;

    std::pair<Blob, bool> ret(strUnHex (params["tx_blob"].asString ()));

    if (!ret.second || !ret.first.size ())
        return rpcError (rpcINVALID_PARAMS);

    Serializer                  sTrans (ret.first);
    SerializerIterator          sitTrans (sTrans);

    SerializedTransaction::pointer stpTrans;

    try
    {
        stpTrans = boost::make_shared<SerializedTransaction> (boost::ref (sitTrans));
    }
    catch (std::exception& e)
    {
        jvResult["error"]           = "invalidTransaction";
        jvResult["error_exception"] = e.what ();

        return jvResult;
    }

    Transaction::pointer            tpTrans;

    try
    {
        tpTrans     = boost::make_shared<Transaction> (stpTrans, false);
    }
    catch (std::exception& e)
    {
        jvResult["error"]           = "internalTransaction";
        jvResult["error_exception"] = e.what ();

        return jvResult;
    }

    try
    {
        (void) mNetOps->processTransaction (tpTrans, mRole == Config::ADMIN, true,
            params.isMember ("fail_hard") && params["fail_hard"].asBool ());
    }
    catch (std::exception& e)
    {
        jvResult["error"]           = "internalSubmit";
        jvResult["error_exception"] = e.what ();

        return jvResult;
    }


    try
    {
        jvResult["tx_json"]     = tpTrans->getJson (0);
        jvResult["tx_blob"]     = strHex (tpTrans->getSTransaction ()->getSerializer ().peekData ());

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
    catch (std::exception& e)
    {
        jvResult["error"]           = "internalJson";
        jvResult["error_exception"] = e.what ();

        return jvResult;
    }
}

Json::Value RPCHandler::doConsensusInfo (Json::Value, Resource::Charge& loadType, Application::ScopedLockType& masterLockHolder)
{
    Json::Value ret (Json::objectValue);

    ret["info"] = mNetOps->getConsensusInfo ();

    return ret;
}

Json::Value RPCHandler::doFetchInfo (Json::Value jvParams, Resource::Charge& loadType, Application::ScopedLockType& masterLockHolder)
{
    masterLockHolder.unlock ();

    Json::Value ret (Json::objectValue);

    if (jvParams.isMember("clear") && jvParams["clear"].asBool())
    {
        mNetOps->clearLedgerFetch();
        ret["clear"] = true;
    }

    ret["info"] = mNetOps->getLedgerFetchInfo();

    return ret;
}

Json::Value RPCHandler::doServerInfo (Json::Value, Resource::Charge& loadType, Application::ScopedLockType& masterLockHolder)
{
    Json::Value ret (Json::objectValue);

    ret["info"] = mNetOps->getServerInfo (true, mRole == Config::ADMIN);

    return ret;
}

Json::Value RPCHandler::doServerState (Json::Value, Resource::Charge& loadType, Application::ScopedLockType& masterLockHolder)
{
    Json::Value ret (Json::objectValue);

    ret["state"]    = mNetOps->getServerInfo (false, mRole == Config::ADMIN);

    return ret;
}

// {
//   start: <index>
// }
Json::Value RPCHandler::doTxHistory (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& masterLockHolder)
{
    masterLockHolder.unlock ();
    loadType = Resource::feeMediumBurdenRPC;

    if (!params.isMember ("start"))
        return rpcError (rpcINVALID_PARAMS);

    unsigned int startIndex = params["start"].asUInt ();

    if ((startIndex > 10000) &&  (mRole != Config::ADMIN))
        return rpcError (rpcNO_PERMISSION);

    Json::Value obj;
    Json::Value txs;

    obj["index"] = startIndex;

    std::string sql =
        boost::str (boost::format ("SELECT * FROM Transactions ORDER BY LedgerSeq desc LIMIT %u,20")
                    % startIndex);

    {
        Database* db = getApp().getTxnDB ()->getDB ();
        DeprecatedScopedLock sl (getApp().getTxnDB ()->getDBLock ());

        SQL_FOREACH (db, sql)
        {
            Transaction::pointer trans = Transaction::transactionFromSQL (db, false);

            if (trans) txs.append (trans->getJson (0));
        }
    }

    obj["txs"] = txs;

    return obj;
}

// {
//   transaction: <hex>
// }
Json::Value RPCHandler::doTx (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& masterLockHolder)
{
    masterLockHolder.unlock ();

    if (!params.isMember ("transaction"))
        return rpcError (rpcINVALID_PARAMS);

    bool binary = params.isMember ("binary") && params["binary"].asBool ();

    std::string strTransaction  = params["transaction"].asString ();

    if (Transaction::isHexTxID (strTransaction))
    {
        // transaction by ID
        uint256 txid (strTransaction);

        Transaction::pointer txn = getApp().getMasterTransaction ().fetch (txid, true);

        if (!txn)
            return rpcError (rpcTXN_NOT_FOUND);

#ifdef READY_FOR_NEW_TX_FORMAT
        Json::Value ret;
        ret["transaction"] = txn->getJson (0, binary);
#else
        Json::Value ret = txn->getJson (0, binary);
#endif

        if (txn->getLedger () != 0)
        {
            Ledger::pointer lgr = mNetOps->getLedgerBySeq (txn->getLedger ());

            if (lgr)
            {
                bool okay = false;

                if (binary)
                {
                    std::string meta;

                    if (lgr->getMetaHex (txid, meta))
                    {
                        ret["meta"] = meta;
                        okay = true;
                    }
                }
                else
                {
                    TransactionMetaSet::pointer set;

                    if (lgr->getTransactionMeta (txid, set))
                    {
                        okay = true;
                        ret["meta"] = set->getJson (0);
                    }
                }

                if (okay)
                    ret["validated"] = mNetOps->isValidated (lgr);
            }
        }

        return ret;
    }

    return rpcError (rpcNOT_IMPL);
}

Json::Value RPCHandler::doLedgerClosed (Json::Value, Resource::Charge& loadType, Application::ScopedLockType& masterLockHolder)
{
    masterLockHolder.unlock ();
    Json::Value jvResult;

    uint256 uLedger = mNetOps->getClosedLedgerHash ();

    jvResult["ledger_index"]        = mNetOps->getLedgerID (uLedger);
    jvResult["ledger_hash"]         = uLedger.ToString ();
    //jvResult["ledger_time"]       = uLedger.

    return jvResult;
}

Json::Value RPCHandler::doLedgerCurrent (Json::Value, Resource::Charge& loadType, Application::ScopedLockType& masterLockHolder)
{
    masterLockHolder.unlock ();
    Json::Value jvResult;

    jvResult["ledger_current_index"]    = mNetOps->getCurrentLedgerID ();

    return jvResult;
}

// Get state nodes from a ledger
//   Inputs:
//     limit:        integer, maximum number of entries
//     marker:       opaque, resume point
//     binary:       boolean, format
//   Outputs:
//     ledger_hash:  chosen ledger's hash
//     ledger_index: chosen ledger's index
//     state:        array of state nodes
//     marker:       resume point, if any
Json::Value RPCHandler::doLedgerData (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& masterLockHolder)
{
    masterLockHolder.unlock ();

    int const BINARY_PAGE_LENGTH = 256;
    int const JSON_PAGE_LENGTH = 2048;

    Ledger::pointer lpLedger;

    Json::Value jvResult = lookupLedger (params, lpLedger);
    if (!lpLedger)
        return jvResult;

    uint256 resumePoint;
    if (params.isMember ("marker"))
    {
        Json::Value const& jMarker = params["marker"];
        if (!jMarker.isString ())
            return RPC::expected_field_error ("marker", "valid");
        if (!resumePoint.SetHex (jMarker.asString ()))
            return RPC::expected_field_error ("marker", "valid");
    }

    bool isBinary = false;
    if (params.isMember ("binary"))
    {
        Json::Value const& jBinary = params["binary"];
        if (!jBinary.isBool ())
            return RPC::expected_field_error ("binary", "bool");
        isBinary = jBinary.asBool ();
    }

    int limit = -1;
    int maxLimit = isBinary ? BINARY_PAGE_LENGTH : JSON_PAGE_LENGTH;

    if (params.isMember ("limit"))
    {
        Json::Value const& jLimit = params["limit"];
        if (!jLimit.isIntegral ())
            return RPC::expected_field_error ("limit", "integer");

        limit = jLimit.asInt ();
    }

    if ((limit < 0) || ((limit > maxLimit) && (mRole != Config::ADMIN)))
        limit = maxLimit;

    Json::Value jvReply = Json::objectValue;

    jvReply["ledger_hash"] = lpLedger->getHash().GetHex ();
    jvReply["ledger_index"] = beast::lexicalCastThrow <std::string> (lpLedger->getLedgerSeq ());

    Json::Value& nodes = (jvReply["state"] = Json::arrayValue);
    SHAMap& map = *(lpLedger->peekAccountStateMap ());

    for (;;)
    {
       SHAMapItem::pointer item = map.peekNextItem (resumePoint);
       if (!item)
           break;
       resumePoint = item->getTag();

       if (limit-- <= 0)
       {
           --resumePoint;
           jvReply["marker"] = resumePoint.GetHex ();
           break;
       }

       if (isBinary)
       {
           Json::Value& entry = nodes.append (Json::objectValue);
           entry["data"] = strHex (item->peekData().begin(), item->peekData().size());
           entry["index"] = item->getTag ().GetHex ();
       }
       else
       {
           SLE sle (item->peekSerializer(), item->getTag ());
           Json::Value& entry = nodes.append (sle.getJson (0));
           entry["index"] = item->getTag ().GetHex ();
       }
    }

    return jvReply;
}

// ledger [id|index|current|closed] [full]
// {
//    ledger: 'current' | 'closed' | <uint256> | <number>,  // optional
//    full: true | false    // optional, defaults to false.
// }
Json::Value RPCHandler::doLedger (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& masterLockHolder)
{
    masterLockHolder.unlock ();
    if (!params.isMember ("ledger") && !params.isMember ("ledger_hash") && !params.isMember ("ledger_index"))
    {
        Json::Value ret (Json::objectValue), current (Json::objectValue), closed (Json::objectValue);

        getApp().getLedgerMaster ().getCurrentLedger ()->addJson (current, 0);
        getApp().getLedgerMaster ().getClosedLedger ()->addJson (closed, 0);

        ret["open"] = current;
        ret["closed"] = closed;

        return ret;
    }

    Ledger::pointer     lpLedger;
    Json::Value         jvResult    = lookupLedger (params, lpLedger);

    if (!lpLedger)
        return jvResult;

    bool    bFull           = params.isMember ("full") && params["full"].asBool ();
    bool    bTransactions   = params.isMember ("transactions") && params["transactions"].asBool ();
    bool    bAccounts       = params.isMember ("accounts") && params["accounts"].asBool ();
    bool    bExpand         = params.isMember ("expand") && params["expand"].asBool ();
    int     iOptions        = (bFull ? LEDGER_JSON_FULL : 0)
                              | (bExpand ? LEDGER_JSON_EXPAND : 0)
                              | (bTransactions ? LEDGER_JSON_DUMP_TXRP : 0)
                              | (bAccounts ? LEDGER_JSON_DUMP_STATE : 0);

    if (bFull || bAccounts)
    {

        if (mRole != Config::ADMIN)
        {
            // Until some sane way to get full ledgers has been implemented, disallow
            // retrieving all state nodes
            return rpcError (rpcNO_PERMISSION);
        }

        if (getApp().getFeeTrack().isLoadedLocal() && (mRole != Config::ADMIN))
        {
            WriteLog (lsDEBUG, Peer) << "Too busy to give full ledger";
            return rpcError(rpcTOO_BUSY);
        }
        loadType = Resource::feeHighBurdenRPC;
    }


    Json::Value ret (Json::objectValue);
    lpLedger->addJson (ret, iOptions);

    return ret;
}

// Temporary switching code until the old account_tx is removed
Json::Value RPCHandler::doAccountTxSwitch (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& masterLockHolder)
{
    if (params.isMember("offset") || params.isMember("count") || params.isMember("descending") ||
            params.isMember("ledger_max") || params.isMember("ledger_min"))
        return doAccountTxOld(params, loadType, masterLockHolder);
    return doAccountTx(params, loadType, masterLockHolder);
}

// {
//   account: account,
//   ledger_index_min: ledger_index,
//   ledger_index_max: ledger_index,
//   binary: boolean,              // optional, defaults to false
//   count: boolean,               // optional, defaults to false
//   descending: boolean,          // optional, defaults to false
//   offset: integer,              // optional, defaults to 0
//   limit: integer                // optional
// }
Json::Value RPCHandler::doAccountTxOld (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& masterLockHolder)
{
    masterLockHolder.unlock ();

    RippleAddress   raAccount;
    std::uint32_t   offset      = params.isMember ("offset") ? params["offset"].asUInt () : 0;
    int             limit       = params.isMember ("limit") ? params["limit"].asUInt () : -1;
    bool            bBinary     = params.isMember ("binary") && params["binary"].asBool ();
    bool            bDescending = params.isMember ("descending") && params["descending"].asBool ();
    bool            bCount      = params.isMember ("count") && params["count"].asBool ();
    std::uint32_t   uLedgerMin;
    std::uint32_t   uLedgerMax;
    std::uint32_t   uValidatedMin;
    std::uint32_t   uValidatedMax;
    bool            bValidated  = mNetOps->getValidatedRange (uValidatedMin, uValidatedMax);

    if (!params.isMember ("account"))
        return rpcError (rpcINVALID_PARAMS);

    if (!raAccount.setAccountID (params["account"].asString ()))
        return rpcError (rpcACT_MALFORMED);

    if (offset > 3000)
        return rpcError (rpcATX_DEPRECATED);

    loadType = Resource::feeHighBurdenRPC;

    // DEPRECATED
    if (params.isMember ("ledger_min"))
    {
        params["ledger_index_min"]   = params["ledger_min"];
        bDescending = true;
    }

    // DEPRECATED
    if (params.isMember ("ledger_max"))
    {
        params["ledger_index_max"]   = params["ledger_max"];
        bDescending = true;
    }

    if (params.isMember ("ledger_index_min") || params.isMember ("ledger_index_max"))
    {
        std::int64_t iLedgerMin  = params.isMember ("ledger_index_min") ? params["ledger_index_min"].asInt () : -1;
        std::int64_t iLedgerMax  = params.isMember ("ledger_index_max") ? params["ledger_index_max"].asInt () : -1;

        if (!bValidated && (iLedgerMin == -1 || iLedgerMax == -1))
        {
            // Don't have a validated ledger range.
            return rpcError (rpcLGR_IDXS_INVALID);
        }

        uLedgerMin  = iLedgerMin == -1 ? uValidatedMin : iLedgerMin;
        uLedgerMax  = iLedgerMax == -1 ? uValidatedMax : iLedgerMax;

        if (uLedgerMax < uLedgerMin)
        {
            return rpcError (rpcLGR_IDXS_INVALID);
        }
    }
    else
    {
        Ledger::pointer l;
        Json::Value ret = lookupLedger (params, l);

        if (!l)
            return ret;

        uLedgerMin = uLedgerMax = l->getLedgerSeq ();
    }

    int count = 0;

#ifndef BEAST_DEBUG

    try
    {
#endif

        Json::Value ret (Json::objectValue);

        ret["account"] = raAccount.humanAccountID ();
        Json::Value& jvTxns = (ret["transactions"] = Json::arrayValue);

        if (bBinary)
        {
            std::vector<NetworkOPs::txnMetaLedgerType> txns =
                mNetOps->getAccountTxsB (raAccount, uLedgerMin, uLedgerMax, bDescending, offset, limit, mRole == Config::ADMIN);

            for (std::vector<NetworkOPs::txnMetaLedgerType>::const_iterator it = txns.begin (), end = txns.end ();
                    it != end; ++it)
            {
                ++count;
                Json::Value& jvObj = jvTxns.append (Json::objectValue);

                std::uint32_t  uLedgerIndex = it->get<2> ();
                jvObj["tx_blob"]        = it->get<0> ();
                jvObj["meta"]           = it->get<1> ();
                jvObj["ledger_index"]   = uLedgerIndex;
                jvObj["validated"]      = bValidated && uValidatedMin <= uLedgerIndex && uValidatedMax >= uLedgerIndex;

            }
        }
        else
        {
            std::vector< std::pair<Transaction::pointer, TransactionMetaSet::pointer> > txns = mNetOps->getAccountTxs (raAccount, uLedgerMin, uLedgerMax, bDescending, offset, limit, mRole == Config::ADMIN);

            for (std::vector< std::pair<Transaction::pointer, TransactionMetaSet::pointer> >::iterator it = txns.begin (), end = txns.end (); it != end; ++it)
            {
                ++count;
                Json::Value&    jvObj = jvTxns.append (Json::objectValue);

                if (it->first)
                    jvObj["tx"]             = it->first->getJson (1);

                if (it->second)
                {
                    std::uint32_t uLedgerIndex = it->second->getLgrSeq ();

                    jvObj["meta"]           = it->second->getJson (0);
                    jvObj["validated"]      = bValidated && uValidatedMin <= uLedgerIndex && uValidatedMax >= uLedgerIndex;
                }

            }
        }

        //Add information about the original query
        ret["ledger_index_min"] = uLedgerMin;
        ret["ledger_index_max"] = uLedgerMax;
        ret["validated"]        = bValidated && uValidatedMin <= uLedgerMin && uValidatedMax >= uLedgerMax;
        ret["offset"]           = offset;

        // We no longer return the full count but only the count of returned transactions
        // Computing this count was two expensive and this API is deprecated anyway
        if (bCount)
            ret["count"]        = count;

        if (params.isMember ("limit"))
            ret["limit"]        = limit;


        return ret;
#ifndef BEAST_DEBUG
    }
    catch (...)
    {
        return rpcError (rpcINTERNAL);
    }

#endif
}

// {
//   account: account,
//   ledger_index_min: ledger_index  // optional, defaults to earliest
//   ledger_index_max: ledger_index, // optional, defaults to latest
//   binary: boolean,                // optional, defaults to false
//   forward: boolean,               // optional, defaults to false
//   limit: integer,                 // optional
//   marker: opaque                  // optional, resume previous query
// }
Json::Value RPCHandler::doAccountTx (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& masterLockHolder)
{
    masterLockHolder.unlock ();

    RippleAddress   raAccount;
    int             limit       = params.isMember ("limit") ? params["limit"].asUInt () : -1;
    bool            bBinary     = params.isMember ("binary") && params["binary"].asBool ();
    bool            bForward    = params.isMember ("forward") && params["forward"].asBool ();
    std::uint32_t   uLedgerMin;
    std::uint32_t   uLedgerMax;
    std::uint32_t   uValidatedMin;
    std::uint32_t   uValidatedMax;
    bool            bValidated  = mNetOps->getValidatedRange (uValidatedMin, uValidatedMax);

    if (!bValidated)
    {
        // Don't have a validated ledger range.
        return rpcError (rpcLGR_IDXS_INVALID);
    }

    if (!params.isMember ("account"))
        return rpcError (rpcINVALID_PARAMS);

    if (!raAccount.setAccountID (params["account"].asString ()))
        return rpcError (rpcACT_MALFORMED);

    loadType = Resource::feeMediumBurdenRPC;

    if (params.isMember ("ledger_index_min") || params.isMember ("ledger_index_max"))
    {
        std::int64_t iLedgerMin  = params.isMember ("ledger_index_min") ? params["ledger_index_min"].asInt () : -1;
        std::int64_t iLedgerMax  = params.isMember ("ledger_index_max") ? params["ledger_index_max"].asInt () : -1;


        uLedgerMin  = iLedgerMin == -1 ? uValidatedMin : iLedgerMin;
        uLedgerMax  = iLedgerMax == -1 ? uValidatedMax : iLedgerMax;

        if (uLedgerMax < uLedgerMin)
        {
            return rpcError (rpcLGR_IDXS_INVALID);
        }
    }
    else
    {
        Ledger::pointer l;
        Json::Value ret = lookupLedger (params, l);

        if (!l)
            return ret;

        uLedgerMin = uLedgerMax = l->getLedgerSeq ();
    }

    Json::Value resumeToken;

    if (params.isMember("marker"))
    {
         resumeToken = params["marker"];
    }

#ifndef BEAST_DEBUG

    try
    {
#endif
        Json::Value ret (Json::objectValue);

        ret["account"] = raAccount.humanAccountID ();
        Json::Value& jvTxns = (ret["transactions"] = Json::arrayValue);

        if (bBinary)
        {
            std::vector<NetworkOPs::txnMetaLedgerType> txns =
                mNetOps->getTxsAccountB (raAccount, uLedgerMin, uLedgerMax, bForward, resumeToken, limit, mRole == Config::ADMIN);

            for (std::vector<NetworkOPs::txnMetaLedgerType>::const_iterator it = txns.begin (), end = txns.end ();
                    it != end; ++it)
            {
                Json::Value& jvObj = jvTxns.append (Json::objectValue);

                std::uint32_t uLedgerIndex = it->get<2> ();
                jvObj["tx_blob"]        = it->get<0> ();
                jvObj["meta"]           = it->get<1> ();
                jvObj["ledger_index"]   = uLedgerIndex;
                jvObj["validated"]      = bValidated && uValidatedMin <= uLedgerIndex && uValidatedMax >= uLedgerIndex;

            }
        }
        else
        {
            std::vector< std::pair<Transaction::pointer, TransactionMetaSet::pointer> > txns =
                 mNetOps->getTxsAccount (raAccount, uLedgerMin, uLedgerMax, bForward, resumeToken, limit, mRole == Config::ADMIN);

            for (std::vector< std::pair<Transaction::pointer, TransactionMetaSet::pointer> >::iterator it = txns.begin (), end = txns.end (); it != end; ++it)
            {
                Json::Value&    jvObj = jvTxns.append (Json::objectValue);

                if (it->first)
                    jvObj["tx"]             = it->first->getJson (1);

                if (it->second)
                {
                    std::uint32_t uLedgerIndex = it->second->getLgrSeq ();

                    jvObj["meta"]           = it->second->getJson (0);
                    jvObj["validated"]      = bValidated && uValidatedMin <= uLedgerIndex && uValidatedMax >= uLedgerIndex;
                }

            }
        }

        //Add information about the original query
        ret["ledger_index_min"] = uLedgerMin;
        ret["ledger_index_max"] = uLedgerMax;
        if (params.isMember ("limit"))
            ret["limit"]        = limit;
        if (!resumeToken.isNull())
            ret["marker"] = resumeToken;

        return ret;
#ifndef BEAST_DEBUG
    }
    catch (...)
    {
        return rpcError (rpcINTERNAL);
    }

#endif
}

// {
//   secret: <string>   // optional
// }
//
// This command requires Config::ADMIN access because it makes no sense to ask an untrusted server for this.
Json::Value RPCHandler::doValidationCreate (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& masterLockHolder)
{
    RippleAddress   raSeed;
    Json::Value     obj (Json::objectValue);

    if (!params.isMember ("secret"))
    {
        WriteLog (lsDEBUG, RPCHandler) << "Creating random validation seed.";

        raSeed.setSeedRandom ();                // Get a random seed.
    }
    else if (!raSeed.setSeedGeneric (params["secret"].asString ()))
    {
        return rpcError (rpcBAD_SEED);
    }

    obj["validation_public_key"]    = RippleAddress::createNodePublic (raSeed).humanNodePublic ();
    obj["validation_seed"]          = raSeed.humanSeed ();
    obj["validation_key"]           = raSeed.humanSeed1751 ();

    return obj;
}

// {
//   secret: <string>
// }
Json::Value RPCHandler::doValidationSeed (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& masterLockHolder)
{
    Json::Value obj (Json::objectValue);

    if (!params.isMember ("secret"))
    {
        Log::out() << "Unset validation seed.";

        getConfig ().VALIDATION_SEED.clear ();
        getConfig ().VALIDATION_PUB.clear ();
        getConfig ().VALIDATION_PRIV.clear ();
    }
    else if (!getConfig ().VALIDATION_SEED.setSeedGeneric (params["secret"].asString ()))
    {
        getConfig ().VALIDATION_PUB.clear ();
        getConfig ().VALIDATION_PRIV.clear ();

        return rpcError (rpcBAD_SEED);
    }
    else
    {
        getConfig ().VALIDATION_PUB = RippleAddress::createNodePublic (getConfig ().VALIDATION_SEED);
        getConfig ().VALIDATION_PRIV = RippleAddress::createNodePrivate (getConfig ().VALIDATION_SEED);

        obj["validation_public_key"]    = getConfig ().VALIDATION_PUB.humanNodePublic ();
        obj["validation_seed"]          = getConfig ().VALIDATION_SEED.humanSeed ();
        obj["validation_key"]           = getConfig ().VALIDATION_SEED.humanSeed1751 ();
    }

    return obj;
}

Json::Value RPCHandler::accounts (Ledger::ref lrLedger, const RippleAddress& naMasterGenerator)
{
    Json::Value jsonAccounts (Json::arrayValue);

    // YYY Don't want to leak to thin server that these accounts are related.
    // YYY Would be best to alternate requests to servers and to cache results.
    unsigned int    uIndex  = 0;

    do
    {
        RippleAddress       naAccount;

        naAccount.setAccountPublic (naMasterGenerator, uIndex++);

        AccountState::pointer as    = mNetOps->getAccountState (lrLedger, naAccount);

        if (as)
        {
            Json::Value jsonAccount (Json::objectValue);

            as->addJson (jsonAccount);

            jsonAccounts.append (jsonAccount);
        }
        else
        {
            uIndex  = 0;
        }
    }
    while (uIndex);

    return jsonAccounts;
}

// {
//   seed: <string>
//   ledger_hash : <ledger>
//   ledger_index : <ledger_index>
// }
Json::Value RPCHandler::doWalletAccounts (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& masterLockHolder)
{
    Ledger::pointer     lpLedger;
    Json::Value         jvResult    = lookupLedger (params, lpLedger);

    if (!lpLedger)
        return jvResult;

    RippleAddress   naSeed;

    if (!params.isMember ("seed") || !naSeed.setSeedGeneric (params["seed"].asString ()))
    {
        return rpcError (rpcBAD_SEED);
    }

    // Try the seed as a master seed.
    RippleAddress   naMasterGenerator   = RippleAddress::createGeneratorPublic (naSeed);

    Json::Value jsonAccounts    = accounts (lpLedger, naMasterGenerator);

    if (jsonAccounts.empty ())
    {
        // No account via seed as master, try seed a regular.
        Json::Value ret = getMasterGenerator (lpLedger, naSeed, naMasterGenerator);

        if (!ret.empty ())
            return ret;

        ret["accounts"] = accounts (lpLedger, naMasterGenerator);

        return ret;
    }
    else
    {
        // Had accounts via seed as master, return them.
        Json::Value ret (Json::objectValue);

        ret["accounts"] = jsonAccounts;

        return ret;
    }
}

Json::Value RPCHandler::doLogRotate (Json::Value, Resource::Charge& loadType, Application::ScopedLockType& masterLockHolder)
{
    masterLockHolder.unlock ();
    return LogSink::get()->rotateLog ();
}

// {
//  passphrase: <string>
// }
Json::Value RPCHandler::doWalletPropose (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& masterLockHolder)
{
    masterLockHolder.unlock ();

    RippleAddress   naSeed;
    RippleAddress   naAccount;

    if (params.isMember ("passphrase"))
    {
        naSeed  = RippleAddress::createSeedGeneric (params["passphrase"].asString ());
    }
    else
    {
        naSeed.setSeedRandom ();
    }

    RippleAddress   naGenerator = RippleAddress::createGeneratorPublic (naSeed);
    naAccount.setAccountPublic (naGenerator, 0);

    Json::Value obj (Json::objectValue);

    obj["master_seed"]      = naSeed.humanSeed ();
    obj["master_seed_hex"]  = naSeed.getSeed ().ToString ();
    //obj["master_key"]     = naSeed.humanSeed1751();
    obj["account_id"]       = naAccount.humanAccountID ();

    return obj;
}

// {
//   secret: <string>
// }
Json::Value RPCHandler::doWalletSeed (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& masterLockHolder)
{
    RippleAddress   raSeed;
    bool            bSecret = params.isMember ("secret");

    if (bSecret && !raSeed.setSeedGeneric (params["secret"].asString ()))
    {
        return rpcError (rpcBAD_SEED);
    }
    else
    {
        RippleAddress   raAccount;

        if (!bSecret)
        {
            raSeed.setSeedRandom ();
        }

        RippleAddress   raGenerator = RippleAddress::createGeneratorPublic (raSeed);

        raAccount.setAccountPublic (raGenerator, 0);

        Json::Value obj (Json::objectValue);

        obj["seed"]     = raSeed.humanSeed ();
        obj["key"]      = raSeed.humanSeed1751 ();

        return obj;
    }
}

#if ENABLE_INSECURE
// TODO: for now this simply checks if this is the Config::ADMIN account
// TODO: need to prevent them hammering this over and over
// TODO: maybe a better way is only allow Config::ADMIN from local host
// {
//   username: <string>,
//   password: <string>
// }
Json::Value RPCHandler::doLogin (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& masterLockHolder)
{
    if (!params.isMember ("username")
            || !params.isMember ("password"))
        return rpcError (rpcINVALID_PARAMS);

    if (params["username"].asString () == getConfig ().RPC_USER && params["password"].asString () == getConfig ().RPC_PASSWORD)
    {
        //mRole=ADMIN;
        return "logged in";
    }
    else
    {
        return "nope";
    }
}
#endif

static void textTime (std::string& text, int& seconds, const char* unitName, int unitVal)
{
    int i = seconds / unitVal;

    if (i == 0)
        return;

    seconds -= unitVal * i;

    if (!text.empty ())
        text += ", ";

    text += beast::lexicalCastThrow <std::string> (i);
    text += " ";
    text += unitName;

    if (i > 1)
        text += "s";
}

Json::Value RPCHandler::doFeature (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& mlh)
{
    if (!params.isMember ("feature"))
    {
        Json::Value jvReply = Json::objectValue;
        jvReply["features"] = getApp().getFeatureTable ().getJson (0);
        return jvReply;
    }

    uint256 uFeature = getApp().getFeatureTable ().getFeature (params["feature"].asString ());

    if (uFeature.isZero ())
    {
        uFeature.SetHex (params["feature"].asString ());

        if (uFeature.isZero ())
            return rpcError (rpcBAD_FEATURE);
    }

    if (!params.isMember ("vote"))
        return getApp().getFeatureTable ().getJson (uFeature);

    // WRITEME
    return rpcError (rpcNOT_SUPPORTED);
}

// {
//   min_count: <number>  // optional, defaults to 10
// }
Json::Value RPCHandler::doGetCounts (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& masterLockHolder)
{
    int minCount = 10;

    if (params.isMember ("min_count"))
        minCount = params["min_count"].asUInt ();

    CountedObjects::List objectCounts = CountedObjects::getInstance ().getCounts (minCount);

    Json::Value ret (Json::objectValue);

    BOOST_FOREACH (CountedObjects::Entry& it, objectCounts)
    {
        ret [it.first] = it.second;
    }

    int dbKB = getApp().getLedgerDB ()->getDB ()->getKBUsedAll ();

    if (dbKB > 0)
        ret["dbKBTotal"] = dbKB;

    dbKB = getApp().getLedgerDB ()->getDB ()->getKBUsedDB ();

    if (dbKB > 0)
        ret["dbKBLedger"] = dbKB;

    dbKB = getApp().getTxnDB ()->getDB ()->getKBUsedDB ();

    if (dbKB > 0)
        ret["dbKBTransaction"] = dbKB;

    {
        std::size_t c = getApp().getOPs().getLocalTxCount ();
        if (c > 0)
            ret["local_txs"] = static_cast<Json::UInt> (c);
    }

    ret["write_load"] = getApp().getNodeStore ().getWriteLoad ();

    ret["SLE_hit_rate"] = getApp().getSLECache ().getHitRate ();
    ret["node_hit_rate"] = getApp().getNodeStore ().getCacheHitRate ();
    ret["ledger_hit_rate"] = getApp().getLedgerMaster ().getCacheHitRate ();
    ret["AL_hit_rate"] = AcceptedLedger::getCacheHitRate ();

    ret["fullbelow_size"] = int(getApp().getFullBelowCache().size());
    ret["treenode_size"] = SHAMap::getTreeNodeSize ();

    std::string uptime;
    int s = UptimeTimer::getInstance ().getElapsedSeconds ();
    textTime (uptime, s, "year", 365 * 24 * 60 * 60);
    textTime (uptime, s, "day", 24 * 60 * 60);
    textTime (uptime, s, "hour", 60 * 60);
    textTime (uptime, s, "minute", 60);
    textTime (uptime, s, "second", 1);
    ret["uptime"] = uptime;

    return ret;
}

Json::Value RPCHandler::doLogLevel (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& masterLockHolder)
{
    // log_level
    if (!params.isMember ("severity"))
    {
        // get log severities
        Json::Value ret (Json::objectValue);
        Json::Value lev (Json::objectValue);

        lev["base"] = Log::severityToString (LogSink::get()->getMinSeverity ());
        std::vector< std::pair<std::string, std::string> > logTable = LogPartition::getSeverities ();
        typedef std::map<std::string, std::string>::value_type stringPair;
        BOOST_FOREACH (const stringPair & it, logTable)
        lev[it.first] = it.second;

        ret["levels"] = lev;
        return ret;
    }

    LogSeverity sv = Log::stringToSeverity (params["severity"].asString ());

    if (sv == lsINVALID)
        return rpcError (rpcINVALID_PARAMS);

    // log_level severity
    if (!params.isMember ("partition"))
    {
        // set base log severity
        LogSink::get()->setMinSeverity (sv, true);
        return Json::objectValue;
    }

    // log_level partition severity base?
    if (params.isMember ("partition"))
    {
        // set partition severity
        std::string partition (params["partition"].asString ());

        if (boost::iequals (partition, "base"))
            LogSink::get()->setMinSeverity (sv, false);
        else if (!LogPartition::setSeverity (partition, sv))
            return rpcError (rpcINVALID_PARAMS);

        return Json::objectValue;
    }

    return rpcError (rpcINVALID_PARAMS);
}

// {
//   node: <domain>|<node_public>,
//   comment: <comment>             // optional
// }
Json::Value RPCHandler::doUnlAdd (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& masterLockHolder)
{
    std::string strNode     = params.isMember ("node") ? params["node"].asString () : "";
    std::string strComment  = params.isMember ("comment") ? params["comment"].asString () : "";

    RippleAddress   raNodePublic;

    if (raNodePublic.setNodePublic (strNode))
    {
        getApp().getUNL ().nodeAddPublic (raNodePublic, UniqueNodeList::vsManual, strComment);

        return "adding node by public key";
    }
    else
    {
        getApp().getUNL ().nodeAddDomain (strNode, UniqueNodeList::vsManual, strComment);

        return "adding node by domain";
    }
}

// {
//   node: <domain>|<public_key>
// }
Json::Value RPCHandler::doUnlDelete (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& masterLockHolder)
{
    if (!params.isMember ("node"))
        return rpcError (rpcINVALID_PARAMS);

    std::string strNode     = params["node"].asString ();

    RippleAddress   raNodePublic;

    if (raNodePublic.setNodePublic (strNode))
    {
        getApp().getUNL ().nodeRemovePublic (raNodePublic);

        return "removing node by public key";
    }
    else
    {
        getApp().getUNL ().nodeRemoveDomain (strNode);

        return "removing node by domain";
    }
}

Json::Value RPCHandler::doUnlList (Json::Value, Resource::Charge& loadType, Application::ScopedLockType& masterLockHolder)
{
    Json::Value obj (Json::objectValue);

    obj["unl"] = getApp().getUNL ().getUnlJson ();

    return obj;
}

// Populate the UNL from a local validators.txt file.
Json::Value RPCHandler::doUnlLoad (Json::Value, Resource::Charge& loadType, Application::ScopedLockType& masterLockHolder)
{
    if (getConfig ().VALIDATORS_FILE.empty () || !getApp().getUNL ().nodeLoad (getConfig ().VALIDATORS_FILE))
    {
        return rpcError (rpcLOAD_FAILED);
    }

    return "loading";
}


// Populate the UNL from ripple.com's validators.txt file.
Json::Value RPCHandler::doUnlNetwork (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& masterLockHolder)
{
    getApp().getUNL ().nodeNetwork ();

    return "fetching";
}

// unl_reset
Json::Value RPCHandler::doUnlReset (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& masterLockHolder)
{
    getApp().getUNL ().nodeReset ();

    return "removing nodes";
}

// unl_score
Json::Value RPCHandler::doUnlScore (Json::Value, Resource::Charge& loadType, Application::ScopedLockType& masterLockHolder)
{
    getApp().getUNL ().nodeScore ();

    return "scoring requested";
}

Json::Value RPCHandler::doSMS (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& masterLockHolder)
{
    masterLockHolder.unlock ();
    if (!params.isMember ("text"))
        return rpcError (rpcINVALID_PARAMS);

    HTTPClient::sendSMS (getApp().getIOService (), params["text"].asString ());

    return "sms dispatched";
}
Json::Value RPCHandler::doStop (Json::Value, Resource::Charge& loadType, Application::ScopedLockType& masterLockHolder)
{
    getApp().signalStop ();

    return SYSTEM_NAME " server stopping";
}

Json::Value RPCHandler::doLedgerAccept (Json::Value, Resource::Charge& loadType, Application::ScopedLockType& masterLockHolder)
{
    Json::Value jvResult;

    if (!getConfig ().RUN_STANDALONE)
    {
        jvResult["error"]   = "notStandAlone";
    }
    else
    {
        mNetOps->acceptLedger ();

        jvResult["ledger_current_index"]    = mNetOps->getCurrentLedgerID ();
    }

    return jvResult;
}

Json::Value RPCHandler::doLedgerCleaner (Json::Value parameters, Resource::Charge& loadType, Application::ScopedLockType& masterLockHolder)
{
    masterLockHolder.unlock();
    getApp().getLedgerMaster().doLedgerCleaner (parameters);
    return "Cleaner configured";
}

// {
//   ledger_hash : <ledger>,
//   ledger_index : <ledger_index>
// }
// XXX In this case, not specify either ledger does not mean ledger current. It means any ledger.
Json::Value RPCHandler::doTransactionEntry (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& masterLockHolder)
{
    masterLockHolder.unlock ();

    Ledger::pointer     lpLedger;
    Json::Value         jvResult    = lookupLedger (params, lpLedger);

    if (!lpLedger)
        return jvResult;

    if (!params.isMember ("tx_hash"))
    {
        jvResult["error"]   = "fieldNotFoundTransaction";
    }
    else if (!params.isMember ("ledger_hash") && !params.isMember ("ledger_index"))
    {
        // We don't work on ledger current.

        jvResult["error"]   = "notYetImplemented";  // XXX We don't support any transaction yet.
    }
    else
    {
        uint256                     uTransID;
        // XXX Relying on trusted WSS client. Would be better to have a strict routine, returning success or failure.
        uTransID.SetHex (params["tx_hash"].asString ());

        if (!lpLedger)
        {
            jvResult["error"]   = "ledgerNotFound";
        }
        else
        {
            Transaction::pointer        tpTrans;
            TransactionMetaSet::pointer tmTrans;

            if (!lpLedger->getTransaction (uTransID, tpTrans, tmTrans))
            {
                jvResult["error"]   = "transactionNotFound";
            }
            else
            {
                jvResult["tx_json"]     = tpTrans->getJson (0);
                if (tmTrans)
                    jvResult["metadata"]    = tmTrans->getJson (0);
                // 'accounts'
                // 'engine_...'
                // 'ledger_...'
            }
        }
    }

    return jvResult;
}

// The previous version of the lookupLedger command would accept the
// "ledger_index" argument as a string and silently treat it as a request to
// return the current ledger which, while not strictly wrong, could cause a
// lot of confusion.
//
// The code now robustly validates the input and ensures that the only possible
// values for the "ledger_index" parameter are the index of a ledger passed as
// an integer or one of the strings "current", "closed" or "validated".
// Additionally, the code ensures that the value passed in "ledger_hash" is a
// string and a valid hash. Invalid values will return an appropriate error
// code.
//
// In the absence of the "ledger_hash" or "ledger_index" parameters, the code
// assumes that "ledger_index" has the value "current".
Json::Value RPCHandler::lookupLedger (Json::Value const& params, Ledger::pointer& lpLedger)
{
    Json::Value jvResult;

    Json::Value ledger_hash = params.get ("ledger_hash", Json::Value ("0"));
    Json::Value ledger_index = params.get ("ledger_index", Json::Value ("current"));

    // Support for DEPRECATED "ledger" - attempt to deduce our input
    if (params.isMember ("ledger"))
    {
        if (params["ledger"].asString ().size () > 12)
        {
            ledger_hash = params["ledger"];
            ledger_index = Json::Value ("");
        }
        else if (params["ledger"].isNumeric ())
        {
            ledger_index = params["ledger"];
            ledger_hash = Json::Value ("0");
        }
        else
        {
            ledger_index = params["ledger"];
            ledger_hash = Json::Value ("0");
        }
    }

    uint256 uLedger (0);

    if (!ledger_hash.isString() || !uLedger.SetHex (ledger_hash.asString ()))
    {
        jvResult["error"] = "ledgerHashMalformed";
        return jvResult;
    }

    std::int32_t iLedgerIndex = LEDGER_CURRENT;

    // We only try to parse a ledger index if we have not already
    // determined that we have a ledger hash.
    if (!uLedger)
    {
        if (ledger_index.isNumeric ())
            iLedgerIndex = ledger_index.asInt ();
        else
        {
            std::string strLedger = ledger_index.asString ();

            if (strLedger == "current")
            {
                iLedgerIndex = LEDGER_CURRENT;
            }
            else if (strLedger == "closed")
            {
                iLedgerIndex = LEDGER_CLOSED;
            }
            else if (strLedger == "validated")
            {
                iLedgerIndex = LEDGER_VALIDATED;
            }
            else
            {
                jvResult["error"] = "ledgerIndexMalformed";
                return jvResult;
            }
        }
    }

    // The ledger was directly specified by hash.
    if (!!uLedger)
    {
        lpLedger = mNetOps->getLedgerByHash (uLedger);

        if (!lpLedger)
        {
            jvResult["error"] = "ledgerNotFound";
            return jvResult;
        }

        iLedgerIndex = lpLedger->getLedgerSeq ();
    }

    switch (iLedgerIndex)
    {
    case LEDGER_CURRENT:
        lpLedger = mNetOps->getCurrentLedger ();
        iLedgerIndex = lpLedger->getLedgerSeq ();
        assert (lpLedger->isImmutable () && !lpLedger->isClosed ());
        break;

    case LEDGER_CLOSED:
        lpLedger = getApp().getLedgerMaster ().getClosedLedger ();
        iLedgerIndex = lpLedger->getLedgerSeq ();
        assert (lpLedger->isImmutable () && lpLedger->isClosed ());
        break;

    case LEDGER_VALIDATED:
        lpLedger = mNetOps->getValidatedLedger ();
        iLedgerIndex = lpLedger->getLedgerSeq ();
        assert (lpLedger->isImmutable () && lpLedger->isClosed ());
        break;
    }

    if (iLedgerIndex <= 0)
    {
        jvResult["error"] = "ledgerIndexMalformed";
        return jvResult;
    }

    if (!lpLedger)
    {
        lpLedger = mNetOps->getLedgerBySeq (iLedgerIndex);

        if (!lpLedger)
        {
            jvResult["error"] = "ledgerNotFound"; // ledger_index from future?
            return jvResult;
        }
    }

    if (lpLedger->isClosed ())
    {
        if (!!uLedger)
            jvResult["ledger_hash"] = uLedger.ToString ();

        jvResult["ledger_index"] = iLedgerIndex;
    }
    else
    {
        // CHECKME - What is this supposed to signify?
        jvResult["ledger_current_index"] = iLedgerIndex;
    }

    return jvResult;
}

// {
//   ledger_hash : <ledger>
//   ledger_index : <ledger_index>
//   ...
// }
Json::Value RPCHandler::doLedgerEntry (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& masterLockHolder)
{
    masterLockHolder.unlock ();

    Ledger::pointer     lpLedger;
    Json::Value         jvResult    = lookupLedger (params, lpLedger);

    if (!lpLedger)
        return jvResult;

    uint256     uNodeIndex;
    bool        bNodeBinary = false;

    if (params.isMember ("index"))
    {
        // XXX Needs to provide proof.
        uNodeIndex.SetHex (params["index"].asString ());
        bNodeBinary = true;
    }
    else if (params.isMember ("account_root"))
    {
        RippleAddress   naAccount;

        if (!naAccount.setAccountID (params["account_root"].asString ())
                || !naAccount.getAccountID ())
        {
            jvResult["error"]   = "malformedAddress";
        }
        else
        {
            uNodeIndex = Ledger::getAccountRootIndex (naAccount.getAccountID ());
        }
    }
    else if (params.isMember ("directory"))
    {
        if (!params["directory"].isObject ())
        {
            uNodeIndex.SetHex (params["directory"].asString ());
        }
        else if (params["directory"].isMember ("sub_index")
                 && !params["directory"]["sub_index"].isIntegral ())
        {
            jvResult["error"]   = "malformedRequest";
        }
        else
        {
            std::uint64_t  uSubIndex = params["directory"].isMember ("sub_index")
                                ? params["directory"]["sub_index"].asUInt ()
                                : 0;

            if (params["directory"].isMember ("dir_root"))
            {
                uint256 uDirRoot;

                uDirRoot.SetHex (params["dir_root"].asString ());

                uNodeIndex  = Ledger::getDirNodeIndex (uDirRoot, uSubIndex);
            }
            else if (params["directory"].isMember ("owner"))
            {
                RippleAddress   naOwnerID;

                if (!naOwnerID.setAccountID (params["directory"]["owner"].asString ()))
                {
                    jvResult["error"]   = "malformedAddress";
                }
                else
                {
                    uint256 uDirRoot    = Ledger::getOwnerDirIndex (naOwnerID.getAccountID ());

                    uNodeIndex  = Ledger::getDirNodeIndex (uDirRoot, uSubIndex);
                }
            }
            else
            {
                jvResult["error"]   = "malformedRequest";
            }
        }
    }
    else if (params.isMember ("generator"))
    {
        RippleAddress   naGeneratorID;

        if (!params["generator"].isObject ())
        {
            uNodeIndex.SetHex (params["generator"].asString ());
        }
        else if (!params["generator"].isMember ("regular_seed"))
        {
            jvResult["error"]   = "malformedRequest";
        }
        else if (!naGeneratorID.setSeedGeneric (params["generator"]["regular_seed"].asString ()))
        {
            jvResult["error"]   = "malformedAddress";
        }
        else
        {
            RippleAddress       na0Public;      // To find the generator's index.
            RippleAddress       naGenerator = RippleAddress::createGeneratorPublic (naGeneratorID);

            na0Public.setAccountPublic (naGenerator, 0);

            uNodeIndex  = Ledger::getGeneratorIndex (na0Public.getAccountID ());
        }
    }
    else if (params.isMember ("offer"))
    {
        RippleAddress   naAccountID;

        if (!params["offer"].isObject ())
        {
            uNodeIndex.SetHex (params["offer"].asString ());
        }
        else if (!params["offer"].isMember ("account")
                 || !params["offer"].isMember ("seq")
                 || !params["offer"]["seq"].isIntegral ())
        {
            jvResult["error"]   = "malformedRequest";
        }
        else if (!naAccountID.setAccountID (params["offer"]["account"].asString ()))
        {
            jvResult["error"]   = "malformedAddress";
        }
        else
        {
            std::uint32_t      uSequence   = params["offer"]["seq"].asUInt ();

            uNodeIndex  = Ledger::getOfferIndex (naAccountID.getAccountID (), uSequence);
        }
    }
    else if (params.isMember ("ripple_state"))
    {
        RippleAddress   naA;
        RippleAddress   naB;
        uint160         uCurrency;
        Json::Value     jvRippleState   = params["ripple_state"];

        if (!jvRippleState.isObject ()
                || !jvRippleState.isMember ("currency")
                || !jvRippleState.isMember ("accounts")
                || !jvRippleState["accounts"].isArray ()
                || 2 != jvRippleState["accounts"].size ()
                || !jvRippleState["accounts"][0u].isString ()
                || !jvRippleState["accounts"][1u].isString ()
                || jvRippleState["accounts"][0u].asString () == jvRippleState["accounts"][1u].asString ()
           )
        {
            jvResult["error"]   = "malformedRequest";
        }
        else if (!naA.setAccountID (jvRippleState["accounts"][0u].asString ())
                 || !naB.setAccountID (jvRippleState["accounts"][1u].asString ()))
        {
            jvResult["error"]   = "malformedAddress";
        }
        else if (!STAmount::currencyFromString (uCurrency, jvRippleState["currency"].asString ()))
        {
            jvResult["error"]   = "malformedCurrency";
        }
        else
        {
            uNodeIndex  = Ledger::getRippleStateIndex (naA, naB, uCurrency);
        }
    }
    else
    {
        jvResult["error"]   = "unknownOption";
    }

    if (uNodeIndex.isNonZero ())
    {
        SLE::pointer    sleNode = mNetOps->getSLEi (lpLedger, uNodeIndex);

        if (params.isMember("binary"))
            bNodeBinary = params["binary"].asBool();

        if (!sleNode)
        {
            // Not found.
            // XXX Should also provide proof.
            jvResult["error"]       = "entryNotFound";
        }
        else if (bNodeBinary)
        {
            // XXX Should also provide proof.
            Serializer s;

            sleNode->add (s);

            jvResult["node_binary"] = strHex (s.peekData ());
            jvResult["index"]       = uNodeIndex.ToString ();
        }
        else
        {
            jvResult["node"]        = sleNode->getJson (0);
            jvResult["index"]       = uNodeIndex.ToString ();
        }
    }

    return jvResult;
}

// {
//   ledger_hash : <ledger>
//   ledger_index : <ledger_index>
// }
Json::Value RPCHandler::doLedgerHeader (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& masterLockHolder)
{
    masterLockHolder.unlock ();

    Ledger::pointer     lpLedger;
    Json::Value         jvResult    = lookupLedger (params, lpLedger);

    if (!lpLedger)
        return jvResult;

    Serializer  s;

    lpLedger->addRaw (s);

    jvResult["ledger_data"] = strHex (s.peekData ());

    // This information isn't verified, they should only use it if they trust us.
    lpLedger->addJson (jvResult, 0);

    return jvResult;
}

boost::unordered_set<RippleAddress> RPCHandler::parseAccountIds (const Json::Value& jvArray)
{
    boost::unordered_set<RippleAddress> usnaResult;

    for (Json::Value::const_iterator it = jvArray.begin (); it != jvArray.end (); it++)
    {
        RippleAddress   naString;

        if (! (*it).isString () || !naString.setAccountID ((*it).asString ()))
        {
            usnaResult.clear ();
            break;
        }
        else
        {
            (void) usnaResult.insert (naString);
        }
    }

    return usnaResult;
}

Json::Value RPCHandler::doSubscribe (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& masterLockHolder)
{
    // FIXME: This needs to release the master lock immediately
    // Subscriptions need to be protected by their own lock

    InfoSub::pointer ispSub;
    Json::Value jvResult (Json::objectValue);
    std::uint32_t uLedgerIndex = params.isMember ("ledger_index") && params["ledger_index"].isNumeric ()
                               ? params["ledger_index"].asUInt ()
                               : 0;

    if (!mInfoSub && !params.isMember ("url"))
    {
        // Must be a JSON-RPC call.
        WriteLog (lsINFO, RPCHandler) << boost::str (boost::format ("doSubscribe: RPC subscribe requires a url"));

        return rpcError (rpcINVALID_PARAMS);
    }

    if (params.isMember ("url"))
    {
        if (mRole != Config::ADMIN)
            return rpcError (rpcNO_PERMISSION);

        std::string strUrl      = params["url"].asString ();
        std::string strUsername = params.isMember ("url_username") ? params["url_username"].asString () : "";
        std::string strPassword = params.isMember ("url_password") ? params["url_password"].asString () : "";

        // DEPRECATED
        if (params.isMember ("username"))
            strUsername = params["username"].asString ();

        // DEPRECATED
        if (params.isMember ("password"))
            strPassword = params["password"].asString ();

        ispSub  = mNetOps->findRpcSub (strUrl);

        if (!ispSub)
        {
            WriteLog (lsDEBUG, RPCHandler) << boost::str (boost::format ("doSubscribe: building: %s") % strUrl);

            RPCSub::pointer rspSub = RPCSub::New (getApp ().getOPs (),
                getApp ().getIOService (), getApp ().getJobQueue (),
                    strUrl, strUsername, strPassword);
            ispSub  = mNetOps->addRpcSub (strUrl, boost::dynamic_pointer_cast<InfoSub> (rspSub));
        }
        else
        {
            WriteLog (lsTRACE, RPCHandler) << boost::str (boost::format ("doSubscribe: reusing: %s") % strUrl);

            if (params.isMember ("username"))
                dynamic_cast<RPCSub*> (&*ispSub)->setUsername (strUsername);

            if (params.isMember ("password"))
                dynamic_cast<RPCSub*> (&*ispSub)->setPassword (strPassword);
        }
    }
    else
    {
        ispSub  = mInfoSub;
    }

    if (!params.isMember ("streams"))
    {
        nothing ();
    }
    else if (!params["streams"].isArray ())
    {
        WriteLog (lsINFO, RPCHandler) << boost::str (boost::format ("doSubscribe: streams requires an array."));

        return rpcError (rpcINVALID_PARAMS);
    }
    else
    {
        for (Json::Value::iterator it = params["streams"].begin (); it != params["streams"].end (); it++)
        {
            if ((*it).isString ())
            {
                std::string streamName = (*it).asString ();

                if (streamName == "server")
                {
                    mNetOps->subServer (ispSub, jvResult);
                }
                else if (streamName == "ledger")
                {
                    mNetOps->subLedger (ispSub, jvResult);
                }
                else if (streamName == "transactions")
                {
                    mNetOps->subTransactions (ispSub);
                }
                else if (streamName == "transactions_proposed"
                         || streamName == "rt_transactions") // DEPRECATED
                {
                    mNetOps->subRTTransactions (ispSub);
                }
                else
                {
                    jvResult["error"]   = "unknownStream";
                }
            }
            else
            {
                jvResult["error"]   = "malformedStream";
            }
        }
    }

    std::string strAccountsProposed = params.isMember ("accounts_proposed")
                                      ? "accounts_proposed"
                                      : "rt_accounts";                                    // DEPRECATED

    if (!params.isMember (strAccountsProposed))
    {
        nothing ();
    }
    else if (!params[strAccountsProposed].isArray ())
    {
        return rpcError (rpcINVALID_PARAMS);
    }
    else
    {
        boost::unordered_set<RippleAddress> usnaAccoundIds  = parseAccountIds (params[strAccountsProposed]);

        if (usnaAccoundIds.empty ())
        {
            jvResult["error"]   = "malformedAccount";
        }
        else
        {
            mNetOps->subAccount (ispSub, usnaAccoundIds, uLedgerIndex, true);
        }
    }

    if (!params.isMember ("accounts"))
    {
        nothing ();

    }
    else if (!params["accounts"].isArray ())
    {
        return rpcError (rpcINVALID_PARAMS);
    }
    else
    {
        boost::unordered_set<RippleAddress> usnaAccoundIds  = parseAccountIds (params["accounts"]);

        if (usnaAccoundIds.empty ())
        {
            jvResult["error"]   = "malformedAccount";
        }
        else
        {
            mNetOps->subAccount (ispSub, usnaAccoundIds, uLedgerIndex, false);

            WriteLog (lsDEBUG, RPCHandler) << boost::str (boost::format ("doSubscribe: accounts: %d") % usnaAccoundIds.size ());
        }
    }

    bool bHaveMasterLock = true;
    if (!params.isMember ("books"))
    {
        nothing ();
    }
    else if (!params["books"].isArray ())
    {
        return rpcError (rpcINVALID_PARAMS);
    }
    else
    {
        for (Json::Value::iterator it = params["books"].begin (); it != params["books"].end (); it++)
        {
            Json::Value&    jvSubRequest    = *it;

            if (!jvSubRequest.isObject ()
                    || !jvSubRequest.isMember ("taker_pays")
                    || !jvSubRequest.isMember ("taker_gets")
                    || !jvSubRequest["taker_pays"].isObject ()
                    || !jvSubRequest["taker_gets"].isObject ())
                return rpcError (rpcINVALID_PARAMS);

            // VFALCO TODO Use RippleAsset here
            RippleCurrency pay_currency;
            RippleIssuer   pay_issuer;
            RippleCurrency get_currency;
            RippleIssuer   get_issuer;

            bool            bBoth           = (jvSubRequest.isMember ("both") && jvSubRequest["both"].asBool ())
                                              || (jvSubRequest.isMember ("both_sides") && jvSubRequest["both_sides"].asBool ());  // DEPRECATED
            bool            bSnapshot       = (jvSubRequest.isMember ("snapshot") && jvSubRequest["snapshot"].asBool ())
                                              || (jvSubRequest.isMember ("state_now") && jvSubRequest["state_now"].asBool ());    // DEPRECATED

            Json::Value     taker_pays     = jvSubRequest["taker_pays"];
            Json::Value     taker_gets     = jvSubRequest["taker_gets"];

            // Parse mandatory currency.
            if (!taker_pays.isMember ("currency")
                    || !STAmount::currencyFromString (pay_currency, taker_pays["currency"].asString ()))
            {
                WriteLog (lsINFO, RPCHandler) << "Bad taker_pays currency.";

                return rpcError (rpcSRC_CUR_MALFORMED);
            }
            // Parse optional issuer.
            else if (((taker_pays.isMember ("issuer"))
                      && (!taker_pays["issuer"].isString ()
                          || !STAmount::issuerFromString (pay_issuer, taker_pays["issuer"].asString ())))
                     // Don't allow illegal issuers.
                     || (!pay_currency != !pay_issuer)
                     || ACCOUNT_ONE == pay_issuer)
            {
                WriteLog (lsINFO, RPCHandler) << "Bad taker_pays issuer.";

                return rpcError (rpcSRC_ISR_MALFORMED);
            }

            // Parse mandatory currency.
            if (!taker_gets.isMember ("currency")
                    || !STAmount::currencyFromString (get_currency, taker_gets["currency"].asString ()))
            {
                WriteLog (lsINFO, RPCHandler) << "Bad taker_pays currency.";

                return rpcError (rpcSRC_CUR_MALFORMED);
            }
            // Parse optional issuer.
            else if (((taker_gets.isMember ("issuer"))
                      && (!taker_gets["issuer"].isString ()
                          || !STAmount::issuerFromString (get_issuer, taker_gets["issuer"].asString ())))
                     // Don't allow illegal issuers.
                     || (!get_currency != !get_issuer)
                     || ACCOUNT_ONE == get_issuer)
            {
                WriteLog (lsINFO, RPCHandler) << "Bad taker_gets issuer.";

                return rpcError (rpcDST_ISR_MALFORMED);
            }

            if (pay_currency == get_currency
                    && pay_issuer == get_issuer)
            {
                WriteLog (lsINFO, RPCHandler) << "taker_gets same as taker_pays.";

                return rpcError (rpcBAD_MARKET);
            }

            RippleAddress   raTakerID;

            if (!jvSubRequest.isMember ("taker"))
            {
                raTakerID.setAccountID (ACCOUNT_ONE);
            }
            else if (!raTakerID.setAccountID (jvSubRequest["taker"].asString ()))
            {
                return rpcError (rpcBAD_ISSUER);
            }

            if (!Ledger::isValidBook (pay_currency, pay_issuer, get_currency, get_issuer))
            {
                WriteLog (lsWARNING, RPCHandler) << "Bad market: " <<
                                                 pay_currency << ":" << pay_issuer << " -> " <<
                                                 get_currency << ":" << get_issuer;
                return rpcError (rpcBAD_MARKET);
            }

            mNetOps->subBook (ispSub, pay_currency, get_currency, pay_issuer, get_issuer);

            if (bBoth) mNetOps->subBook (ispSub, get_currency, pay_currency, get_issuer, pay_issuer);

            if (bSnapshot)
            {
                if (bHaveMasterLock)
                {
                    masterLockHolder.unlock ();
                    bHaveMasterLock = false;
                }

                loadType = Resource::feeMediumBurdenRPC;
                Ledger::pointer     lpLedger = getApp().getLedgerMaster ().getPublishedLedger ();
                if (lpLedger)
                {
                    const Json::Value   jvMarker = Json::Value (Json::nullValue);

                    if (bBoth)
                    {
                        Json::Value jvBids (Json::objectValue);
                        Json::Value jvAsks (Json::objectValue);

                        mNetOps->getBookPage (lpLedger, pay_currency, pay_issuer, get_currency, get_issuer, raTakerID.getAccountID (), false, 0, jvMarker, jvBids);

                        if (jvBids.isMember ("offers")) jvResult["bids"] = jvBids["offers"];

                        mNetOps->getBookPage (lpLedger, get_currency, get_issuer, pay_currency, pay_issuer, raTakerID.getAccountID (), false, 0, jvMarker, jvAsks);

                        if (jvAsks.isMember ("offers")) jvResult["asks"] = jvAsks["offers"];
                    }
                    else
                    {
                        mNetOps->getBookPage (lpLedger, pay_currency, pay_issuer, get_currency, get_issuer, raTakerID.getAccountID (), false, 0, jvMarker, jvResult);
                    }
                }
            }
        }
    }

    return jvResult;
}

// FIXME: This leaks RPCSub objects for JSON-RPC.  Shouldn't matter for anyone sane.
Json::Value RPCHandler::doUnsubscribe (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& masterLockHolder)
{
    InfoSub::pointer ispSub;
    Json::Value jvResult (Json::objectValue);

    if (!mInfoSub && !params.isMember ("url"))
    {
        // Must be a JSON-RPC call.
        return rpcError (rpcINVALID_PARAMS);
    }

    if (params.isMember ("url"))
    {
        if (mRole != Config::ADMIN)
            return rpcError (rpcNO_PERMISSION);

        std::string strUrl  = params["url"].asString ();

        ispSub  = mNetOps->findRpcSub (strUrl);

        if (!ispSub)
            return jvResult;
    }
    else
    {
        ispSub  = mInfoSub;
    }

    if (params.isMember ("streams"))
    {
        for (Json::Value::iterator it = params["streams"].begin (); it != params["streams"].end (); it++)
        {
            if ((*it).isString ())
            {
                std::string streamName = (*it).asString ();

                if (streamName == "server")
                {
                    mNetOps->unsubServer (ispSub->getSeq ());
                }
                else if (streamName == "ledger")
                {
                    mNetOps->unsubLedger (ispSub->getSeq ());
                }
                else if (streamName == "transactions")
                {
                    mNetOps->unsubTransactions (ispSub->getSeq ());
                }
                else if (streamName == "transactions_proposed"
                         || streamName == "rt_transactions")         // DEPRECATED
                {
                    mNetOps->unsubRTTransactions (ispSub->getSeq ());
                }
                else
                {
                    jvResult["error"]   = str (boost::format ("Unknown stream: %s") % streamName);
                }
            }
            else
            {
                jvResult["error"]   = "malformedSteam";
            }
        }
    }

    if (params.isMember ("accounts_proposed") || params.isMember ("rt_accounts"))
    {
        boost::unordered_set<RippleAddress> usnaAccoundIds  = parseAccountIds (
                    params.isMember ("accounts_proposed")
                    ? params["accounts_proposed"]
                    : params["rt_accounts"]);                    // DEPRECATED

        if (usnaAccoundIds.empty ())
        {
            jvResult["error"]   = "malformedAccount";
        }
        else
        {
            mNetOps->unsubAccount (ispSub->getSeq (), usnaAccoundIds, true);
        }
    }

    if (params.isMember ("accounts"))
    {
        boost::unordered_set<RippleAddress> usnaAccoundIds  = parseAccountIds (params["accounts"]);

        if (usnaAccoundIds.empty ())
        {
            jvResult["error"]   = "malformedAccount";
        }
        else
        {
            mNetOps->unsubAccount (ispSub->getSeq (), usnaAccoundIds, false);
        }
    }

    if (!params.isMember ("books"))
    {
        nothing ();
    }
    else if (!params["books"].isArray ())
    {
        return rpcError (rpcINVALID_PARAMS);
    }
    else
    {
        for (Json::Value::iterator it = params["books"].begin (); it != params["books"].end (); it++)
        {
            Json::Value&    jvSubRequest    = *it;

            if (!jvSubRequest.isObject ()
                    || !jvSubRequest.isMember ("taker_pays")
                    || !jvSubRequest.isMember ("taker_gets")
                    || !jvSubRequest["taker_pays"].isObject ()
                    || !jvSubRequest["taker_gets"].isObject ())
                return rpcError (rpcINVALID_PARAMS);

            uint160         pay_currency;
            uint160         pay_issuer;
            uint160         get_currency;
            uint160         get_issuer;
            bool            bBoth           = (jvSubRequest.isMember ("both") && jvSubRequest["both"].asBool ())
                                              || (jvSubRequest.isMember ("both_sides") && jvSubRequest["both_sides"].asBool ());  // DEPRECATED

            Json::Value     taker_pays     = jvSubRequest["taker_pays"];
            Json::Value     taker_gets     = jvSubRequest["taker_gets"];

            // Parse mandatory currency.
            if (!taker_pays.isMember ("currency")
                    || !STAmount::currencyFromString (pay_currency, taker_pays["currency"].asString ()))
            {
                WriteLog (lsINFO, RPCHandler) << "Bad taker_pays currency.";

                return rpcError (rpcSRC_CUR_MALFORMED);
            }
            // Parse optional issuer.
            else if (((taker_pays.isMember ("issuer"))
                      && (!taker_pays["issuer"].isString ()
                          || !STAmount::issuerFromString (pay_issuer, taker_pays["issuer"].asString ())))
                     // Don't allow illegal issuers.
                     || (!pay_currency != !pay_issuer)
                     || ACCOUNT_ONE == pay_issuer)
            {
                WriteLog (lsINFO, RPCHandler) << "Bad taker_pays issuer.";

                return rpcError (rpcSRC_ISR_MALFORMED);
            }

            // Parse mandatory currency.
            if (!taker_gets.isMember ("currency")
                    || !STAmount::currencyFromString (get_currency, taker_gets["currency"].asString ()))
            {
                WriteLog (lsINFO, RPCHandler) << "Bad taker_pays currency.";

                return rpcError (rpcSRC_CUR_MALFORMED);
            }
            // Parse optional issuer.
            else if (((taker_gets.isMember ("issuer"))
                      && (!taker_gets["issuer"].isString ()
                          || !STAmount::issuerFromString (get_issuer, taker_gets["issuer"].asString ())))
                     // Don't allow illegal issuers.
                     || (!get_currency != !get_issuer)
                     || ACCOUNT_ONE == get_issuer)
            {
                WriteLog (lsINFO, RPCHandler) << "Bad taker_gets issuer.";

                return rpcError (rpcDST_ISR_MALFORMED);
            }

            if (pay_currency == get_currency
                    && pay_issuer == get_issuer)
            {
                WriteLog (lsINFO, RPCHandler) << "taker_gets same as taker_pays.";

                return rpcError (rpcBAD_MARKET);
            }

            mNetOps->unsubBook (ispSub->getSeq (), pay_currency, get_currency, pay_issuer, get_issuer);

            if (bBoth) mNetOps->unsubBook (ispSub->getSeq (), get_currency, pay_currency, get_issuer, pay_issuer);
        }
    }

    return jvResult;
}

// Provide the JSON-RPC "result" value.
//
// JSON-RPC provides a method and an array of params. JSON-RPC is used as a transport for a command and a request object. The
// command is the method. The request object is supplied as the first element of the params.
Json::Value RPCHandler::doRpcCommand (const std::string& strMethod, Json::Value const& jvParams, int iRole, Resource::Charge& loadType)
{
    WriteLog (lsTRACE, RPCHandler) << "doRpcCommand:" << strMethod << ":" << jvParams;

    if (!jvParams.isArray () || jvParams.size () > 1)
        return logRPCError (rpcError (rpcINVALID_PARAMS));

    Json::Value params   = jvParams.size () ? jvParams[0u] : Json::Value (Json::objectValue);

    if (!params.isObject ())
        return logRPCError (rpcError (rpcINVALID_PARAMS));

    // Provide the JSON-RPC method as the field "command" in the request.
    params["command"]    = strMethod;

    Json::Value jvResult = doCommand (params, iRole, loadType);

    // Always report "status".  On an error report the request as received.
    if (jvResult.isMember ("error"))
    {
        jvResult["status"]  = "error";
        jvResult["request"] = params;

    }
    else
    {
        jvResult["status"]  = "success";
    }

    return logRPCError (jvResult);
}

Json::Value RPCHandler::doInternal (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& masterLockHolder)
{
    // Used for debug or special-purpose RPC commands
    if (!params.isMember ("internal_command"))
        return rpcError (rpcINVALID_PARAMS);

    return RPCInternalHandler::runHandler (params["internal_command"].asString (), params["params"]);
}

Json::Value RPCHandler::doCommand (const Json::Value& params, int iRole, Resource::Charge& loadType)
{
    if (iRole != Config::ADMIN)
    {
        // VFALCO NOTE Should we also add up the jtRPC jobs?
        //
        int jc = getApp().getJobQueue ().getJobCountGE (jtCLIENT);

        if (jc > 500)
        {
            WriteLog (lsDEBUG, RPCHandler) << "Too busy for command: " << jc;
            return rpcError (rpcTOO_BUSY);
        }
    }

    if (!params.isMember ("command"))
        return rpcError (rpcCOMMAND_MISSING);

    std::string     strCommand  = params["command"].asString ();

    WriteLog (lsTRACE, RPCHandler) << "COMMAND:" << strCommand;
    WriteLog (lsTRACE, RPCHandler) << "REQUEST:" << params;

    mRole   = iRole;

    static struct
    {
        const char*     pCommand;
        doFuncPtr       dfpFunc;
        bool            bAdminRequired;
        unsigned int    iOptions;
    } commandsA[] =
    {
        // Request-response methods
        {   "account_info",         &RPCHandler::doAccountInfo,         false,  optCurrent  },
        {   "account_currencies",   &RPCHandler::doAccountCurrencies,   false,  optCurrent  },
        {   "account_lines",        &RPCHandler::doAccountLines,        false,  optCurrent  },
        {   "account_offers",       &RPCHandler::doAccountOffers,       false,  optCurrent  },
        {   "account_tx",           &RPCHandler::doAccountTxSwitch,     false,  optNetwork  },
        {   "blacklist",            &RPCHandler::doBlackList,           true,   optNone     },
        {   "book_offers",          &RPCHandler::doBookOffers,          false,  optCurrent  },
        {   "connect",              &RPCHandler::doConnect,             true,   optNone     },
        {   "consensus_info",       &RPCHandler::doConsensusInfo,       true,   optNone     },
        {   "get_counts",           &RPCHandler::doGetCounts,           true,   optNone     },
        {   "internal",             &RPCHandler::doInternal,            true,   optNone     },
        {   "feature",              &RPCHandler::doFeature,             true,   optNone     },
        {   "fetch_info",           &RPCHandler::doFetchInfo,           true,   optNone     },
        {   "ledger",               &RPCHandler::doLedger,              false,  optNetwork  },
        {   "ledger_accept",        &RPCHandler::doLedgerAccept,        true,   optCurrent  },
        {   "ledger_cleaner",       &RPCHandler::doLedgerCleaner,       true,   optNetwork  },
        {   "ledger_closed",        &RPCHandler::doLedgerClosed,        false,  optClosed   },
        {   "ledger_current",       &RPCHandler::doLedgerCurrent,       false,  optCurrent  },
        {   "ledger_data",          &RPCHandler::doLedgerData,          false,  optCurrent  },
        {   "ledger_entry",         &RPCHandler::doLedgerEntry,         false,  optCurrent  },
        {   "ledger_header",        &RPCHandler::doLedgerHeader,        false,  optCurrent  },
        {   "log_level",            &RPCHandler::doLogLevel,            true,   optNone     },
        {   "logrotate",            &RPCHandler::doLogRotate,           true,   optNone     },
//      {   "nickname_info",        &RPCHandler::doNicknameInfo,        false,  optCurrent  },
        {   "owner_info",           &RPCHandler::doOwnerInfo,           false,  optCurrent  },
        {   "peers",                &RPCHandler::doPeers,               true,   optNone     },
        {   "path_find",            &RPCHandler::doPathFind,            false,  optCurrent  },
        {   "ping",                 &RPCHandler::doPing,                false,  optNone     },
        {   "print",                &RPCHandler::doPrint,               true,   optNone     },
//      {   "profile",              &RPCHandler::doProfile,             false,  optCurrent  },
        {   "proof_create",         &RPCHandler::doProofCreate,         true,   optNone     },
        {   "proof_solve",          &RPCHandler::doProofSolve,          true,   optNone     },
        {   "proof_verify",         &RPCHandler::doProofVerify,         true,   optNone     },
        {   "random",               &RPCHandler::doRandom,              false,  optNone     },
        {   "ripple_path_find",     &RPCHandler::doRipplePathFind,      false,  optCurrent  },
        {   "sign",                 &RPCHandler::doSign,                false,  optNone     },
        {   "submit",               &RPCHandler::doSubmit,              false,  optCurrent  },
        {   "server_info",          &RPCHandler::doServerInfo,          false,  optNone     },
        {   "server_state",         &RPCHandler::doServerState,         false,  optNone     },
        {   "sms",                  &RPCHandler::doSMS,                 true,   optNone     },
        {   "stop",                 &RPCHandler::doStop,                true,   optNone     },
        {   "transaction_entry",    &RPCHandler::doTransactionEntry,    false,  optCurrent  },
        {   "tx",                   &RPCHandler::doTx,                  false,  optNetwork  },
        {   "tx_history",           &RPCHandler::doTxHistory,           false,  optNone     },
        {   "unl_add",              &RPCHandler::doUnlAdd,              true,   optNone     },
        {   "unl_delete",           &RPCHandler::doUnlDelete,           true,   optNone     },
        {   "unl_list",             &RPCHandler::doUnlList,             true,   optNone     },
        {   "unl_load",             &RPCHandler::doUnlLoad,             true,   optNone     },
        {   "unl_network",          &RPCHandler::doUnlNetwork,          true,   optNone     },
        {   "unl_reset",            &RPCHandler::doUnlReset,            true,   optNone     },
        {   "unl_score",            &RPCHandler::doUnlScore,            true,   optNone     },
        {   "validation_create",    &RPCHandler::doValidationCreate,    true,   optNone     },
        {   "validation_seed",      &RPCHandler::doValidationSeed,      true,   optNone     },
        {   "wallet_accounts",      &RPCHandler::doWalletAccounts,      false,  optCurrent  },
        {   "wallet_propose",       &RPCHandler::doWalletPropose,       true,   optNone     },
        {   "wallet_seed",          &RPCHandler::doWalletSeed,          true,   optNone     },

#if ENABLE_INSECURE
        // XXX Unnecessary commands which should be removed.
        {   "login",                &RPCHandler::doLogin,               true,   optNone     },
        {   "data_delete",          &RPCHandler::doDataDelete,          true,   optNone     },
        {   "data_fetch",           &RPCHandler::doDataFetch,           true,   optNone     },
        {   "data_store",           &RPCHandler::doDataStore,           true,   optNone     },
#endif

        // Evented methods
        {   "subscribe",            &RPCHandler::doSubscribe,           false,  optNone     },
        {   "unsubscribe",          &RPCHandler::doUnsubscribe,         false,  optNone     },
    };

    int     i = NUMBER (commandsA);

    while (i-- && strCommand != commandsA[i].pCommand)
        ;

    if (i < 0)
    {
        return rpcError (rpcUNKNOWN_COMMAND);
    }
    else if (commandsA[i].bAdminRequired && mRole != Config::ADMIN)
    {
        return rpcError (rpcNO_PERMISSION);
    }

    {
        Application::ScopedLockType lock (getApp().getMasterLock ());

        if ((commandsA[i].iOptions & optNetwork) && (mNetOps->getOperatingMode () < NetworkOPs::omSYNCING))
        {
            WriteLog (lsINFO, RPCHandler) << "Insufficient network mode for RPC: " << mNetOps->strOperatingMode ();

            return rpcError (rpcNO_NETWORK);
        }

        if (!getConfig ().RUN_STANDALONE && (commandsA[i].iOptions & optCurrent) && (getApp().getLedgerMaster().getValidatedLedgerAge() > 120))
        {
            return rpcError (rpcNO_CURRENT);
        }
        else if ((commandsA[i].iOptions & optClosed) && !mNetOps->getClosedLedger ())
        {
            return rpcError (rpcNO_CLOSED);
        }
        else
        {
            try
            {
                LoadEvent::autoptr ev   = getApp().getJobQueue().getLoadEventAP(
                    jtGENERIC, std::string("cmd:") + strCommand);
                Json::Value jvRaw       = (this->* (commandsA[i].dfpFunc)) (params, loadType, lock);

                // Regularize result.
                if (jvRaw.isObject ())
                {
                    // Got an object.
                    return jvRaw;
                }
                else
                {
                    // Probably got a string.
                    Json::Value jvResult (Json::objectValue);

                    jvResult["message"] = jvRaw;

                    return jvResult;
                }
            }
            catch (std::exception& e)
            {
                WriteLog (lsINFO, RPCHandler) << "Caught throw: " << e.what ();

                if (loadType == Resource::feeReferenceRPC)
                    loadType = Resource::feeExceptionRPC;

                return rpcError (rpcINTERNAL);
            }
        }
    }
}

RPCInternalHandler* RPCInternalHandler::sHeadHandler = nullptr;

RPCInternalHandler::RPCInternalHandler (const std::string& name, handler_t Handler) : mName (name), mHandler (Handler)
{
    mNextHandler = sHeadHandler;
    sHeadHandler = this;
}

Json::Value RPCInternalHandler::runHandler (const std::string& name, const Json::Value& params)
{
    RPCInternalHandler* h = sHeadHandler;

    while (h != nullptr)
    {
        if (name == h->mName)
        {
            WriteLog (lsWARNING, RPCHandler) << "Internal command " << name << ": " << params;
            Json::Value ret = h->mHandler (params);
            WriteLog (lsWARNING, RPCHandler) << "Internal command returns: " << ret;
            return ret;
        }

        h = h->mNextHandler;
    }

    return rpcError (rpcBAD_SYNTAX);
}

//------------------------------------------------------------------------------

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

            expect (! RPC::contains_error (result));
        }

        {
            Json::Value req;
            Json::Value result;
            Json::Reader ().parse (
                "{ \"fee_mult_max\" : 0, \"tx_json\" : { } } "
                , req);
            autofill_fee (req, ledger, result, true);

            expect (RPC::contains_error (result));
        }
    }

    void run ()
    {
        testAutoFillFees ();
    }
};

BEAST_DEFINE_TESTSUITE(JSONRPC,ripple_app,ripple);

} // ripple
