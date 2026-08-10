#include <xrpld/rpc/detail/PathRequest.h>

#include <xrpld/app/main/Application.h>
#include <xrpld/core/Config.h>
#include <xrpld/rpc/detail/AccountAssets.h>
#include <xrpld/rpc/detail/PathRequestManager.h>
#include <xrpld/rpc/detail/Pathfinder.h>
#include <xrpld/rpc/detail/PathfinderUtils.h>
#include <xrpld/rpc/detail/Tuning.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/UnorderedContainers.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/PaymentSandbox.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/PathAsset.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STPathSet.h>
#include <xrpl/protocol/SystemParameters.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/resource/Consumer.h>
#include <xrpl/server/InfoSub.h>
#include <xrpl/server/LoadFeeTrack.h>
#include <xrpl/tx/paths/RippleCalc.h>

#include <algorithm>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace xrpl {

PathRequest::PathRequest(
    Application& app,
    std::shared_ptr<InfoSub> const& subscriber,
    int id,
    PathRequestManager& owner,
    beast::Journal journal)
    : app_(app)
    , journal_(journal)
    , owner_(&owner)
    , wpSubscriber_(subscriber)
    , consumer_(subscriber->getConsumer())
    , jvStatus_(json::ValueType::Object)
    , lastIndex_(0)
    , inProgress_(false)
    , iLevel_(0)
    , bLastSuccess_(false)
    , lastFullSearchIndex_(0)
    , iIdentifier_(id)
    , created_(std::chrono::steady_clock::now())
{
    JLOG(journal_.debug()) << iIdentifier_ << " created";
}

PathRequest::PathRequest(
    Application& app,
    std::function<void(void)> completion,
    resource::Consumer& consumer,
    int id,
    PathRequestManager& owner,
    beast::Journal journal)
    : app_(app)
    , journal_(journal)
    , owner_(&owner)
    , fCompletion_(std::move(completion))
    , consumer_(consumer)
    , jvStatus_(json::ValueType::Object)
    , lastIndex_(0)
    , inProgress_(false)
    , iLevel_(0)
    , bLastSuccess_(false)
    , lastFullSearchIndex_(0)
    , iIdentifier_(id)
    , created_(std::chrono::steady_clock::now())
{
    JLOG(journal_.debug()) << iIdentifier_ << " created";
}

void
PathRequest::detachFromManager() noexcept
{
    owner_ = nullptr;
}

PathRequest::~PathRequest()
{
    // WS disconnect or last strong-ref drop: unhook from the manager so the
    // shared AssetCache can be released when no sessions remain. owner_ is
    // null if ~PathRequestManager already detached this session.
    if (owner_)
        owner_->removePathRequest(this);

    using namespace std::chrono;
    auto stream = journal_.info();
    if (!stream)
        return;

    std::string fast, full;
    if (quickReply_ != steady_clock::time_point{})
    {
        fast = " fast:";
        fast += std::to_string(duration_cast<milliseconds>(quickReply_ - created_).count());
        fast += "ms";
    }
    if (fullReply_ != steady_clock::time_point{})
    {
        full = " full:";
        full += std::to_string(duration_cast<milliseconds>(fullReply_ - created_).count());
        full += "ms";
    }
    stream << iIdentifier_ << " complete:" << fast << full
           << " total:" << duration_cast<milliseconds>(steady_clock::now() - created_).count()
           << "ms";
}

bool
PathRequest::isNew() const
{
    std::scoped_lock const sl(indexLock_);
    return !firstUpdateDone_;
}

bool
PathRequest::needsUpdate(bool newOnly, LedgerIndex index)
{
    std::scoped_lock const sl(indexLock_);

    if (inProgress_)
    {
        // Another thread is handling this
        return false;
    }

    if (newOnly && firstUpdateDone_)
    {
        // Only handling brand-new sessions
        return false;
    }

    // Already finished a pinned update for this ledger (or newer). Open first
    // updates leave lastIndex_ at 0 so the same-seq closed wave still runs.
    if (lastIndex_ != 0 && lastIndex_ >= index)
    {
        return false;
    }

    inProgress_ = true;
    return true;
}

bool
PathRequest::hasCompletion()
{
    return bool(fCompletion_);
}

void
PathRequest::updateComplete(std::optional<LedgerIndex> pinSeq, bool completedWork)
{
    std::scoped_lock const sl(indexLock_);

    // Idempotent: updateAll may release remaining batch claims after a throw
    // while ClaimGuard already cleared the request that failed.
    if (!inProgress_)
        return;

    inProgress_ = false;

    if (!completedWork)
    {
        // Abandoned claim / drop: clear inProgress only.
        return;
    }

    firstUpdateDone_ = true;
    if (pinSeq)
    {
        // Closed (or explicit pin): skip reprocess of this seq until next.
        lastIndex_ = *pinSeq;
    }

    if (fCompletion_)
    {
        fCompletion_();
        fCompletion_ = std::function<void(void)>();
    }
}

