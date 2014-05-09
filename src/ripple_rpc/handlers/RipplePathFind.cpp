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

#include "../impl/LegacyPathFind.h"

namespace ripple {

// This interface is deprecated.
Json::Value RPCHandler::doRipplePathFind (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& masterLockHolder)
{
    masterLockHolder.unlock ();

    RPC::LegacyPathFind lpf (mRole == Config::ADMIN);
    if (!lpf.isOk ())
        return rpcError (rpcTOO_BUSY);

    loadType = Resource::feeHighBurdenRPC;

    RippleAddress   raSrc;
    RippleAddress   raDst;
    STAmount        saDstAmount;
    Ledger::pointer lpLedger;

    Json::Value     jvResult;

    if (getConfig().RUN_STANDALONE || params.isMember("ledger") || params.isMember("ledger_index") || params.isMember("ledger_hash"))
    { // The caller specified a ledger
        jvResult = RPC::lookupLedger (params, lpLedger, *mNetOps);
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
        || saDstAmount <= zero
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
        {
            // The caller specified a ledger
            lpLedger = boost::make_shared<Ledger> (boost::ref (*lpLedger), false);
            cache = boost::make_shared<RippleLineCache>(lpLedger);
        }
        else
        {
            // The closed ledger is recent and any nodes made resident
            // have the best chance to persist
            lpLedger = mNetOps->getClosedLedger();
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

} // ripple
