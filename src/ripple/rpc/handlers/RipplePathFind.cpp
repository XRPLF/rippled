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
#include <ripple/app/paths/AccountCurrencies.h>
#include <ripple/app/paths/FindPaths.h>
#include <ripple/app/paths/RippleCalc.h>
#include <ripple/core/LoadFeeTrack.h>
#include <ripple/protocol/STParsedJSON.h>
#include <ripple/rpc/impl/LegacyPathFind.h>
#include <ripple/server/Role.h>

namespace ripple {

// This interface is deprecated.
Json::Value doRipplePathFind (RPC::Context& context)
{
    RPC::LegacyPathFind lpf (context.role == Role::ADMIN);
    if (!lpf.isOk ())
        return rpcError (rpcTOO_BUSY);

    context.loadType = Resource::feeHighBurdenRPC;

    RippleAddress raSrc;
    RippleAddress raDst;
    STAmount saDstAmount;
    Ledger::pointer lpLedger;

    Json::Value jvResult;

    if (getConfig().RUN_STANDALONE ||
        context.params.isMember(jss::ledger) ||
        context.params.isMember(jss::ledger_index) ||
        context.params.isMember(jss::ledger_hash))
    {
        // The caller specified a ledger
        jvResult = RPC::lookupLedger (
            context.params, lpLedger, context.netOps);
        if (!lpLedger)
            return jvResult;
    }

    if (!context.params.isMember ("source_account"))
    {
        jvResult = rpcError (rpcSRC_ACT_MISSING);
    }
    else if (!context.params["source_account"].isString ()
             || !raSrc.setAccountID (
                 context.params["source_account"].asString ()))
    {
        jvResult = rpcError (rpcSRC_ACT_MALFORMED);
    }
    else if (!context.params.isMember ("destination_account"))
    {
        jvResult = rpcError (rpcDST_ACT_MISSING);
    }
    else if (!context.params["destination_account"].isString ()
             || !raDst.setAccountID (
                 context.params["destination_account"].asString ()))
    {
        jvResult = rpcError (rpcDST_ACT_MALFORMED);
    }
    else if (
        // Parse saDstAmount.
        !context.params.isMember ("destination_amount")
        || ! amountFromJsonNoThrow(saDstAmount, context.params["destination_amount"])
        || saDstAmount <= zero
        || (!isXRP(saDstAmount.getCurrency ())
            && (!saDstAmount.getIssuer () ||
                noAccount() == saDstAmount.getIssuer ())))
    {
        WriteLog (lsINFO, RPCHandler) << "Bad destination_amount.";
        jvResult    = rpcError (rpcINVALID_PARAMS);
    }
    else if (
        // Checks on source_currencies.
        context.params.isMember ("source_currencies")
        && (!context.params["source_currencies"].isArray ()
            || !context.params["source_currencies"].size ())
        // Don't allow empty currencies.
    )
    {
        WriteLog (lsINFO, RPCHandler) << "Bad source_currencies.";
        jvResult    = rpcError (rpcINVALID_PARAMS);
    }
    else
    {
        context.loadType = Resource::feeHighBurdenRPC;
        RippleLineCache::pointer cache;

        if (lpLedger)
        {
            // The caller specified a ledger
            lpLedger = std::make_shared<Ledger> (std::ref (*lpLedger), false);
            cache = std::make_shared<RippleLineCache>(lpLedger);
        }
        else
        {
            // The closed ledger is recent and any nodes made resident
            // have the best chance to persist
            lpLedger = context.netOps.getClosedLedger();
            cache = getApp().getPathRequests().getLineCache(lpLedger, false);
        }

        Json::Value     jvSrcCurrencies;

        if (context.params.isMember ("source_currencies"))
        {
            jvSrcCurrencies = context.params["source_currencies"];
        }
        else
        {
            auto currencies = accountSourceCurrencies (raSrc, cache, true);
            jvSrcCurrencies = Json::Value (Json::arrayValue);

            for (auto const& uCurrency: currencies)
            {
                Json::Value jvCurrency (Json::objectValue);
                jvCurrency["currency"] = to_string(uCurrency);
                jvSrcCurrencies.append (jvCurrency);
            }
        }

        // Fill in currencies destination will accept
        Json::Value jvDestCur (Json::arrayValue);

        // TODO(tom): this could be optimized the same way that
        // PathRequest::doUpdate() is - if we don't obsolete this code first.
        auto usDestCurrID = accountDestCurrencies (raDst, cache, true);
        for (auto const& uCurrency: usDestCurrID)
                jvDestCur.append (to_string (uCurrency));

        jvResult["destination_currencies"] = jvDestCur;
        jvResult["destination_account"] = raDst.humanAccountID ();

        Json::Value jvArray (Json::arrayValue);

        int level = getConfig().PATH_SEARCH_OLD;
        if ((getConfig().PATH_SEARCH_MAX > level)
            && !getApp().getFeeTrack().isLoadedLocal())
        {
            ++level;
        }

        if (context.params.isMember("search_depth")
            && context.params["search_depth"].isIntegral())
        {
            int rLev = context.params["search_depth"].asInt ();
            if ((rLev < level) || (context.role == Role::ADMIN))
                level = rLev;
        }

        FindPaths fp (
            cache,
            raSrc.getAccountID(),
            raDst.getAccountID(),
            saDstAmount,
            level,
            4); // max paths

        for (unsigned int i = 0; i != jvSrcCurrencies.size (); ++i)
        {
            Json::Value jvSource        = jvSrcCurrencies[i];

            Currency uSrcCurrencyID;
            Account uSrcIssuerID;

            if (!jvSource.isObject ())
                return rpcError (rpcINVALID_PARAMS);

            // Parse mandatory currency.
            if (!jvSource.isMember ("currency")
                || !to_currency (
                    uSrcCurrencyID, jvSource["currency"].asString ()))
            {
                WriteLog (lsINFO, RPCHandler) << "Bad currency.";

                return rpcError (rpcSRC_CUR_MALFORMED);
            }

            if (uSrcCurrencyID.isNonZero ())
                uSrcIssuerID = raSrc.getAccountID ();

            // Parse optional issuer.
            if (jvSource.isMember ("issuer") &&
                ((!jvSource["issuer"].isString () ||
                  !to_issuer (uSrcIssuerID, jvSource["issuer"].asString ())) ||
                 (uSrcIssuerID.isZero () != uSrcCurrencyID.isZero ()) ||
                 (noAccount() == uSrcIssuerID)))
            {
                WriteLog (lsINFO, RPCHandler) << "Bad issuer.";
                return rpcError (rpcSRC_ISR_MALFORMED);
            }

            STPathSet spsComputed;
            if (context.params.isMember("paths"))
            {
                Json::Value pathSet = Json::objectValue;
                pathSet["Paths"] = context.params["paths"];
                STParsedJSONObject paths ("pathSet", pathSet);
                if (paths.object.get() == nullptr)
                    return paths.error;
                else
                {
                    spsComputed = paths.object.get()->getFieldPathSet (sfPaths);
                    WriteLog (lsTRACE, RPCHandler) << "ripple_path_find: Paths: " << spsComputed.getJson (0);
                }
            }

            STPath fullLiquidityPath;
            auto valid = fp.findPathsForIssue (
                {uSrcCurrencyID, uSrcIssuerID},
                spsComputed,
                fullLiquidityPath);
            if (!valid)
            {
                WriteLog (lsWARNING, RPCHandler)
                    << "ripple_path_find: No paths found.";
            }
            else
            {
                auto& issuer =
                    isXRP (uSrcIssuerID) ?
                        isXRP (uSrcCurrencyID) ? // Default to source account.
                            xrpAccount() :
                            Account (raSrc.getAccountID ())
                        : uSrcIssuerID;            // Use specifed issuer.

                STAmount saMaxAmount ({uSrcCurrencyID, issuer}, 1);
                saMaxAmount.negate ();

                LedgerEntrySet lesSandbox (lpLedger, tapNONE);

                auto rc = path::RippleCalc::rippleCalculate (
                    lesSandbox,
                    saMaxAmount,            // --> Amount to send is unlimited
                                            //     to get an estimate.
                    saDstAmount,            // --> Amount to deliver.
                    raDst.getAccountID (),  // --> Account to deliver to.
                    raSrc.getAccountID (),  // --> Account sending from.
                    spsComputed);           // --> Path set.

                WriteLog (lsWARNING, RPCHandler)
                    << "ripple_path_find:"
                    << " saMaxAmount=" << saMaxAmount
                    << " saDstAmount=" << saDstAmount
                    << " saMaxAmountAct=" << rc.actualAmountIn
                    << " saDstAmountAct=" << rc.actualAmountOut;

                if (fullLiquidityPath.size() > 0 &&
                    (rc.result() == terNO_LINE || rc.result() == tecPATH_PARTIAL))
                {
                    WriteLog (lsDEBUG, PathRequest)
                        << "Trying with an extra path element";

                    spsComputed.push_back (fullLiquidityPath);
                    lesSandbox.clear ();
                    rc = path::RippleCalc::rippleCalculate (
                        lesSandbox,
                        saMaxAmount,            // --> Amount to send is unlimited
                        //     to get an estimate.
                        saDstAmount,            // --> Amount to deliver.
                        raDst.getAccountID (),  // --> Account to deliver to.
                        raSrc.getAccountID (),  // --> Account sending from.
                        spsComputed);         // --> Path set.
                    WriteLog (lsDEBUG, PathRequest)
                        << "Extra path element gives "
                        << transHuman (rc.result ());
                }

                if (rc.result () == tesSUCCESS)
                {
                    Json::Value jvEntry (Json::objectValue);

                    STPathSet   spsCanonical;

                    // Reuse the expanded as it would need to be calcuated
                    // anyway to produce the canonical.  (At least unless we
                    // make a direct canonical.)

                    jvEntry["source_amount"] = rc.actualAmountIn.getJson (0);
                    jvEntry["paths_canonical"]  = Json::arrayValue;
                    jvEntry["paths_computed"]   = spsComputed.getJson (0);

                    jvArray.append (jvEntry);
                }
                else
                {
                    std::string strToken;
                    std::string strHuman;

                    transResultInfo (rc.result (), strToken, strHuman);

                    WriteLog (lsDEBUG, RPCHandler)
                        << "ripple_path_find: "
                        << strToken << " "
                        << strHuman << " "
                        << spsComputed.getJson (0);
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
