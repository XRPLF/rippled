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

#include <xrpld/app/main/Application.h>
#include <xrpld/app/misc/LoadFeeTrack.h>
#include <xrpld/app/misc/NetworkOPs.h>
#include <xrpld/app/paths/AccountAssets.h>
#include <xrpld/app/paths/PathRequest.h>
#include <xrpld/app/paths/PathRequests.h>
#include <xrpld/app/paths/RippleCalc.h>
#include <xrpld/app/paths/detail/PathfinderUtils.h>
#include <xrpld/core/Config.h>
#include <xrpld/rpc/detail/Tuning.h>

#include <xrpl/basics/Log.h>
#include <xrpl/beast/core/LexicalCast.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/UintTypes.h>

#include <optional>
#include <tuple>

namespace ripple {

PathRequest::PathRequest(
    Application& app,
    std::shared_ptr<InfoSub> const& subscriber,
    int id,
    PathRequests& owner,
    beast::Journal journal)
    : app_(app)
    , m_journal(journal)
    , mOwner(owner)
    , wpSubscriber(subscriber)
    , consumer_(subscriber->getConsumer())
    , jvStatus(Json::objectValue)
    , mLastIndex(0)
    , mInProgress(false)
    , iLevel(0)
    , bLastSuccess(false)
    , iIdentifier(id)
    , created_(std::chrono::steady_clock::now())
{
    JLOG(m_journal.debug()) << iIdentifier << " created";
}

PathRequest::PathRequest(
    Application& app,
    std::function<void(void)> const& completion,
    Resource::Consumer& consumer,
    int id,
    PathRequests& owner,
    beast::Journal journal)
    : app_(app)
    , m_journal(journal)
    , mOwner(owner)
    , fCompletion(completion)
    , consumer_(consumer)
    , jvStatus(Json::objectValue)
    , mLastIndex(0)
    , mInProgress(false)
    , iLevel(0)
    , bLastSuccess(false)
    , iIdentifier(id)
    , created_(std::chrono::steady_clock::now())
{
    JLOG(m_journal.debug()) << iIdentifier << " created";
}

PathRequest::~PathRequest()
{
    using namespace std::chrono;
    auto stream = m_journal.info();
    if (!stream)
        return;

    std::string fast, full;
    if (quick_reply_ != steady_clock::time_point{})
    {
        fast = " fast:";
        fast += std::to_string(
            duration_cast<milliseconds>(quick_reply_ - created_).count());
        fast += "ms";
    }
    if (full_reply_ != steady_clock::time_point{})
    {
        full = " full:";
        full += std::to_string(
            duration_cast<milliseconds>(full_reply_ - created_).count());
        full += "ms";
    }
    stream
        << iIdentifier << " complete:" << fast << full << " total:"
        << duration_cast<milliseconds>(steady_clock::now() - created_).count()
        << "ms";
}

bool
PathRequest::isNew()
{
    std::lock_guard sl(mIndexLock);

    // does this path request still need its first full path
    return mLastIndex == 0;
}

bool
PathRequest::needsUpdate(bool newOnly, LedgerIndex index)
{
    std::lock_guard sl(mIndexLock);

    if (mInProgress)
    {
        // Another thread is handling this
        return false;
    }

    if (newOnly && (mLastIndex != 0))
    {
        // Only handling new requests, this isn't new
        return false;
    }

    if (mLastIndex >= index)
    {
        return false;
    }

    mInProgress = true;
    return true;
}

bool
PathRequest::hasCompletion()
{
    return bool(fCompletion);
}

void
PathRequest::updateComplete()
{
    std::lock_guard sl(mIndexLock);

    XRPL_ASSERT(
        mInProgress, "ripple::PathRequest::updateComplete : in progress");
    mInProgress = false;

    if (fCompletion)
    {
        fCompletion();
        fCompletion = std::function<void(void)>();
    }
}

bool
PathRequest::isValid(std::shared_ptr<AssetCache> const& crCache)
{
    if (!raSrcAccount || !raDstAccount)
        return false;

    if (!convert_all_ && (saSendMax || saDstAmount <= beast::zero))
    {
        // If send max specified, dst amt must be -1.
        jvStatus = rpcError(rpcDST_AMT_MALFORMED);
        return false;
    }

    auto const& lrLedger = crCache->getLedger();

    if (!lrLedger->exists(keylet::account(*raSrcAccount)))
    {
        // Source account does not exist.
        jvStatus = rpcError(rpcSRC_ACT_NOT_FOUND);
        return false;
    }

    auto const sleDest = lrLedger->read(keylet::account(*raDstAccount));

    Json::Value& jvDestCur =
        (jvStatus[jss::destination_currencies] = Json::arrayValue);

    if (!sleDest)
    {
        jvDestCur.append(Json::Value(systemCurrencyCode()));
        if (!saDstAmount.native())
        {
            // Only XRP can be send to a non-existent account.
            jvStatus = rpcError(rpcACT_NOT_FOUND);
            return false;
        }

        if (!convert_all_ &&
            saDstAmount < STAmount(lrLedger->fees().accountReserve(0)))
        {
            // Payment must meet reserve.
            jvStatus = rpcError(rpcDST_AMT_MALFORMED);
            return false;
        }
    }
    else
    {
        bool const disallowXRP(sleDest->getFlags() & lsfDisallowXRP);

        auto const destAssets =
            accountDestAssets(*raDstAccount, crCache, !disallowXRP);

        for (auto const& asset : destAssets)
            jvDestCur.append(to_string(asset));

        jvStatus[jss::destination_tag] =
            (sleDest->getFlags() & lsfRequireDestTag);
    }

    jvStatus[jss::ledger_hash] = to_string(lrLedger->info().hash);
    jvStatus[jss::ledger_index] = lrLedger->seq();
    return true;
}

/*  If this is a normal path request, we want to run it once "fast" now
    to give preliminary results.

    If this is a legacy path request, we are only going to run it once,
    and we can't run it in full now, so we don't want to run it at all.

    If there's an error, we need to be sure to return it to the caller
    in all cases.
*/
std::pair<bool, Json::Value>
PathRequest::doCreate(
    std::shared_ptr<AssetCache> const& cache,
    Json::Value const& value)
{
    bool valid = false;

    if (parseJson(value) != PFR_PJ_INVALID)
    {
        valid = isValid(cache);
        if (!hasCompletion() && valid)
            doUpdate(cache, true);
    }

    if (auto stream = m_journal.debug())
    {
        if (valid)
        {
            stream << iIdentifier << " valid: " << toBase58(*raSrcAccount);
            stream << iIdentifier << " deliver: " << saDstAmount.getFullText();
        }
        else
        {
            stream << iIdentifier << " invalid";
        }
    }

    return {valid, jvStatus};
}

int
PathRequest::parseJson(Json::Value const& jvParams)
{
    if (!jvParams.isMember(jss::source_account))
    {
        jvStatus = rpcError(rpcSRC_ACT_MISSING);
        return PFR_PJ_INVALID;
    }

    if (!jvParams.isMember(jss::destination_account))
    {
        jvStatus = rpcError(rpcDST_ACT_MISSING);
        return PFR_PJ_INVALID;
    }

    if (!jvParams.isMember(jss::destination_amount))
    {
        jvStatus = rpcError(rpcDST_AMT_MISSING);
        return PFR_PJ_INVALID;
    }

    raSrcAccount =
        parseBase58<AccountID>(jvParams[jss::source_account].asString());
    if (!raSrcAccount)
    {
        jvStatus = rpcError(rpcSRC_ACT_MALFORMED);
        return PFR_PJ_INVALID;
    }

    raDstAccount =
        parseBase58<AccountID>(jvParams[jss::destination_account].asString());
    if (!raDstAccount)
    {
        jvStatus = rpcError(rpcDST_ACT_MALFORMED);
        return PFR_PJ_INVALID;
    }

    if (!amountFromJsonNoThrow(saDstAmount, jvParams[jss::destination_amount]))
    {
        jvStatus = rpcError(rpcDST_AMT_MALFORMED);
        return PFR_PJ_INVALID;
    }

    convert_all_ = saDstAmount == STAmount(saDstAmount.asset(), 1u, 0, true);

    if (!validAsset(saDstAmount.asset()) ||
        (!convert_all_ && saDstAmount <= beast::zero))
    {
        jvStatus = rpcError(rpcDST_AMT_MALFORMED);
        return PFR_PJ_INVALID;
    }

    if (jvParams.isMember(jss::send_max))
    {
        // Send_max requires destination amount to be -1.
        if (!convert_all_)
        {
            jvStatus = rpcError(rpcDST_AMT_MALFORMED);
            return PFR_PJ_INVALID;
        }

        saSendMax.emplace();
        if (!amountFromJsonNoThrow(*saSendMax, jvParams[jss::send_max]) ||
            !validAsset(saSendMax->asset()) ||
            (*saSendMax <= beast::zero &&
             *saSendMax != STAmount(saSendMax->asset(), 1u, 0, true)))
        {
            jvStatus = rpcError(rpcSENDMAX_MALFORMED);
            return PFR_PJ_INVALID;
        }
    }

    if (jvParams.isMember(jss::source_currencies))
    {
        Json::Value const& jvSrcCurrencies = jvParams[jss::source_currencies];
        if (!jvSrcCurrencies.isArray() || jvSrcCurrencies.size() == 0 ||
            jvSrcCurrencies.size() > RPC::Tuning::max_src_cur)
        {
            jvStatus = rpcError(rpcSRC_CUR_MALFORMED);
            return PFR_PJ_INVALID;
        }

        sciSourceAssets.clear();

        for (auto const& c : jvSrcCurrencies)
        {
            // Mandatory currency or MPT
            if (!validJSONAsset(c) || !c.isObject())
            {
                jvStatus = rpcError(rpcSRC_CUR_MALFORMED);
                return PFR_PJ_INVALID;
            }

            PathAsset srcPathAsset;
            if (c.isMember(jss::currency))
            {
                Currency currency;
                if (!c[jss::currency].isString() ||
                    !to_currency(currency, c[jss::currency].asString()))
                {
                    jvStatus = rpcError(rpcSRC_CUR_MALFORMED);
                    return PFR_PJ_INVALID;
                }
                srcPathAsset = currency;
            }
            else
            {
                uint192 u;
                if (!c[jss::mpt_issuance_id].isString() ||
                    !u.parseHex(c[jss::mpt_issuance_id].asString()))
                {
                    jvStatus = rpcError(rpcSRC_CUR_MALFORMED);
                    return PFR_PJ_INVALID;
                }
                srcPathAsset = u;
            }

            // Optional issuer
            AccountID srcIssuerID;
            if (c.isMember(jss::issuer) &&
                (c.isMember(jss::mpt_issuance_id) ||
                 !c[jss::issuer].isString() ||
                 !to_issuer(srcIssuerID, c[jss::issuer].asString())))
            {
                jvStatus = rpcError(rpcSRC_ISR_MALFORMED);
                return PFR_PJ_INVALID;
            }

            if (srcPathAsset.holds<Currency>())
            {
                if (srcPathAsset.get<Currency>().isZero())
                {
                    if (srcIssuerID.isNonZero())
                    {
                        jvStatus = rpcError(rpcSRC_CUR_MALFORMED);
                        return PFR_PJ_INVALID;
                    }
                }
                else if (srcIssuerID.isZero())
                {
                    srcIssuerID = *raSrcAccount;
                }
            }

            if (saSendMax)
            {
                // If the assets don't match, ignore the source asset.
                if (srcPathAsset == saSendMax->asset())
                {
                    // If neither is the source and they are not equal, then the
                    // source issuer is illegal.
                    if (srcIssuerID != *raSrcAccount &&
                        saSendMax->getIssuer() != *raSrcAccount &&
                        srcIssuerID != saSendMax->getIssuer())
                    {
                        jvStatus = rpcError(rpcSRC_ISR_MALFORMED);
                        return PFR_PJ_INVALID;
                    }

                    // If both are the source, use the source.
                    // Otherwise, use the one that's not the source.
                    if (srcPathAsset.holds<Currency>())
                    {
                        if (srcIssuerID != *raSrcAccount)
                        {
                            sciSourceAssets.insert(Issue{
                                srcPathAsset.get<Currency>(), srcIssuerID});
                        }
                        else if (saSendMax->getIssuer() != *raSrcAccount)
                        {
                            sciSourceAssets.insert(Issue{
                                srcPathAsset.get<Currency>(),
                                saSendMax->getIssuer()});
                        }
                        else
                        {
                            sciSourceAssets.insert(Issue{
                                srcPathAsset.get<Currency>(), *raSrcAccount});
                        }
                    }
                    else
                        sciSourceAssets.insert(srcPathAsset.get<MPTID>());
                }
            }
            else if (srcPathAsset.holds<Currency>())
            {
                sciSourceAssets.insert(
                    Issue{srcPathAsset.get<Currency>(), srcIssuerID});
            }
            else
            {
                sciSourceAssets.insert(MPTIssue{srcPathAsset.get<MPTID>()});
            }
        }
    }

    if (jvParams.isMember(jss::id))
        jvId = jvParams[jss::id];

    if (jvParams.isMember(jss::domain))
    {
        uint256 num;
        if (!jvParams[jss::domain].isString() ||
            !num.parseHex(jvParams[jss::domain].asString()))
        {
            jvStatus = rpcError(rpcDOMAIN_MALFORMED);
            return PFR_PJ_INVALID;
        }
        else
        {
            domain = num;
        }
    }

    return PFR_PJ_NOCHANGE;
}

Json::Value
PathRequest::doClose()
{
    JLOG(m_journal.debug()) << iIdentifier << " closed";
    std::lock_guard sl(mLock);
    jvStatus[jss::closed] = true;
    return jvStatus;
}

Json::Value
PathRequest::doStatus(Json::Value const&)
{
    std::lock_guard sl(mLock);
    jvStatus[jss::status] = jss::success;
    return jvStatus;
}

void
PathRequest::doAborting() const
{
    JLOG(m_journal.info()) << iIdentifier << " aborting early";
}

std::unique_ptr<Pathfinder> const&
PathRequest::getPathFinder(
    std::shared_ptr<AssetCache> const& cache,
    hash_map<PathAsset, std::unique_ptr<Pathfinder>>& pathasset_map,
    PathAsset const& asset,
    STAmount const& dst_amount,
    int const level,
    std::function<bool(void)> const& continueCallback)
{
    auto i = pathasset_map.find(asset);
    if (i != pathasset_map.end())
        return i->second;
    auto pathfinder = std::make_unique<Pathfinder>(
        cache,
        *raSrcAccount,
        *raDstAccount,
        asset,
        std::nullopt,
        dst_amount,
        saSendMax,
        domain,
        app_);
    if (pathfinder->findPaths(level, continueCallback))
        pathfinder->computePathRanks(max_paths_, continueCallback);
    else
        pathfinder.reset();  // It's a bad request - clear it.
    return pathasset_map[asset] = std::move(pathfinder);
}

bool
PathRequest::findPaths(
    std::shared_ptr<AssetCache> const& cache,
    int const level,
    Json::Value& jvArray,
    std::function<bool(void)> const& continueCallback)
{
    auto sourceAssets = sciSourceAssets;
    if (sourceAssets.empty() && saSendMax)
    {
        sourceAssets.insert(saSendMax->asset());
    }
    if (sourceAssets.empty())
    {
        auto assets = accountSourceAssets(*raSrcAccount, cache, true);
        bool const sameAccount = *raSrcAccount == *raDstAccount;
        for (auto const& asset : assets)
        {
            if (!std::visit(
                    [&]<typename TAsset>(TAsset const& a) {
                        if (!sameAccount || a != saDstAmount.asset())
                        {
                            if (sourceAssets.size() >=
                                RPC::Tuning::max_auto_src_cur)
                                return false;
                            if constexpr (std::is_same_v<TAsset, Currency>)
                                sourceAssets.insert(Issue{
                                    a,
                                    a.isZero() ? xrpAccount() : *raSrcAccount});
                            else
                                sourceAssets.insert(MPTIssue{a});
                        }
                        return true;
                    },
                    asset.value()))
            {
                return false;
            }
        }
    }

    auto const dst_amount = convertAmount(saDstAmount, convert_all_);
    hash_map<PathAsset, std::unique_ptr<Pathfinder>> pathasset_map;
    for (auto const& asset : sourceAssets)
    {
        if (continueCallback && !continueCallback())
            break;
        JLOG(m_journal.debug())
            << iIdentifier
            << " Trying to find paths: " << STAmount(asset, 1).getFullText();

        auto& pathfinder = getPathFinder(
            cache, pathasset_map, asset, dst_amount, level, continueCallback);
        if (!pathfinder)
        {
            JLOG(m_journal.debug()) << iIdentifier << " No paths found";
            continue;
        }

        STPath fullLiquidityPath;
        auto ps = pathfinder->getBestPaths(
            max_paths_,
            fullLiquidityPath,
            mContext[asset],
            asset.getIssuer(),
            continueCallback);
        mContext[asset] = ps;

        auto const& sourceAccount = [&] {
            if (!isXRP(asset.getIssuer()))
                return asset.getIssuer();

            if (isXRP(asset))
                return xrpAccount();

            return *raSrcAccount;
        }();

        STAmount saMaxAmount = [&]() {
            if (saSendMax)
                return *saSendMax;
            if (asset.holds<Issue>())
                return STAmount(
                    Issue{asset.get<Issue>().currency, sourceAccount},
                    1u,
                    0,
                    true);
            return STAmount(asset.get<MPTIssue>(), 1u, 0, true);
        }();

        JLOG(m_journal.debug())
            << iIdentifier << " Paths found, calling rippleCalc";

        path::RippleCalc::Input rcInput;
        if (convert_all_)
            rcInput.partialPaymentAllowed = true;
        auto sandbox =
            std::make_unique<PaymentSandbox>(&*cache->getLedger(), tapNONE);
        auto rc = path::RippleCalc::rippleCalculate(
            *sandbox,
            saMaxAmount,    // --> Amount to send is unlimited
                            //     to get an estimate.
            dst_amount,     // --> Amount to deliver.
            *raDstAccount,  // --> Account to deliver to.
            *raSrcAccount,  // --> Account sending from.
            ps,             // --> Path set.
            domain,         // --> Domain.
            app_.logs(),
            &rcInput);

        if (!convert_all_ && !fullLiquidityPath.empty() &&
            (rc.result() == terNO_LINE || rc.result() == tecPATH_PARTIAL))
        {
            JLOG(m_journal.debug())
                << iIdentifier << " Trying with an extra path element";

            ps.push_back(fullLiquidityPath);
            sandbox =
                std::make_unique<PaymentSandbox>(&*cache->getLedger(), tapNONE);
            rc = path::RippleCalc::rippleCalculate(
                *sandbox,
                saMaxAmount,    // --> Amount to send is unlimited
                                //     to get an estimate.
                dst_amount,     // --> Amount to deliver.
                *raDstAccount,  // --> Account to deliver to.
                *raSrcAccount,  // --> Account sending from.
                ps,             // --> Path set.
                domain,         // --> Domain.
                app_.logs());

            if (rc.result() != tesSUCCESS)
            {
                JLOG(m_journal.warn())
                    << iIdentifier << " Failed with covering path "
                    << transHuman(rc.result());
            }
            else
            {
                JLOG(m_journal.debug())
                    << iIdentifier << " Extra path element gives "
                    << transHuman(rc.result());
            }
        }

        if (rc.result() == tesSUCCESS)
        {
            Json::Value jvEntry(Json::objectValue);
            if (rc.actualAmountIn.holds<Issue>())
                rc.actualAmountIn.setIssuer(sourceAccount);
            jvEntry[jss::source_amount] =
                rc.actualAmountIn.getJson(JsonOptions::none);
            jvEntry[jss::paths_computed] = ps.getJson(JsonOptions::none);

            if (convert_all_)
                jvEntry[jss::destination_amount] =
                    rc.actualAmountOut.getJson(JsonOptions::none);

            if (hasCompletion())
            {
                // Old ripple_path_find API requires this
                jvEntry[jss::paths_canonical] = Json::arrayValue;
            }

            jvArray.append(jvEntry);
        }
        else
        {
            JLOG(m_journal.debug()) << iIdentifier << " rippleCalc returns "
                                    << transHuman(rc.result());
        }
    }

    /*  The resource fee is based on the number of source currencies used.
        The minimum cost is 50 and the maximum is 400. The cost increases
        after four source currencies, 50 - (4 * 4) = 34.
    */
    int const size = sourceAssets.size();
    consumer_.charge({std::clamp(size * size + 34, 50, 400), "path update"});
    return true;
}

Json::Value
PathRequest::doUpdate(
    std::shared_ptr<AssetCache> const& cache,
    bool fast,
    std::function<bool(void)> const& continueCallback)
{
    using namespace std::chrono;
    JLOG(m_journal.debug())
        << iIdentifier << " update " << (fast ? "fast" : "normal");

    {
        std::lock_guard sl(mLock);

        if (!isValid(cache))
            return jvStatus;
    }

    Json::Value newStatus = Json::objectValue;

    if (hasCompletion())
    {
        // Old ripple_path_find API gives destination_currencies
        auto& destAssets =
            (newStatus[jss::destination_currencies] = Json::arrayValue);
        auto const assets = accountDestAssets(*raDstAccount, cache, true);
        for (auto const& asset : assets)
            destAssets.append(to_string(asset));
    }

    newStatus[jss::source_account] = toBase58(*raSrcAccount);
    newStatus[jss::destination_account] = toBase58(*raDstAccount);
    newStatus[jss::destination_amount] = saDstAmount.getJson(JsonOptions::none);
    newStatus[jss::full_reply] = !fast;

    if (jvId)
        newStatus[jss::id] = jvId;

    bool loaded = app_.getFeeTrack().isLoadedLocal();

    if (iLevel == 0)
    {
        // first pass
        if (loaded || fast)
            iLevel = app_.config().PATH_SEARCH_FAST;
        else
            iLevel = app_.config().PATH_SEARCH;
    }
    else if ((iLevel == app_.config().PATH_SEARCH_FAST) && !fast)
    {
        // leaving fast pathfinding
        iLevel = app_.config().PATH_SEARCH;
        if (loaded && (iLevel > app_.config().PATH_SEARCH_FAST))
            --iLevel;
    }
    else if (bLastSuccess)
    {
        // decrement, if possible
        if (iLevel > app_.config().PATH_SEARCH ||
            (loaded && (iLevel > app_.config().PATH_SEARCH_FAST)))
            --iLevel;
    }
    else
    {
        // adjust as needed
        if (!loaded && (iLevel < app_.config().PATH_SEARCH_MAX))
            ++iLevel;
        if (loaded && (iLevel > app_.config().PATH_SEARCH_FAST))
            --iLevel;
    }

    JLOG(m_journal.debug()) << iIdentifier << " processing at level " << iLevel;

    Json::Value jvArray = Json::arrayValue;
    if (findPaths(cache, iLevel, jvArray, continueCallback))
    {
        bLastSuccess = jvArray.size() != 0;
        newStatus[jss::alternatives] = std::move(jvArray);
    }
    else
    {
        bLastSuccess = false;
        newStatus = rpcError(rpcINTERNAL);
    }

    if (fast && quick_reply_ == steady_clock::time_point{})
    {
        quick_reply_ = steady_clock::now();
        mOwner.reportFast(duration_cast<milliseconds>(quick_reply_ - created_));
    }
    else if (!fast && full_reply_ == steady_clock::time_point{})
    {
        full_reply_ = steady_clock::now();
        mOwner.reportFull(duration_cast<milliseconds>(full_reply_ - created_));
    }

    {
        std::lock_guard sl(mLock);
        jvStatus = newStatus;
    }

    JLOG(m_journal.debug())
        << iIdentifier << " update finished " << (fast ? "fast" : "normal");
    return newStatus;
}

InfoSub::pointer
PathRequest::getSubscriber() const
{
    return wpSubscriber.lock();
}

}  // namespace ripple