bool
PathRequest::isValid(std::shared_ptr<AssetCache> const& crCache)
{
    if (!raSrcAccount_ || !raDstAccount_)
        return false;

    if (!convertAll_ && (saSendMax_ || saDstAmount_ <= beast::kZero))
    {
        // If send max specified, dst amt must be -1.
        jvStatus_ = rpcError(RpcDstAmtMalformed);
        return false;
    }

    // By-value snapshot: keeps ReadView alive if the shared cache advances.
    auto const lrLedger = crCache->getLedger();

    if (!lrLedger->exists(keylet::account(*raSrcAccount_)))
    {
        // Source account does not exist.
        jvStatus_ = rpcError(RpcSrcActNotFound);
        return false;
    }

    auto const sleDest = lrLedger->read(keylet::account(*raDstAccount_));

    json::Value& jvDestCur = (jvStatus_[jss::destination_currencies] = json::ValueType::Array);

    if (!sleDest)
    {
        jvDestCur.append(json::Value(systemCurrencyCode()));
        if (!saDstAmount_.native())
        {
            // Only XRP can be send to a non-existent account.
            jvStatus_ = rpcError(RpcActNotFound);
            return false;
        }

        if (!convertAll_ && saDstAmount_ < STAmount(lrLedger->fees().reserve))
        {
            // Payment must meet reserve.
            jvStatus_ = rpcError(RpcDstAmtMalformed);
            return false;
        }
    }
    else
    {
        bool const disallowXRP(sleDest->isFlag(lsfDisallowXRP));

        auto const destAssets = accountDestAssets(*raDstAccount_, crCache, !disallowXRP);

        for (auto const& asset : destAssets)
            jvDestCur.append(to_string(asset));

        jvStatus_[jss::destination_tag] = (sleDest->getFlags() & lsfRequireDestTag);
    }

    jvStatus_[jss::ledger_hash] = to_string(lrLedger->header().hash);
    jvStatus_[jss::ledger_index] = lrLedger->seq();
    return true;
}

/*  If this is a normal path request, we want to run it once "fast" now
    to give preliminary results.

    If this is a legacy path request, we are only going to run it once,
    and we can't run it in full now, so we don't want to run it at all.

    If there's an error, we need to be sure to return it to the caller
    in all cases.
*/
std::pair<bool, json::Value>
PathRequest::doCreate(std::shared_ptr<AssetCache> const& cache, json::Value const& value)
{
    bool valid = false;

    if (parseJson(value) != PFR_PJ_INVALID)
    {
        valid = isValid(cache);
        // WS subscription: run a fast Pathfinder for the create reply. Claim
        // inProgress_ so concurrent updateAll / mid-close cannot race context_
        // (same PathRequest). Do not pin lastIndex_ — isNew() stays true until
        // the first completed updateAll wave.
        if (!hasCompletion() && valid)
        {
            {
                std::scoped_lock const sl(indexLock_);
                // Create is single-threaded per request; should never be in flight.
                XRPL_ASSERT(!inProgress_, "xrpl::PathRequest::doCreate : not in progress");
                inProgress_ = true;
            }
            try
            {
                doUpdate(cache, true);
            }
            catch (...)
            {
                updateComplete();
                throw;
            }
            // Clear claim without pinning so the first updateAll still runs.
            updateComplete();
        }
    }

    if (auto stream = journal_.debug())
    {
        if (valid)
        {
            stream << iIdentifier_ << " valid: " << toBase58(*raSrcAccount_);
            stream << iIdentifier_ << " deliver: " << saDstAmount_.getFullText();
        }
        else
        {
            stream << iIdentifier_ << " invalid";
        }
    }

    return {valid, jvStatus_};
}

