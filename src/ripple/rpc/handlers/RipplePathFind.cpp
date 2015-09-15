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
#include <ripple/rpc/RipplePathFind.h>
#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/main/Application.h>
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/app/paths/AccountCurrencies.h>
#include <ripple/app/paths/FindPaths.h>
#include <ripple/app/paths/PathRequest.h>
#include <ripple/app/paths/PathRequests.h>
#include <ripple/app/paths/RippleCalc.h>
#include <ripple/basics/Log.h>
#include <ripple/core/Config.h>
#include <ripple/core/LoadFeeTrack.h>
#include <ripple/json/json_reader.h>
#include <ripple/json/json_writer.h>
#include <ripple/ledger/PaymentSandbox.h>
#include <ripple/net/RPCErr.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/JsonFields.h>
#include <ripple/protocol/STParsedJSON.h>
#include <ripple/protocol/types.h>
#include <ripple/resource/Fees.h>
#include <ripple/rpc/Context.h>
#include <ripple/rpc/RipplePathFind.h>
#include <ripple/rpc/impl/LegacyPathFind.h>
#include <ripple/rpc/impl/LookupLedger.h>
#include <ripple/rpc/impl/Tuning.h>
#include <ripple/server/Role.h>

namespace ripple {

static
Json::Value
buildSrcCurrencies(AccountID const& account,
    RippleLineCache::pointer const& cache)
{
    auto currencies = accountSourceCurrencies(account, cache, true);
    auto jvSrcCurrencies = Json::Value(Json::arrayValue);

    for (auto const& uCurrency : currencies)
    {
        Json::Value jvCurrency(Json::objectValue);
        jvCurrency[jss::currency] = to_string(uCurrency);
        jvSrcCurrencies.append(jvCurrency);
    }

    return jvSrcCurrencies;
}

// This interface is deprecated.
Json::Value doRipplePathFind (RPC::Context& context)
{
    RPC::LegacyPathFind lpf (context.role == Role::ADMIN, context.app);
    if (!lpf.isOk ())
        return rpcError (rpcTOO_BUSY);

    context.loadType = Resource::feeHighBurdenRPC;

    AccountID raSrc;
    AccountID raDst;
    STAmount saDstAmount;
    std::shared_ptr <ReadView const> lpLedger;

    Json::Value jvResult;

    if (getConfig().RUN_STANDALONE ||
        context.params.isMember(jss::ledger) ||
        context.params.isMember(jss::ledger_index) ||
        context.params.isMember(jss::ledger_hash))
    {
        // The caller specified a ledger
        jvResult = RPC::lookupLedger (lpLedger, context);
        if (!lpLedger)
            return jvResult;
    }
    else
    {
        if (context.app.getLedgerMaster().getValidatedLedgerAge() >
            RPC::Tuning::maxValidatedLedgerAge)
        {
            return rpcError (rpcNO_NETWORK);
        }

        context.loadType = Resource::feeHighBurdenRPC;
        lpLedger = context.ledgerMaster.getClosedLedger();

        PathRequest::pointer request;
        context.suspend.suspend(
            [&request, &context, &jvResult, &lpLedger]
            (RPC::Callback const& callback)
        {
            jvResult = context.app.getPathRequests().makeLegacyPathRequest (
                request, callback, lpLedger, context.params);
            callback();
        });

        if (request)
            jvResult = request->doStatus (context.params);

        return jvResult;
    }

    if (!context.params.isMember (jss::source_account))
    {
        jvResult = rpcError (rpcSRC_ACT_MISSING);
    }
    else if (! deprecatedParseBase58(raSrc,
        context.params[jss::source_account]))
    {
        jvResult = rpcError (rpcSRC_ACT_MALFORMED);
    }
    else if (!context.params.isMember (jss::destination_account))
    {
        jvResult = rpcError (rpcDST_ACT_MISSING);
    }
    else if (! deprecatedParseBase58 (raDst,
        context.params[jss::destination_account]))
    {
        jvResult = rpcError (rpcDST_ACT_MALFORMED);
    }
    else if (
        // Parse saDstAmount.
        !context.params.isMember (jss::destination_amount)
        || ! amountFromJsonNoThrow(saDstAmount, context.params[jss::destination_amount])
        || (saDstAmount <= zero && saDstAmount !=
                STAmount(saDstAmount.issue(), 1u, 0, true))
        || (!isXRP(saDstAmount.getCurrency ())
            && (!saDstAmount.getIssuer () ||
                noAccount() == saDstAmount.getIssuer ())))
    {
        WriteLog (lsINFO, RPCHandler) << "Bad destination_amount.";
        jvResult    = rpcError (rpcINVALID_PARAMS);
    }
    else if (
        // Checks on source_currencies.
        context.params.isMember (jss::source_currencies)
        && (!context.params[jss::source_currencies].isArray ()
            || !context.params[jss::source_currencies].size ())
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
            cache = std::make_shared<RippleLineCache>(lpLedger);
        }
        else
        {
            // The closed ledger is recent and any nodes made resident
            // have the best chance to persist
            lpLedger = context.ledgerMaster.getClosedLedger();
            cache = context.app.getPathRequests().getLineCache(lpLedger, false);
        }

        Json::Value     jvSrcCurrencies;

        if (context.params.isMember (jss::source_currencies))
        {
            jvSrcCurrencies = context.params[jss::source_currencies];
        }
        else
        {
            jvSrcCurrencies = buildSrcCurrencies(raSrc, cache);
        }

        // Fill in currencies destination will accept
        Json::Value jvDestCur (Json::arrayValue);

        // TODO(tom): this could be optimized the same way that
        // PathRequest::doUpdate() is - if we don't obsolete this code first.
        auto usDestCurrID = accountDestCurrencies (raDst, cache, true);
        for (auto const& uCurrency: usDestCurrID)
                jvDestCur.append (to_string (uCurrency));

        jvResult[jss::destination_currencies] = jvDestCur;
        jvResult[jss::destination_account] = context.app.accountIDCache().toBase58(raDst);

        int level = getConfig().PATH_SEARCH_OLD;
        if ((context.app.config().PATH_SEARCH_MAX > level)
            && !context.app.getFeeTrack().isLoadedLocal())
        {
            ++level;
        }

        if (context.params.isMember(jss::search_depth)
            && context.params[jss::search_depth].isIntegral())
        {
            int rLev = context.params[jss::search_depth].asInt ();
            if ((rLev < level) || (context.role == Role::ADMIN))
                level = rLev;
        }

        auto const convert_all =
            saDstAmount == STAmount(saDstAmount.issue(), 1u, 0, true);

        boost::optional<STAmount> saSendMax;
        if (context.params.isMember(jss::send_max))
        {
            // Send_max requires destination amount to be -1.
            if (! convert_all)
                return rpcError(rpcDST_AMT_MALFORMED);

            saSendMax.emplace();
            if (!amountFromJsonNoThrow(
                *saSendMax, context.params[jss::send_max]) ||
                (saSendMax->getCurrency().isZero() &&
                    saSendMax->getIssuer().isNonZero()) ||
                (saSendMax->getCurrency() == badCurrency()) ||
                (*saSendMax <= zero &&
                    *saSendMax != STAmount(saSendMax->issue(), 1u, 0, true)))
            {
                return rpcError(rpcSENDMAX_MALFORMED);
            }
        }

        auto contextPaths = context.params.isMember(jss::paths) ?
            boost::optional<Json::Value>(context.params[jss::paths]) :
                boost::optional<Json::Value>(boost::none);
        auto pathFindResult = ripplePathFind(cache, raSrc, raDst, saDstAmount,
            jvSrcCurrencies, contextPaths, level, saSendMax, convert_all,
                context.app);
        if (!pathFindResult.first)
            return pathFindResult.second;

        // Each alternative differs by source currency.
        jvResult[jss::alternatives] = pathFindResult.second;
    }

