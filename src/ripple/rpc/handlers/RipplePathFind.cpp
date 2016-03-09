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

static unsigned int const max_paths = 4;

// This interface is deprecated.
Json::Value doRipplePathFind (RPC::Context& context)
{
    if (context.app.config().PATH_SEARCH_MAX == 0)
        return rpcError (rpcNOT_SUPPORTED);

    context.loadType = Resource::feeHighBurdenRPC;

    std::shared_ptr <ReadView const> lpLedger;
    Json::Value jvResult;

    if (! context.app.config().RUN_STANDALONE &&
        ! context.params.isMember(jss::ledger) &&
        ! context.params.isMember(jss::ledger_index) &&
        ! context.params.isMember(jss::ledger_hash))
    {
        if (context.app.getLedgerMaster().getValidatedLedgerAge() >
            RPC::Tuning::maxValidatedLedgerAge)
        {
            return rpcError (rpcNO_NETWORK);
        }

        PathRequest::pointer request;
        lpLedger = context.ledgerMaster.getClosedLedger();

        jvResult = context.app.getPathRequests().makeLegacyPathRequest (
            request, std::bind(&JobCoro::post, context.jobCoro),
                context.consumer, lpLedger, context.params);
        if (request)
        {
            context.jobCoro->yield();
            jvResult = request->doStatus (context.params);
        }

        return jvResult;
    }

    // The caller specified a ledger
    jvResult = RPC::lookupLedger (lpLedger, context);
    if (! lpLedger)
        return jvResult;

    RPC::LegacyPathFind lpf (isUnlimited (context.role), context.app);
    if (! lpf.isOk ())
        return rpcError (rpcTOO_BUSY);

    AccountID raSrc;
    AccountID raDst;
    STAmount saDstAmount;
    if (! context.params.isMember (jss::source_account))
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
        JLOG (context.j.info()) << "Bad destination_amount.";
        jvResult    = rpcError (rpcINVALID_PARAMS);
    }
    else if (
        // Checks on source_currencies.
        context.params.isMember(jss::source_currencies) &&
            (! context.params[jss::source_currencies].isArray() ||
                context.params[jss::source_currencies].size() == 0 ||
                    context.params[jss::source_currencies].size() >
                        RPC::Tuning::max_src_cur))
    {
        JLOG (context.j.info()) << "Bad source_currencies.";
        jvResult = rpcError(rpcINVALID_PARAMS);
    }
    else
    {
        std::shared_ptr<RippleLineCache> cache;

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

        Json::Value jvSrcCurrencies;
        if (context.params.isMember (jss::source_currencies))
        {
            jvSrcCurrencies = context.params[jss::source_currencies];
        }
        else
        {
            auto currencies = accountSourceCurrencies(raSrc, cache, true);
            if (currencies.size() > RPC::Tuning::max_auto_src_cur)
                return rpcError(rpcINTERNAL);
            auto jvSrcCurrencies = Json::Value(Json::arrayValue);
            for (auto const& c : currencies)
            {
                Json::Value jvCurrency(Json::objectValue);
                jvCurrency[jss::currency] = to_string(c);
                jvSrcCurrencies.append(jvCurrency);
            }
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

        int level = context.app.config().PATH_SEARCH_OLD;
        if ((context.app.config().PATH_SEARCH_MAX > level)
            && !context.app.getFeeTrack().isLoadedLocal())
        {
            ++level;
        }

        if (context.params.isMember(jss::search_depth)
            && context.params[jss::search_depth].isIntegral())
        {
            int rLev = context.params[jss::search_depth].asInt ();
            if ((rLev < level) || (isUnlimited (context.role)))
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
        auto pathFindResult = ripplePathFind(
            cache, raSrc, raDst, (convert_all ? STAmount(saDstAmount.issue(),
                STAmount::cMaxValue, STAmount::cMaxOffset) : saDstAmount),
                    jvSrcCurrencies, contextPaths, level, saSendMax,
                        convert_all, context.app);
        if (!pathFindResult.first)
            return pathFindResult.second;

        // Each alternative differs by source currency.
        jvResult[jss::alternatives] = pathFindResult.second;
    }

    JLOG (context.j.debug())
            << "ripple_path_find< " << jvResult;

    return jvResult;
}

std::unique_ptr<Pathfinder> const&
getPathFinder(std::shared_ptr<RippleLineCache> const& cache, AccountID const& raSrc,
    AccountID const& raDst, boost::optional<STAmount> saSendMax,
        hash_map<Currency, std::unique_ptr<Pathfinder>>& currency_map,
            Currency const& currency, STAmount const& dst_amount,
                int const level, Application& app)
{
    auto i = currency_map.find(currency);
    if (i != currency_map.end())
        return i->second;
    auto pathfinder = std::make_unique<Pathfinder>(cache, raSrc, raDst,
        currency, boost::none, dst_amount, saSendMax, app);
    if (pathfinder->findPaths(level))
        pathfinder->computePathRanks(max_paths);
    else
        pathfinder.reset();  // It's a bad request - clear it.
    return currency_map[currency] = std::move(pathfinder);
}

std::pair<bool, Json::Value>
ripplePathFind (std::shared_ptr<RippleLineCache> const& cache,
  AccountID const& raSrc, AccountID const& raDst,
    STAmount const& saDstAmount, Json::Value const& jvSrcCurrencies,
        boost::optional<Json::Value> const& contextPaths,
            int const level, boost::optional<STAmount> saSendMax,
                bool convert_all, Application& app)
{
    Json::Value jvArray(Json::arrayValue);
    hash_map<Currency, std::unique_ptr<Pathfinder>> currency_map;

    auto j = app.journal ("RPCHandler");

    for (auto const& c : jvSrcCurrencies)
    {
        if (! c.isObject())
            return std::make_pair(false, rpcError(rpcINVALID_PARAMS));

        // Mandatory currency
        Currency srcCurrencyID;
        if (! c.isMember(jss::currency) ||
            ! to_currency(srcCurrencyID, c[jss::currency].asString()))
        {
            JLOG (j.info()) << "Bad currency.";
            return std::make_pair(false, rpcError(rpcSRC_CUR_MALFORMED));
        }

        // Optional issuer
        AccountID srcIssuerID;
        if (c.isMember (jss::issuer) &&
            (! c[jss::issuer].isString() ||
                ! to_issuer(srcIssuerID, c[jss::issuer].asString()) ||
                    srcIssuerID.isZero() != srcCurrencyID.isZero() ||
                        noAccount() == srcIssuerID))
        {
            JLOG (j.info()) << "Bad issuer.";
            return std::make_pair(false, rpcError(rpcSRC_ISR_MALFORMED));
        }

        if (srcIssuerID.isZero())
            srcIssuerID = raSrc;

        auto issue = Issue(srcCurrencyID, srcIssuerID);
        if (saSendMax)
        {
            // If the currencies don't match, ignore the source currency.
            if (srcCurrencyID != saSendMax->getCurrency())
                continue;

            // If neither is the source and they are not equal, then the
            // source issuer is illegal.
            if (srcIssuerID != raSrc && saSendMax->getIssuer() != raSrc &&
                srcIssuerID != saSendMax->getIssuer())
            {
                return std::make_pair(false, rpcError(rpcSRC_ISR_MALFORMED));
            }

            // If both are the source, use the source.
            // Otherwise, use the one that's not the source.
            if (srcIssuerID == raSrc)
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

            spsComputed = paths.object->getFieldPathSet(sfPaths);
            JLOG (j.trace()) << "ripple_path_find: Paths: " <<
                spsComputed.getJson(0);
        }

        auto& pathfinder = getPathFinder(cache, raSrc, raDst, saSendMax,
            currency_map, issue.currency, saDstAmount, level, app);
        if (! pathfinder)
        {
            JLOG (j.warn()) << "ripple_path_find: No paths found.";
            continue;
        }

        STPath fullLiquidityPath;
        auto ps = pathfinder->getBestPaths(max_paths,
            fullLiquidityPath, spsComputed, issue.account);

        STAmount saMaxAmount;
        if (saSendMax)
        {
            saMaxAmount = *saSendMax;
        }
        else
        {
            AccountID issuerID;
            if (isXRP(srcIssuerID))
            {
                // Default to source account.
                if(isXRP(srcCurrencyID))
                    issuerID = xrpAccount();
                else
                    issuerID = raSrc;
            }
            else
            {
                // Use specifed issuer.
                issuerID = srcIssuerID;
            }

            saMaxAmount = STAmount(
                {srcCurrencyID, issuerID}, 1u, 0, true);
        }

        boost::optional<PaymentSandbox> sandbox;
        sandbox.emplace(&*cache->getLedger(), tapNONE);
        assert(sandbox->open());

        path::RippleCalc::Input rcInput;
        if (convert_all)
            rcInput.partialPaymentAllowed = true;

        auto rc = path::RippleCalc::rippleCalculate(
            *sandbox,
            saMaxAmount,    // --> Amount to send is unlimited
                            //     to get an estimate.
            saDstAmount,    // --> Amount to deliver.
            raDst,          // --> Account to deliver to.
            raSrc,          // --> Account sending from.
            ps,             // --> Path set.
            app.logs(),
            app.config(),
            &rcInput);

        JLOG(j.info())
            << "ripple_path_find:"
            << " saMaxAmount=" << saMaxAmount
            << " saDstAmount=" << saDstAmount
            << " saMaxAmountAct=" << rc.actualAmountIn
            << " saDstAmountAct=" << rc.actualAmountOut;

        if (! convert_all &&
            ! fullLiquidityPath.empty() &&
            (rc.result() == terNO_LINE || rc.result() == tecPATH_PARTIAL))
        {
            auto jpr = app.journal("PathRequest");
            JLOG(jpr.debug())
                << "Trying with an extra path element";
            ps.push_back(fullLiquidityPath);
            sandbox.emplace(&*cache->getLedger(), tapNONE);
            assert(sandbox->open());
            rc = path::RippleCalc::rippleCalculate(
                *sandbox,
                saMaxAmount,    // --> Amount to send is unlimited
                                //     to get an estimate.
                saDstAmount,    // --> Amount to deliver.
                raDst,          // --> Account to deliver to.
                raSrc,          // --> Account sending from.
                ps,             // --> Path set.
                app.logs(),
                app.config());
            JLOG(jpr.debug())
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
            jvEntry[jss::paths_computed] = ps.getJson(0);

            if (convert_all)
                jvEntry[jss::destination_amount] = rc.actualAmountOut.getJson(0);

            jvArray.append(jvEntry);
        }
        else
        {
            std::string strToken;
            std::string strHuman;
            transResultInfo(rc.result(), strToken, strHuman);
            JLOG (j.debug())
                << "ripple_path_find: "
                << strToken << " "
                << strHuman << " "
                << spsComputed.getJson(0);
        }
    }

    return std::make_pair(true, std::move(jvArray));
}

} // ripple