int
PathRequest::parseJson(json::Value const& jvParams)
{
    if (!jvParams.isMember(jss::source_account))
    {
        jvStatus_ = rpcError(RpcSrcActMissing);
        return PFR_PJ_INVALID;
    }

    if (!jvParams.isMember(jss::destination_account))
    {
        jvStatus_ = rpcError(RpcDstActMissing);
        return PFR_PJ_INVALID;
    }

    if (!jvParams.isMember(jss::destination_amount))
    {
        jvStatus_ = rpcError(RpcDstAmtMissing);
        return PFR_PJ_INVALID;
    }

    raSrcAccount_ = parseBase58<AccountID>(jvParams[jss::source_account].asString());
    if (!raSrcAccount_)
    {
        jvStatus_ = rpcError(RpcSrcActMalformed);
        return PFR_PJ_INVALID;
    }

    raDstAccount_ = parseBase58<AccountID>(jvParams[jss::destination_account].asString());
    if (!raDstAccount_)
    {
        jvStatus_ = rpcError(RpcDstActMalformed);
        return PFR_PJ_INVALID;
    }

    if (!amountFromJsonNoThrow(saDstAmount_, jvParams[jss::destination_amount]))
    {
        jvStatus_ = rpcError(RpcDstAmtMalformed);
        return PFR_PJ_INVALID;
    }

    convertAll_ = saDstAmount_ == STAmount(saDstAmount_.asset(), 1u, 0, true);

    if (!validAsset(saDstAmount_.asset()) || (!convertAll_ && saDstAmount_ <= beast::kZero))
    {
        jvStatus_ = rpcError(RpcDstAmtMalformed);
        return PFR_PJ_INVALID;
    }

    if (jvParams.isMember(jss::send_max))
    {
        // Send_max requires destination amount to be -1.
        if (!convertAll_)
        {
            jvStatus_ = rpcError(RpcDstAmtMalformed);
            return PFR_PJ_INVALID;
        }

        saSendMax_.emplace();
        if (!amountFromJsonNoThrow(*saSendMax_, jvParams[jss::send_max]) ||
            !validAsset(saSendMax_->asset()) ||
            (*saSendMax_ <= beast::kZero &&
             *saSendMax_ != STAmount(saSendMax_->asset(), 1u, 0, true)))
        {
            jvStatus_ = rpcError(RpcSendmaxMalformed);
            return PFR_PJ_INVALID;
        }
    }

    if (jvParams.isMember(jss::source_currencies))
    {
        json::Value const& jvSrcCurrencies = jvParams[jss::source_currencies];
        if (!jvSrcCurrencies.isArray() || jvSrcCurrencies.size() == 0 ||
            jvSrcCurrencies.size() > rpc::tuning::kMaxSrcCur)
        {
            jvStatus_ = rpcError(RpcSrcCurMalformed);
            return PFR_PJ_INVALID;
        }

        sciSourceAssets_.clear();

        for (auto const& c : jvSrcCurrencies)
        {
            // Mandatory currency or MPT
            if (!validJSONAsset(c) || !c.isObject())
            {
                jvStatus_ = rpcError(RpcSrcCurMalformed);
                return PFR_PJ_INVALID;
            }

            PathAsset srcPathAsset;
            if (c.isMember(jss::currency))
            {
                Currency currency;
                if (!c[jss::currency].isString() ||
                    !toCurrency(currency, c[jss::currency].asString()))
                {
                    jvStatus_ = rpcError(RpcSrcCurMalformed);
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
                    jvStatus_ = rpcError(RpcSrcCurMalformed);
                    return PFR_PJ_INVALID;
                }
                srcPathAsset = u;
            }

            // Optional issuer
            AccountID srcIssuerID;
            if (c.isMember(jss::issuer) &&
                (c.isMember(jss::mpt_issuance_id) || !c[jss::issuer].isString() ||
                 !toIssuer(srcIssuerID, c[jss::issuer].asString())))
            {
                jvStatus_ = rpcError(RpcSrcIsrMalformed);
                return PFR_PJ_INVALID;
            }

            if (srcPathAsset.holds<Currency>())
            {
                if (srcPathAsset.get<Currency>().isZero())
                {
                    if (srcIssuerID.isNonZero())
                    {
                        jvStatus_ = rpcError(RpcSrcCurMalformed);
                        return PFR_PJ_INVALID;
                    }
                }
                else if (srcIssuerID.isZero())
                {
                    srcIssuerID = *raSrcAccount_;
                }
            }

            if (saSendMax_)
            {
                // If the assets don't match, ignore the source asset.
                if (srcPathAsset == saSendMax_->asset())
                {
                    // If neither is the source and they are not equal, then the
                    // source issuer is illegal.
                    if (srcIssuerID != *raSrcAccount_ &&
                        saSendMax_->getIssuer() != *raSrcAccount_ &&
                        srcIssuerID != saSendMax_->getIssuer())
                    {
                        jvStatus_ = rpcError(RpcSrcIsrMalformed);
                        return PFR_PJ_INVALID;
                    }

                    // If both are the source, use the source.
                    // Otherwise, use the one that's not the source.
                    srcPathAsset.visit(
                        [&](Currency const& currency) {
                            if (srcIssuerID != *raSrcAccount_)
                            {
                                sciSourceAssets_.insert(Issue{currency, srcIssuerID});
                            }
                            else if (saSendMax_->getIssuer() != *raSrcAccount_)
                            {
                                sciSourceAssets_.insert(Issue{currency, saSendMax_->getIssuer()});
                            }
                            {
                                sciSourceAssets_.insert(Issue{currency, *raSrcAccount_});
                            }
                        },
                        [&](MPTID const& mpt) { sciSourceAssets_.insert(mpt); });
                }
            }
            else
            {
                srcPathAsset.visit(
                    [&](Currency const& currency) {
                        sciSourceAssets_.insert(Issue{currency, srcIssuerID});
                    },
                    [&](MPTID const& mpt) { sciSourceAssets_.insert(MPTIssue{mpt}); });
            }
        }
    }

    if (jvParams.isMember(jss::id))
        jvId_ = jvParams[jss::id];

    if (jvParams.isMember(jss::domain))
    {
        uint256 num;
        if (!jvParams[jss::domain].isString() || !num.parseHex(jvParams[jss::domain].asString()))
        {
            jvStatus_ = rpcError(RpcDomainMalformed);
            return PFR_PJ_INVALID;
        }

        domain_ = num;
    }

    return PFR_PJ_NOCHANGE;
}