    WriteLog (lsDEBUG, RPCHandler)
            << "ripple_path_find< " << jvResult;

    return jvResult;
}

std::pair<bool, Json::Value>
ripplePathFind (RippleLineCache::pointer const& cache,
  AccountID const& raSrc, AccountID const& raDst,
    STAmount const& saDstAmount, Json::Value const& jvSrcCurrencies,
        boost::optional<Json::Value> const& contextPaths,
            int const& level, boost::optional<STAmount> saSendMax,
                bool convert_all, Application& app)
{
    auto const sa = STAmount(saDstAmount.issue(),
        STAmount::cMaxValue, STAmount::cMaxOffset);
    FindPaths fp(
        cache,
        raSrc,
        raDst,
        convert_all ? sa : saDstAmount,
        saSendMax,
        level,
        4); // max paths

    Json::Value jvArray(Json::arrayValue);

    for (unsigned int i = 0; i != jvSrcCurrencies.size(); ++i)
    {
        Json::Value jvSource = jvSrcCurrencies[i];

        Currency uSrcCurrencyID;
        AccountID uSrcIssuerID;

        if (!jvSource.isObject())
            return std::make_pair(false, rpcError(rpcINVALID_PARAMS));

        // Parse mandatory currency.
        if (!jvSource.isMember(jss::currency)
            || !to_currency(
            uSrcCurrencyID, jvSource[jss::currency].asString()))
        {
            WriteLog(lsINFO, RPCHandler) << "Bad currency.";
            return std::make_pair(false, rpcError(rpcSRC_CUR_MALFORMED));
        }

        if (uSrcCurrencyID.isNonZero())
            uSrcIssuerID = raSrc;

        // Parse optional issuer.
        if (jvSource.isMember(jss::issuer) &&
            ((!jvSource[jss::issuer].isString() ||
            !to_issuer(uSrcIssuerID, jvSource[jss::issuer].asString())) ||
            (uSrcIssuerID.isZero() != uSrcCurrencyID.isZero()) ||
            (noAccount() == uSrcIssuerID)))
        {
            WriteLog(lsINFO, RPCHandler) << "Bad issuer.";
            return std::make_pair(false, rpcError(rpcSRC_ISR_MALFORMED));
        }

        auto issue = Issue(uSrcCurrencyID, uSrcIssuerID);
        if (saSendMax)
        {
            // If the currencies don't match, ignore the source currency.
            if (uSrcCurrencyID != saSendMax->getCurrency())
                continue;

            // If neither is the source and they are not equal, then the
            // source issuer is illegal.
            if (uSrcIssuerID != raSrc && saSendMax->getIssuer() != raSrc &&
                uSrcIssuerID != saSendMax->getIssuer())
            {
                return std::make_pair(false, rpcError(rpcSRC_ISR_MALFORMED));
            }

            // If both are the source, use the source.
            // Otherwise, use the one that's not the source.
            if (uSrcIssuerID == raSrc)
            {
                issue.account = saSendMax->getIssuer() != raSrc ?
                    saSendMax->getIssuer() : raSrc;
            }
        }

        STPathSet spsComputed;
        if (contextPaths)
        {
            Json::Value pathSet = Json::objectValue;
            pathSet[jss::Paths] = contextPaths.get();
            STParsedJSONObject paths("pathSet", pathSet);
            if (! paths.object)
                return std::make_pair(false, paths.error);
            else
            {
                spsComputed = paths.object->getFieldPathSet(sfPaths);
                WriteLog(lsTRACE, RPCHandler) << "ripple_path_find: Paths: " <<
                    spsComputed.getJson(0);
            }
        }

        STPath fullLiquidityPath;
        auto result = fp.findPathsForIssue(
            issue,
            spsComputed,
            fullLiquidityPath,
            app);
        if (! result)
        {
            WriteLog(lsWARNING, RPCHandler)
                << "ripple_path_find: No paths found.";
        }
        else
        {
            auto& issuer =
                isXRP(uSrcIssuerID) ?
                isXRP(uSrcCurrencyID) ? // Default to source account.
                xrpAccount() :
                (raSrc)
                : uSrcIssuerID;            // Use specifed issuer.
            STAmount saMaxAmount = saSendMax.value_or(
                STAmount({uSrcCurrencyID, issuer}, 1u, 0, true));

            boost::optional<PaymentSandbox> sandbox;
            sandbox.emplace(&*cache->getLedger(), tapNONE);
            assert(sandbox->open());

            path::RippleCalc::Input rcInput;
            if (convert_all)
                rcInput.partialPaymentAllowed = true;

            auto rc = path::RippleCalc::rippleCalculate(
                *sandbox,
                saMaxAmount,                    // --> Amount to send is unlimited
                                                //     to get an estimate.
                convert_all ? sa : saDstAmount, // --> Amount to deliver.
                raDst,                          // --> Account to deliver to.
                raSrc,                          // --> Account sending from.
                *result,                        // --> Path set.
                &rcInput);

            WriteLog(lsWARNING, RPCHandler)
                << "ripple_path_find:"
                << " saMaxAmount=" << saMaxAmount
                << " saDstAmount=" << saDstAmount
                << " saMaxAmountAct=" << rc.actualAmountIn
                << " saDstAmountAct=" << rc.actualAmountOut;

            if (! convert_all &&
                ! fullLiquidityPath.empty() &&
                (rc.result() == terNO_LINE || rc.result() == tecPATH_PARTIAL))
            {
                WriteLog(lsDEBUG, PathRequest)
                    << "Trying with an extra path element";
                result->push_back(fullLiquidityPath);
                sandbox.emplace(&*cache->getLedger(), tapNONE);
                assert(sandbox->open());
                rc = path::RippleCalc::rippleCalculate(
                    *sandbox,
                    saMaxAmount,            // --> Amount to send is unlimited
                                            //     to get an estimate.
                    saDstAmount,            // --> Amount to deliver.
                    raDst,                  // --> Account to deliver to.
                    raSrc,                  // --> Account sending from.
                    *result);               // --> Path set.
                WriteLog(lsDEBUG, PathRequest)
                    << "Extra path element gives "
                    << transHuman(rc.result());
            }

            if (rc.result() == tesSUCCESS)
            {
                Json::Value jvEntry(Json::objectValue);

                STPathSet   spsCanonical;

                // Reuse the expanded as it would need to be calcuated
                // anyway to produce the canonical.  (At least unless we
                // make a direct canonical.)

                jvEntry[jss::source_amount] = rc.actualAmountIn.getJson(0);
                jvEntry[jss::paths_canonical] = Json::arrayValue;
                jvEntry[jss::paths_computed] = result->getJson(0);

                if (convert_all)
                    jvEntry[jss::destination_amount] = rc.actualAmountOut.getJson(0);

                jvArray.append(jvEntry);
            }
            else
            {
                std::string strToken;
                std::string strHuman;

                transResultInfo(rc.result(), strToken, strHuman);

                WriteLog(lsDEBUG, RPCHandler)
                    << "ripple_path_find: "
                    << strToken << " "
                    << strHuman << " "
                    << spsComputed.getJson(0);
            }
        }
    }

    return std::make_pair(true, std::move(jvArray));
}

} // ripple
