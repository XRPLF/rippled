#include <xrpld/rpc/detail/PathRequest.h>

#include <xrpld/app/main/Application.h>
#include <xrpld/rpc/detail/AccountAssets.h>
#include <xrpld/rpc/detail/GraphPathfinder.h>
#include <xrpld/rpc/detail/PathRequestManager.h>
#include <xrpld/rpc/detail/PathfinderUtils.h>
#include <xrpld/rpc/detail/PayGraph.h>
#include <xrpld/rpc/detail/Tuning.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/UnorderedContainers.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/json_writer.h>
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
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <variant>

namespace xrpl {

PathRequest::PathRequest(
    Application& app,
    std::shared_ptr<InfoSub> const& subscriber,
    int id,
    PathRequestManager& owner,
    beast::Journal journal)
    : app_(app)
    , journal_(journal)
    , owner_(owner)
    , wpSubscriber_(subscriber)
    , consumer_(subscriber->getConsumer())
    , jvStatus_(json::ValueType::Object)
    , lastIndex_(0)
    , inProgress_(false)
    , iIdentifier_(id)
    , created_(std::chrono::steady_clock::now())
{
    JLOG(journal_.debug()) << iIdentifier_ << " created";
}

PathRequest::PathRequest(
    Application& app,
    std::function<void(void)> const& completion,
    Resource::Consumer& consumer,
    int id,
    PathRequestManager& owner,
    beast::Journal journal)
    : app_(app)
    , journal_(journal)
    , owner_(owner)
    , fCompletion_(std::move(completion))
    , consumer_(consumer)
    , jvStatus_(json::ValueType::Object)
    , lastIndex_(0)
    , inProgress_(false)
    , iIdentifier_(id)
    , created_(std::chrono::steady_clock::now())
{
    JLOG(journal_.debug()) << iIdentifier_ << " created";
}

PathRequest::~PathRequest()
{
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
PathRequest::isNew()
{
    std::scoped_lock const sl(indexLock_);

    // does this path request still need its first full path
    return lastIndex_ == 0;
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

    if (newOnly && (lastIndex_ != 0))
    {
        // Only handling new requests, this isn't new
        return false;
    }

    if (lastIndex_ >= index)
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
PathRequest::updateComplete()
{
    std::scoped_lock const sl(indexLock_);

    XRPL_ASSERT(inProgress_, "xrpl::PathRequest::updateComplete : in progress");
    inProgress_ = false;

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

    auto const& lrLedger = crCache->getLedger();

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
        if (!hasCompletion() && valid)
            doUpdate(cache, true);
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
            jvSrcCurrencies.size() > RPC::Tuning::kMaxSrcCur)
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

std::unique_ptr<GraphPathfinder> const&
PathRequest::getGraphPathFinder(
    std::shared_ptr<PayGraph> const& graph,
    std::shared_ptr<AssetCache> const& cache,
    hash_map<PathAsset, std::unique_ptr<GraphPathfinder>>& currencyMap,
    PathAsset const& currency,
    std::optional<AccountID> const& srcIssuer,
    STAmount const& dstAmount,
    bool const fast,
    std::function<bool(void)> const& continueCallback)
{
    auto i = currencyMap.find(currency);
    if (i != currencyMap.end())
        return i->second;
    // NOLINTBEGIN(bugprone-unchecked-optional-access) isValid() ensures both are set
    auto pathfinder = std::make_unique<GraphPathfinder>(
        graph,
        cache,
        *raSrcAccount_,
        *raDstAccount_,
        currency,
        srcIssuer,
        dstAmount,
        saSendMax_,
        domain_,
        app_);
    // NOLINTEND(bugprone-unchecked-optional-access)
    if (pathfinder->findPaths(continueCallback))
    {
        // On the fast pass, skip ranking — unranked paths are returned
        // immediately via getBestPaths' fallback, giving a near-instant
        // first response. The full pass will rank them properly.
        if (!fast)
            pathfinder->computePathRanks(kMaxPaths, continueCallback);
    }
    else
    {
        pathfinder.reset();
    }
    return currencyMap[currency] = std::move(pathfinder);
}

bool
PathRequest::findPaths(
    std::shared_ptr<AssetCache> const& cache,
    json::Value& jvArray,
    bool const fast,
    std::function<bool(void)> const& continueCallback)
{
    auto sourceAssets = sciSourceAssets_;
    if (sourceAssets.empty() && saSendMax_)
    {
        sourceAssets.insert(saSendMax_->asset());
    }
    if (sourceAssets.empty())
    {
        // NOLINTBEGIN(bugprone-unchecked-optional-access) isValid() ensures both are set
        auto assets = accountSourceAssets(*raSrcAccount_, cache, true);
        bool const sameAccount = *raSrcAccount_ == *raDstAccount_;
        // NOLINTEND(bugprone-unchecked-optional-access)
        for (auto const& asset : assets)
        {
            if (!std::visit(
                    [&]<typename TAsset>(TAsset const& a) {
                        if (!sameAccount || a != saDstAmount_.asset())
                        {
                            if (sourceAssets.size() >= RPC::Tuning::kMaxAutoSrcCur)
                                return false;
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
                        return true;
                    },
                    asset.value()))
            {
                return false;
            }
        }
    }

    auto const dstAmount = convertAmount(saDstAmount_, convertAll_);

    // Shared post-processing: run rippleCalc, append JSON.
    //
    // We deliberately do NOT carry paths across ticks.  Yen's K-Shortest
    // re-runs every tick on the current PayGraph snapshot, so the freshly
    // discovered paths already reflect the latest order-book state.
    // Feeding the previous tick's results back in as extraPaths would only
    // re-price stale path shapes and let them out-rank current ones on a
    // quality tie.
    auto processResult = [&](STPathSet ps, Asset const& asset) {
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

        JLOG(journal_.info()) << iIdentifier_ << " rippleCalc src=" << toBase58(*raSrcAccount_)
                              << " dst=" << toBase58(*raDstAccount_)
                              << " sendMax=" << saMaxAmount.getFullText()
                              << " dstAmt=" << dstAmount.getFullText()
                              << " paths=" << json::Compact{ps.getJson(JsonOptions::Values::None)};
        JLOG(journal_.debug()) << iIdentifier_ << " Paths found, calling rippleCalc";

        path::RippleCalc::Input rcInput;
        if (convertAll_)
            rcInput.partialPaymentAllowed = true;
        auto sandbox = std::make_unique<PaymentSandbox>(&*cache->getLedger(), TapNone);

        // rippleCalculate only catches FlowException internally; other exceptions
        // (e.g. std::overflow_error from AMMLiquidity::generateFibSeqOffer) can
        // propagate.  Catch them here so one bad path doesn't abort the entire
        // processResult and lose all alternatives.
        auto safeCalc = [&](PaymentSandbox& sb,
                            STAmount const& maxSend,
                            STAmount const& dst,
                            STPathSet const& paths,
                            path::RippleCalc::Input const* inp) -> path::RippleCalc::Output {
            try
            {
                return path::RippleCalc::rippleCalculate(
                    sb, maxSend, dst, *raDstAccount_, *raSrcAccount_, paths, domain_, app_, inp);
            }
            catch (std::exception const& e)
            {
                JLOG(journal_.debug()) << iIdentifier_ << " rippleCalc exception: " << e.what();
                path::RippleCalc::Output out;
                out.setResult(tefEXCEPTION);
                return out;
            }
        };

        auto rc = safeCalc(*sandbox, saMaxAmount, dstAmount, ps, &rcInput);

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
            JLOG(journal_.info()) << iIdentifier_ << " rippleCalc returns "
                                  << transHuman(rc.result());
        }
    };

    // When a domain filter is active, build a domain-specific PayGraph so
    // domain-only offers (stored in domainBooks_ rather than allBooks_) are
    // visible to the pathfinder.  The global PayGraph is built with nullopt
    // and therefore cannot see them.
    std::shared_ptr<PayGraph> domainGraph;
    if (domain_)
    {
        if (auto* ledger = cache->getLedger().get())
        {
            domainGraph = PayGraph::build(app_.getOrderBookDB(), *ledger, domain_, journal_);
        }
    }

    auto baseGraph = domain_ ? domainGraph : owner_.getPayGraph();
    if (auto graph = baseGraph)
    {
        // Fast path: Yen's K-Shortest on the pre-built asset graph — O(μs).
        hash_map<PathAsset, std::unique_ptr<GraphPathfinder>> graphMap;
        for (auto const& asset : sourceAssets)
        {
            if (continueCallback && !continueCallback())
                break;
            JLOG(journal_.debug())
                << iIdentifier_ << " Trying to find paths: " << STAmount(asset, 1).getFullText();

            // Extract the gateway issuer from the asset so GraphPathfinder
            // can build srcAmount_ with the correct issuer account.
            std::optional<AccountID> assetIssuer;
            if (asset.holds<Issue>() && !isXRP(asset.get<Issue>().currency))
                assetIssuer = asset.get<Issue>().account;

            auto& pf = getGraphPathFinder(
                graph,
                cache,
                graphMap,
                PathAsset(asset),
                assetIssuer,
                dstAmount,
                fast,
                continueCallback);
            if (!pf)
            {
                JLOG(journal_.debug()) << iIdentifier_ << " No paths found";
                continue;
            }

            auto ps = pf->getBestPaths(kMaxPaths, STPathSet{}, asset.getIssuer(), continueCallback);
            processResult(std::move(ps), asset);
        }
    }
    else
    {
        // PayGraph not yet ready — caller should surface RpcNotReady.
        return false;
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
    std::function<bool(void)> const& continueCallback)
{
    using namespace std::chrono;
    JLOG(journal_.debug()) << iIdentifier_ << " update " << (fast ? "fast" : "normal");

    {
        std::scoped_lock const sl(lock_);

        if (!isValid(cache))
            return jvStatus_;
    }

    json::Value newStatus = json::ValueType::Object;

    if (hasCompletion())
    {
        // Old ripple_path_find API gives destination_currencies
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

    if (!owner_.getPayGraph())
    {
        JLOG(journal_.info()) << iIdentifier_ << " PayGraph not ready, returning notReady";
        newStatus = rpcError(RpcNotReady);
    }
    else
    {
        json::Value jvArray = json::ValueType::Array;
        if (findPaths(cache, jvArray, fast, continueCallback))
        {
            newStatus[jss::alternatives] = std::move(jvArray);
        }
        else
        {
            newStatus = rpcError(RpcInternal);
        }
    }

    if (fast && quickReply_ == steady_clock::time_point{})
    {
        quickReply_ = steady_clock::now();
        owner_.reportFast(duration_cast<milliseconds>(quickReply_ - created_));
    }
    else if (!fast && fullReply_ == steady_clock::time_point{})
    {
        fullReply_ = steady_clock::now();
        owner_.reportFull(duration_cast<milliseconds>(fullReply_ - created_));
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