json::Value
PathRequest::doClose()
{
    JLOG(journal_.debug()) << iIdentifier_ << " closed";
    // Detach immediately so AssetCache can reclaim if this was the last session
    // (do not wait for ~PathRequest / next updateAll scavenge).
    if (owner_)
        owner_->removePathRequest(this);
    std::scoped_lock const sl(lock_);
    jvStatus_[jss::closed] = true;
    return jvStatus_;
}

json::Value
PathRequest::doStatus(json::Value const&)
{
    std::scoped_lock const sl(lock_);
    jvStatus_[jss::status] = jss::success;
    return jvStatus_;
}

void
PathRequest::doAborting() const
{
    JLOG(journal_.info()) << iIdentifier_ << " aborting early";
}

std::unique_ptr<Pathfinder> const&
PathRequest::getPathFinder(
    std::shared_ptr<AssetCache> const& cache,
    hash_map<PathAsset, std::unique_ptr<Pathfinder>>& currencyMap,
    PathAsset const& currency,
    STAmount const& dstAmount,
    int const level,
    std::function<bool(void)> const& continueCallback)
{
    auto i = currencyMap.find(currency);
    if (i != currencyMap.end())
        return i->second;
    // NOLINTBEGIN(bugprone-unchecked-optional-access) isValid() ensures both are set
    auto pathfinder = std::make_unique<Pathfinder>(
        cache,
        *raSrcAccount_,
        *raDstAccount_,
        currency,
        std::nullopt,
        dstAmount,
        saSendMax_,
        domain_,
        app_);
    // NOLINTEND(bugprone-unchecked-optional-access)
    if (pathfinder->findPaths(level, continueCallback))
    {
        pathfinder->computePathRanks(kMaxPaths, continueCallback);
    }
    else
    {
        pathfinder.reset();  // It's a bad request - clear it.
    }
    return currencyMap[currency] = std::move(pathfinder);
}

bool
PathRequest::revalidatePaths(
    std::shared_ptr<AssetCache> const& cache,
    Asset const& asset,
    STPathSet const& paths,
    STAmount const& dstAmount,
    json::Value& jvArray,
    std::shared_ptr<ReadView const> const& calcLedger)
{
    if (paths.empty())
        return false;

    auto const& sourceAccount = [&] {
        if (!isXRP(asset.getIssuer()))
            return asset.getIssuer();
        if (isXRP(asset))
            return xrpAccount();
        return *raSrcAccount_;
    }();

    STAmount const saMaxAmount = [&]() {
        if (saSendMax_)
            return *saSendMax_;
        return asset.visit(
            [&](Issue const& issue) {
                return STAmount(Issue{issue.currency, sourceAccount}, 1u, 0, true);
            },
            [](MPTIssue const& issue) { return STAmount(issue, 1u, 0, true); });
    }();

    path::RippleCalc::Input rcInput;
    if (convertAll_)
        rcInput.partialPaymentAllowed = true;

    // Mid-close may pass the open ledger for fresh offers/balances while line
    // vectors still come from the shared AssetCache.
    auto const ledger = calcLedger ? calcLedger : cache->getLedger();
    PaymentSandbox sandbox(&*ledger, TapNone);
    auto rc = path::RippleCalc::rippleCalculate(
        sandbox,
        saMaxAmount,
        dstAmount,
        *raDstAccount_,
        *raSrcAccount_,
        paths,
        domain_,
        app_,
        &rcInput);

    if (rc.result() != tesSUCCESS)
    {
        JLOG(journal_.debug()) << iIdentifier_ << " revalidate failed: " << transHuman(rc.result());
        return false;
    }

    json::Value jvEntry(json::ValueType::Object);
    if (rc.actualAmountIn.holds<Issue>())
        rc.actualAmountIn.get<Issue>().account = sourceAccount;
    jvEntry[jss::source_amount] = rc.actualAmountIn.getJson(JsonOptions::Values::None);
    jvEntry[jss::paths_computed] = paths.getJson(JsonOptions::Values::None);

    if (convertAll_)
    {
        jvEntry[jss::destination_amount] = rc.actualAmountOut.getJson(JsonOptions::Values::None);
    }

    if (hasCompletion())
        jvEntry[jss::paths_canonical] = json::ValueType::Array;

    jvArray.append(std::move(jvEntry));
    return true;
}

