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
    , iLevel_(0)
    , bLastSuccess_(false)
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
    , fCompletion_(completion)
    , consumer_(consumer)
    , jvStatus_(json::ValueType::Object)
    , lastIndex_(0)
    , inProgress_(false)
    , iLevel_(0)
    , bLastSuccess_(false)
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
    if (quick_reply_ != steady_clock::time_point{})
    {
        fast = " fast:";
        fast += std::to_string(duration_cast<milliseconds>(quick_reply_ - created_).count());
        fast += "ms";
    }
    if (full_reply_ != steady_clock::time_point{})
    {
        full = " full:";
        full += std::to_string(duration_cast<milliseconds>(full_reply_ - created_).count());
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

    if (!convert_all_ && (saSendMax_ || saDstAmount_ <= beast::kZero))
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

        if (!convert_all_ && saDstAmount_ < STAmount(lrLedger->fees().reserve))
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

    convert_all_ = saDstAmount_ == STAmount(saDstAmount_.asset(), 1u, 0, true);

    if (!validAsset(saDstAmount_.asset()) || (!convert_all_ && saDstAmount_ <= beast::kZero))
    {
        jvStatus_ = rpcError(RpcDstAmtMalformed);
        return PFR_PJ_INVALID;
    }

    if (jvParams.isMember(jss::send_max))
    {
        // Send_max requires destination amount to be -1.
        if (!convert_all_)
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
PathRequest::findPaths(
    std::shared_ptr<AssetCache> const& cache,
    int const level,
    json::Value& jvArray,
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

    auto const dstAmount = convertAmount(saDstAmount_, convert_all_);
    hash_map<PathAsset, std::unique_ptr<Pathfinder>> currencyMap;
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

        STPath fullLiquidityPath;
        auto ps = pathfinder->getBestPaths(
            kMaxPaths, fullLiquidityPath, context_[asset], asset.getIssuer(), continueCallback);
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
        if (convert_all_)
            rcInput.partialPaymentAllowed = true;
        auto sandbox = std::make_unique<PaymentSandbox>(&*cache->getLedger(), TapNone);
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

        if (!convert_all_ && !fullLiquidityPath.empty() &&
            (rc.result() == terNO_LINE || rc.result() == tecPATH_PARTIAL))
        {
            JLOG(journal_.debug()) << iIdentifier_ << " Trying with an extra path element";

            ps.pushBack(fullLiquidityPath);
            sandbox = std::make_unique<PaymentSandbox>(&*cache->getLedger(), TapNone);
            rc = path::RippleCalc::rippleCalculate(
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
                app_);

            if (!isTesSuccess(rc.result()))
            {
                JLOG(journal_.warn())
                    << iIdentifier_ << " Failed with covering path " << transHuman(rc.result());
            }
            else
            {
                JLOG(journal_.debug())
                    << iIdentifier_ << " Extra path element gives " << transHuman(rc.result());
            }
        }

        if (rc.result() == tesSUCCESS)
        {
            json::Value jvEntry(json::ValueType::Object);
            if (rc.actualAmountIn.holds<Issue>())
                rc.actualAmountIn.get<Issue>().account = sourceAccount;
            jvEntry[jss::source_amount] = rc.actualAmountIn.getJson(JsonOptions::Values::None);
            jvEntry[jss::paths_computed] = ps.getJson(JsonOptions::Values::None);

            if (convert_all_)
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

    bool const loaded = app_.getFeeTrack().isLoadedLocal();

    if (iLevel_ == 0)
    {
        // first pass
        if (loaded || fast)
        {
            iLevel_ = app_.config().PATH_SEARCH_FAST;
        }
        else
        {
            iLevel_ = app_.config().PATH_SEARCH;
        }
    }
    else if ((iLevel_ == app_.config().PATH_SEARCH_FAST) && !fast)
    {
        // leaving fast pathfinding
        iLevel_ = app_.config().PATH_SEARCH;
        if (loaded && (iLevel_ > app_.config().PATH_SEARCH_FAST))
            --iLevel_;
    }
    else if (bLastSuccess_)
    {
        // decrement, if possible
        if (iLevel_ > app_.config().PATH_SEARCH ||
            (loaded && (iLevel_ > app_.config().PATH_SEARCH_FAST)))
            --iLevel_;
    }
    else
    {
        // adjust as needed
        if (!loaded && (iLevel_ < app_.config().PATH_SEARCH_MAX))
            ++iLevel_;
        if (loaded && (iLevel_ > app_.config().PATH_SEARCH_FAST))
            --iLevel_;
    }

    JLOG(journal_.debug()) << iIdentifier_ << " processing at level " << iLevel_;

    json::Value jvArray = json::ValueType::Array;
    if (findPaths(cache, iLevel_, jvArray, continueCallback))
    {
        bLastSuccess_ = jvArray.size() != 0;
        newStatus[jss::alternatives] = std::move(jvArray);
    }
    else
    {
        bLastSuccess_ = false;
        newStatus = rpcError(RpcInternal);
    }

    if (fast && quick_reply_ == steady_clock::time_point{})
    {
        quick_reply_ = steady_clock::now();
        owner_.reportFast(duration_cast<milliseconds>(quick_reply_ - created_));
    }
    else if (!fast && full_reply_ == steady_clock::time_point{})
    {
        full_reply_ = steady_clock::now();
        owner_.reportFull(duration_cast<milliseconds>(full_reply_ - created_));
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