bool
PathRequest::findPaths(
    std::shared_ptr<AssetCache> const& cache,
    int const level,
    json::Value& jvArray,
    std::function<bool(void)> const& continueCallback,
    bool fullSearch,
    bool allowEscalate,
    bool& didFullSearch,
    std::shared_ptr<ReadView const> const& calcLedger)
{
    didFullSearch = false;
    auto sourceAssets = sciSourceAssets_;
    if (sourceAssets.empty() && saSendMax_)
    {
        sourceAssets.insert(saSendMax_->asset());
    }
    if (sourceAssets.empty())
    {
        // Absolute hard cap (legacy ripple_path_find): exceeding is an error.
        // Soft cap is for WS subscriptions only so concurrent Pathfinder waves
        // stay bounded. One-shot ripple_path_find never silent-truncates under
        // load — clients expect a complete auto source set (or a hard error).
        std::size_t const hardMax = static_cast<std::size_t>(rpc::tuning::kMaxAutoSrcCur);
        std::size_t softMax = hardMax;
        if (!hasCompletion())
        {
            softMax = static_cast<std::size_t>(rpc::tuning::kMaxAutoSrcCurSub);
            if (app_.getFeeTrack().isLoadedLocal())
            {
                softMax =
                    std::min(softMax, static_cast<std::size_t>(rpc::tuning::kMaxAutoSrcCurLoaded));
            }
        }

        // accountSourceAssets returns a hash_set (unspecified order). Build a
        // deterministic list so soft truncation is stable across runs: XRP
        // first, then currency/MPT codes sorted by hex.
        // NOLINTBEGIN(bugprone-unchecked-optional-access) isValid() ensures both are set
        auto assets = accountSourceAssets(*raSrcAccount_, cache, true);
        bool const sameAccount = *raSrcAccount_ == *raDstAccount_;
        // NOLINTEND(bugprone-unchecked-optional-access)
        std::vector<PathAsset> ordered;
        ordered.reserve(assets.size());
        for (auto const& asset : assets)
            ordered.push_back(asset);
        std::sort(ordered.begin(), ordered.end(), [](PathAsset const& a, PathAsset const& b) {
            return to_string(a) < to_string(b);
        });

        for (auto const& asset : ordered)
        {
            bool overHard = false;
            bool atSoft = false;
            std::visit(
                [&]<typename TAsset>(TAsset const& a) {
                    if (!sameAccount || a != saDstAmount_.asset())
                    {
                        if (sourceAssets.size() >= hardMax)
                        {
                            overHard = true;
                            return;
                        }
                        if (sourceAssets.size() >= softMax)
                        {
                            atSoft = true;
                            return;
                        }
                        if constexpr (std::is_same_v<TAsset, Currency>)
                        {
                            sourceAssets.insert(
                                Issue{a, a.isZero() ? xrpAccount() : *raSrcAccount_});
                        }
                        else
                        {
                            sourceAssets.insert(MPTIssue{a});
                        }
                    }
                },
                asset.value());
            if (overHard)
                return false;
            if (atSoft)
                break;
        }
    }

    auto const dstAmount = convertAmount(saDstAmount_, convertAll_);
    hash_map<PathAsset, std::unique_ptr<Pathfinder>> currencyMap;

    // Cheap path: revalidate previously discovered paths without Pathfinder.
    // Prefer this whenever a full graph search is not required (first/fast/
    // failed last / staggered rediscovery).
    if (!fullSearch)
    {
        bool anyOk = false;
        for (auto const& asset : sourceAssets)
        {
            if (continueCallback && !continueCallback())
                break;
            auto it = context_.find(asset);
            if (it == context_.end() || it->second.empty())
                continue;
            if (revalidatePaths(cache, asset, it->second, dstAmount, jvArray, calcLedger))
                anyOk = true;
        }

        if (anyOk)
        {
            int const size = static_cast<int>(sourceAssets.size());
            consumer_.charge({std::clamp((size * size) / 2 + 20, 25, 200), "path revalidate"});
            JLOG(journal_.debug()) << iIdentifier_ << " incremental revalidate ok ("
                                   << jvArray.size() << " alternatives)";
            return true;
        }

        // No prior paths worked. Escalating on every failed revalidate was the
        // main wave-cost blowup under load. Mid-close ticks pass
        // allowEscalate=false; closed waves escalate only when fullSearch was
        // already selected (rediscovery / failed backoff).
        if (!allowEscalate)
        {
            JLOG(journal_.debug())
                << iIdentifier_ << " incremental revalidate empty/failed; no escalate";
            return true;
        }

        JLOG(journal_.debug()) << iIdentifier_
                               << " incremental revalidate empty/failed; full search";
        fullSearch = true;
    }

    didFullSearch = true;

    for (auto const& asset : sourceAssets)
    {
        if (continueCallback && !continueCallback())
            break;
        JLOG(journal_.debug()) << iIdentifier_
                               << " Trying to find paths: " << STAmount(asset, 1).getFullText();

        auto& pathfinder =
            getPathFinder(cache, currencyMap, PathAsset(asset), dstAmount, level, continueCallback);
        if (!pathfinder)
        {
            JLOG(journal_.debug()) << iIdentifier_ << " No paths found";
            continue;
        }

        auto ps = pathfinder->getBestPaths(
            kMaxPaths, context_[asset], asset.getIssuer(), continueCallback);
        context_[asset] = ps;

        auto const& sourceAccount = [&] {
            if (!isXRP(asset.getIssuer()))
                return asset.getIssuer();

            if (isXRP(asset))
                return xrpAccount();

            return *raSrcAccount_;
        }();

        STAmount const saMaxAmount = [&]() {
            if (saSendMax_)
                return *saSendMax_;
            return asset.visit(
                [&](Issue const& issue) {
                    return STAmount(Issue{issue.currency, sourceAccount}, 1u, 0, true);
                },
                [](MPTIssue const& issue) { return STAmount(issue, 1u, 0, true); });
        }();

        JLOG(journal_.debug()) << iIdentifier_ << " Paths found, calling rippleCalc";

        path::RippleCalc::Input rcInput;
        if (convertAll_)
            rcInput.partialPaymentAllowed = true;
        auto const ledger = calcLedger ? calcLedger : cache->getLedger();
        auto sandbox = std::make_unique<PaymentSandbox>(&*ledger, TapNone);
        auto rc = path::RippleCalc::rippleCalculate(
            *sandbox,
            saMaxAmount,  // --> Amount to send is unlimited
                          //     to get an estimate.
            dstAmount,    // --> Amount to deliver.
            // NOLINTBEGIN(bugprone-unchecked-optional-access) isValid() ensures both are set
            *raDstAccount_,  // --> Account to deliver to.
            *raSrcAccount_,  // --> Account sending from.
            // NOLINTEND(bugprone-unchecked-optional-access)
            ps,       // --> Path set.
            domain_,  // --> Domain.
            app_,
            &rcInput);

        // No covering/full-liquidity spare path: alternatives are exactly the
        // best maxPaths set from getBestPaths.

        if (rc.result() == tesSUCCESS)
        {
            json::Value jvEntry(json::ValueType::Object);
            if (rc.actualAmountIn.holds<Issue>())
                rc.actualAmountIn.get<Issue>().account = sourceAccount;
            jvEntry[jss::source_amount] = rc.actualAmountIn.getJson(JsonOptions::Values::None);
            jvEntry[jss::paths_computed] = ps.getJson(JsonOptions::Values::None);

            if (convertAll_)
            {
                jvEntry[jss::destination_amount] =
                    rc.actualAmountOut.getJson(JsonOptions::Values::None);
            }

            if (hasCompletion())
            {
                // Old ripple_path_find API requires this
                jvEntry[jss::paths_canonical] = json::ValueType::Array;
            }

            jvArray.append(jvEntry);
        }
        else
        {
            JLOG(journal_.debug())
                << iIdentifier_ << " rippleCalc returns " << transHuman(rc.result());
        }
    }

    /*  The resource fee is based on the number of source currencies used.
        The minimum cost is 50 and the maximum is 400. The cost increases
        after four source currencies, 50 - (4 * 4) = 34.
    */
    int const size = sourceAssets.size();
    consumer_.charge({std::clamp((size * size) + 34, 50, 400), "path update"});
    return true;
}

json::Value
PathRequest::doUpdate(
    std::shared_ptr<AssetCache> const& cache,
    bool fast,
    std::function<bool(void)> const& continueCallback,
    bool revalidateOnly,
    std::shared_ptr<ReadView const> const& calcLedger)
{
    using namespace std::chrono;
    JLOG(journal_.debug()) << iIdentifier_ << " update " << (fast ? "fast" : "normal")
                           << (revalidateOnly ? " revalidate_only" : "");

    // Pin every account loaded via getRippleLines on this thread to this
    // session so shared hubs stay cached until *this* path_find ends.
    AssetCache::SessionPin const sessionPin{iIdentifier_};

    // One-shot ripple_path_find: load/expand up to the per-account cap so the
    // single reply sees the full line set (budget permitting).
    // WebSocket path_find: default LoadScope (64-line chunks) and progressive
    // expand across later closed-ledger updates.
    std::optional<AssetCache::LoadScope> lineLoadScope;
    if (hasCompletion())
        lineLoadScope.emplace(app_.config().pathFindMaxLinesPerAccount);

    {
        std::scoped_lock const sl(lock_);

        if (!isValid(cache))
            return jvStatus_;
    }

    json::Value newStatus = json::ValueType::Object;

    if (hasCompletion())
    {
        // Old ripple_path_find API lists destination_currencies. Build it only
        // after the destination account is pinned and any incomplete shared
        // progressive fill is drained under LoadScope — otherwise a WS partial
        // (64-line) cache hit would silently omit currencies the dest can receive.
        // NOLINTNEXTLINE(bugprone-unchecked-optional-access) isValid() ensures both are set
        (void)cache->getRippleLines(*raDstAccount_);
        while (cache->expandIncompleteLinesForSession(iIdentifier_))
        {
        }

        auto& destAssets = (newStatus[jss::destination_currencies] = json::ValueType::Array);
        // NOLINTNEXTLINE(bugprone-unchecked-optional-access) isValid() ensures both are set
        auto const assets = accountDestAssets(*raDstAccount_, cache, true);
        for (auto const& asset : assets)
            destAssets.append(to_string(asset));
    }

    // NOLINTBEGIN(bugprone-unchecked-optional-access) isValid() ensures both are set
    newStatus[jss::source_account] = toBase58(*raSrcAccount_);
    newStatus[jss::destination_account] = toBase58(*raDstAccount_);
    // NOLINTEND(bugprone-unchecked-optional-access)
    newStatus[jss::destination_amount] = saDstAmount_.getJson(JsonOptions::Values::None);
    newStatus[jss::full_reply] = !fast;

    if (jvId_)
        newStatus[jss::id] = jvId_;

    bool const loaded = app_.getFeeTrack().isLoadedLocal();
    bool const isSubscription = !hasCompletion();

    if (iLevel_ == 0)
    {
        // first pass
        if (loaded || fast)
        {
            iLevel_ = app_.config().pathSearchFast;
        }
        else
        {
            iLevel_ = app_.config().pathSearch;
        }
    }
    else if ((iLevel_ == app_.config().pathSearchFast) && !fast)
    {
        // leaving fast pathfinding
        iLevel_ = app_.config().pathSearch;
        if (loaded && (iLevel_ > app_.config().pathSearchFast))
            --iLevel_;
    }
    else if (bLastSuccess_)
    {
        // decrement, if possible
        if (iLevel_ > app_.config().pathSearch ||
            (loaded && (iLevel_ > app_.config().pathSearchFast)))
            --iLevel_;
    }
    else
    {
        // Failed last attempt: deepen search for one-shot/legacy requests.
        // WS subscriptions freeze depth so concurrent rediscovery does not
        // ratchet every session toward pathSearchMax.
        if (!isSubscription && !loaded && (iLevel_ < app_.config().pathSearchMax))
            ++iLevel_;
        if (loaded && (iLevel_ > app_.config().pathSearchFast))
            --iLevel_;
    }

    // Subscriptions: hard-cap search depth so staggered rediscovery stays cheap.
    if (isSubscription && !fast)
    {
        int const cap = loaded ? app_.config().pathSearchFast : app_.config().pathSearch;
        if (iLevel_ > cap)
            iLevel_ = cap;
    }

    // Prefer calcLedger seq for rediscovery timing when mid-close passes open.
    auto const ledgerForSeq = calcLedger ? calcLedger : cache->getLedger();
    auto const ledgerSeq = ledgerForSeq->seq();

    // Full Pathfinder when: first/fast update, failed-search backoff elapsed, or
    // staggered rediscovery is due. Timed rediscovery is skipped while the
    // server is locally loaded (revalidate-only until load eases).
    //
    // revalidateOnly (mid-close / periodic only): never Pathfinder — keeps the
    // 500ms tick cheap. Closed-ledger waves pass revalidateOnly=false so
    // rediscovery and failed recovery still run (staggered / backoff).
    //
    // lastFullSearchIndex_ is only stamped for *non-fast* Pathfinder runs so a
    // fast doCreate cannot block the first non-fast updateAll Pathfinder.
    //
    // Stagger: dueAt = lastFull + interval + (id % interval).
    // Config clamps pathFullSearchInterval to 1–100; still guard % 0.
    auto const interval = std::max<std::uint32_t>(1, app_.config().pathFullSearchInterval);
    bool rediscoveryDue = false;
    if (!revalidateOnly && !fast && lastFullSearchIndex_ != 0 && bLastSuccess_ && !loaded)
    {
        auto const stagger = static_cast<LedgerIndex>(iIdentifier_ % interval);
        auto const dueAt = lastFullSearchIndex_ + interval + stagger;
        rediscoveryDue = ledgerSeq >= dueAt;
    }

    bool failedSearchDue = false;
    if (!revalidateOnly && !fast && !bLastSuccess_)
    {
        if (lastFullSearchIndex_ == 0)
            failedSearchDue = true;
        else
            failedSearchDue =
                ledgerSeq >= lastFullSearchIndex_ + rpc::tuning::kPathFailedSearchInterval;
    }

    // One-shot ripple_path_find: drain incomplete fills for *this session's*
    // pinned accounts only. Must not call expandIncompleteLines() on a shared
    // AssetCache — that walks every incomplete hub any WS session cached and
    // can load up to max_total_lines under unique_lock in one RPC.
    // WS subscriptions: PathRequestManager expands once per closed wave.
    if (!revalidateOnly && hasCompletion())
    {
        while (cache->expandIncompleteLinesForSession(iIdentifier_))
        {
        }
    }

    // WS only: progressive fills bump lineEpoch(); escalate Pathfinder when this
    // session has not searched against the latest epoch. Stagger so concurrent
    // subscriptions do not all full-search every close while a whale chunks in.
    // One-shot already filled above and always full-searches via lastFull==0.
    auto const lineEpoch = cache->lineEpoch();
    bool const linesNewer = isSubscription && (lineEpoch != lastLineEpoch_);
    bool growthSearch = false;
    if (linesNewer && !revalidateOnly && !loaded)
    {
        auto const growInterval = std::max<LedgerIndex>(1, interval / 4);
        auto const stagger = static_cast<LedgerIndex>(iIdentifier_ % growInterval);
        growthSearch =
            lastFullSearchIndex_ == 0 || ledgerSeq >= lastFullSearchIndex_ + growInterval + stagger;
    }

    bool const fullSearch = !revalidateOnly &&
        (fast || lastFullSearchIndex_ == 0 || failedSearchDue || rediscoveryDue || growthSearch);

    // Subscriptions never escalate a failed revalidate into Pathfinder unless
    // this wave already chose fullSearch (first / rediscovery / failed backoff /
    // progressive line growth). One-shot ripple_path_find may still escalate.
    bool const allowEscalate = !revalidateOnly && (!isSubscription || fullSearch);

    JLOG(journal_.debug()) << iIdentifier_ << " processing at level " << iLevel_
                           << (fullSearch ? (growthSearch          ? " full_search(lines_grew)"
                                                 : rediscoveryDue  ? " full_search(rediscovery)"
                                                 : failedSearchDue ? " full_search(failed_backoff)"
                                                                   : " full_search")
                                          : " revalidate");

    json::Value jvArray = json::ValueType::Array;
    bool didFullSearch = false;
    if (findPaths(
            cache,
            iLevel_,
            jvArray,
            continueCallback,
            fullSearch,
            allowEscalate,
            didFullSearch,
            calcLedger))
    {
        // Non-escalating revalidate produced nothing (mid-close revalidateOnly,
        // or closed subscription revalidate without fullSearch): restore last
        // alternatives for display so the client is not blanked, with an
        // explicit warning. bLastSuccess_ is forced false so the next closed
        // wave can Pathfinder via failedSearchDue (not a silent success).
        bool restoredStale = false;
        if (jvArray.size() == 0 && isSubscription && !didFullSearch)
        {
            std::scoped_lock const sl(lock_);
            if (jvStatus_.isMember(jss::alternatives) && jvStatus_[jss::alternatives].size() > 0)
            {
                jvArray = jvStatus_[jss::alternatives];
                restoredStale = true;
            }
        }

        if (restoredStale)
        {
            bLastSuccess_ = false;
            newStatus[jss::full_reply] = false;
            newStatus[jss::warning] = "path_revalidate_failed";
        }
        else
        {
            bLastSuccess_ = jvArray.size() != 0;
        }

        // Stamp only non-fast Pathfinder runs. Fast create must leave
        // lastFullSearchIndex_ at 0 so the first updateAll still full-searches.
        if (didFullSearch && !fast)
            lastFullSearchIndex_ = ledgerSeq;
        // Capture post-search epoch (Pathfinder may have loaded new accounts).
        if (didFullSearch)
            lastLineEpoch_ = cache->lineEpoch();
        newStatus[jss::alternatives] = std::move(jvArray);
    }
    else
    {
        bLastSuccess_ = false;
        if (didFullSearch && !fast)
            lastFullSearchIndex_ = ledgerSeq;
        if (didFullSearch)
            lastLineEpoch_ = cache->lineEpoch();
        newStatus = rpcError(RpcInternal);
    }

    // Incomplete owner-dir fill for accounts this session pinned (not cache-global).
    // Do not overwrite path_revalidate_failed (stale restore takes precedence).
    if (!newStatus.isMember(jss::error) && !newStatus.isMember(jss::warning) &&
        cache->hasIncompleteLinesForSession(iIdentifier_))
        newStatus[jss::warning] = "path_lines_partial";

    if (fast && quickReply_ == steady_clock::time_point{})
    {
        quickReply_ = steady_clock::now();
        if (owner_)
            owner_->reportFast(duration_cast<milliseconds>(quickReply_ - created_));
    }
    else if (!fast && fullReply_ == steady_clock::time_point{})
    {
        fullReply_ = steady_clock::now();
        if (owner_)
            owner_->reportFull(duration_cast<milliseconds>(fullReply_ - created_));
    }

    {
        std::scoped_lock const sl(lock_);
        jvStatus_ = newStatus;
    }

    JLOG(journal_.debug()) << iIdentifier_ << " update finished " << (fast ? "fast" : "normal");
    return newStatus;
}

InfoSub::pointer
PathRequest::getSubscriber() const
{
    return wpSubscriber_.lock();
}

}  // namespace xrpl
