#include <xrpld/app/main/Application.h>
#include <xrpld/app/paths/AccountCurrencies.h>
#include <xrpld/app/paths/PathRequest.h>
#include <xrpld/app/paths/PathRequests.h>
#include <xrpld/app/paths/detail/PathfinderUtils.h>
#include <xrpld/core/Config.h>
#include <xrpld/rpc/detail/Tuning.h>

#include <xrpl/basics/Log.h>
#include <xrpl/beast/core/LexicalCast.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/server/LoadFeeTrack.h>
#include <xrpl/server/NetworkOPs.h>
#include <xrpl/tx/paths/RippleCalc.h>

#include <optional>
#include <tuple>

namespace xrpl {

PathRequest::PathRequest(
    Application& app,
    std::shared_ptr<InfoSub> const& subscriber,
    int id,
    PathRequests& owner,
    beast::Journal journal)
    : app_(app)
    , journal_(journal)
    , owner_(owner)
    , wpSubscriber(subscriber)
    , consumer_(subscriber->getConsumer())
    , jvStatus(Json::objectValue)
    , lastIndex_(0)
    , inProgress_(false)
    , iLevel(0)
    , bLastSuccess(false)
    , iIdentifier(id)
    , created_(std::chrono::steady_clock::now())
{
    JLOG(journal_.debug()) << iIdentifier << " created";
}

PathRequest::PathRequest(
    Application& app,
    std::function<void(void)> const& completion,
    Resource::Consumer& consumer,
    int id,
    PathRequests& owner,
    beast::Journal journal)
    : app_(app)
    , journal_(journal)
    , owner_(owner)
    , fCompletion(completion)
    , consumer_(consumer)
    , jvStatus(Json::objectValue)
    , lastIndex_(0)
    , inProgress_(false)
    , iLevel(0)
    , bLastSuccess(false)
    , iIdentifier(id)
    , created_(std::chrono::steady_clock::now())
{
    JLOG(journal_.debug()) << iIdentifier << " created";
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
    stream << iIdentifier << " complete:" << fast << full
           << " total:" << duration_cast<milliseconds>(steady_clock::now() - created_).count()
           << "ms";
}

bool
PathRequest::isNew()
{
    std::lock_guard sl(indexLock_);

    // does this path request still need its first full path
    return lastIndex_ == 0;
}

bool
PathRequest::needsUpdate(bool newOnly, LedgerIndex index)
{
    std::lock_guard sl(indexLock_);

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
    return bool(fCompletion);
}

void
PathRequest::updateComplete()
{
    std::lock_guard sl(indexLock_);

    XRPL_ASSERT(inProgress_, "xrpl::PathRequest::updateComplete : in progress");
    inProgress_ = false;

    if (fCompletion)
    {
        fCompletion();
        fCompletion = std::function<void(void)>();
    }
}

bool
PathRequest::isValid(std::shared_ptr<RippleLineCache> const& crCache)
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

    Json::Value& jvDestCur = (jvStatus[jss::destination_currencies] = Json::arrayValue);

    if (!sleDest)
    {
        jvDestCur.append(Json::Value(systemCurrencyCode()));
        if (!saDstAmount.native())
        {
            // Only XRP can be send to a non-existent account.
            jvStatus = rpcError(rpcACT_NOT_FOUND);
            return false;
        }

        if (!convert_all_ && saDstAmount < STAmount(lrLedger->fees().reserve))
        {
            // Payment must meet reserve.
            jvStatus = rpcError(rpcDST_AMT_MALFORMED);
            return false;
        }
    }
    else
    {
        bool const disallowXRP(sleDest->getFlags() & lsfDisallowXRP);

        auto usDestCurrID = accountDestCurrencies(*raDstAccount, crCache, !disallowXRP);

        for (auto const& currency : usDestCurrID)
            jvDestCur.append(to_string(currency));
        jvStatus[jss::destination_tag] = (sleDest->getFlags() & lsfRequireDestTag);
    }

    jvStatus[jss::ledger_hash] = to_string(lrLedger->header().hash);
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
PathRequest::doCreate(std::shared_ptr<RippleLineCache> const& cache, Json::Value const& value)
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

    raSrcAccount = parseBase58<AccountID>(jvParams[jss::source_account].asString());
    if (!raSrcAccount)
    {
        jvStatus = rpcError(rpcSRC_ACT_MALFORMED);
        return PFR_PJ_INVALID;
    }

    raDstAccount = parseBase58<AccountID>(jvParams[jss::destination_account].asString());
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

    convert_all_ = saDstAmount == STAmount(saDstAmount.issue(), 1u, 0, true);

    if ((saDstAmount.getCurrency().isZero() && saDstAmount.getIssuer().isNonZero()) ||
        (saDstAmount.getCurrency() == badCurrency()) ||
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
            (saSendMax->getCurrency().isZero() && saSendMax->getIssuer().isNonZero()) ||
            (saSendMax->getCurrency() == badCurrency()) ||
            (*saSendMax <= beast::zero && *saSendMax != STAmount(saSendMax->issue(), 1u, 0, true)))
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

        sciSourceCurrencies.clear();

        for (auto const& c : jvSrcCurrencies)
        {
            // Mandatory currency
            Currency srcCurrencyID;
            if (!c.isObject() || !c.isMember(jss::currency) || !c[jss::currency].isString() ||
                !to_currency(srcCurrencyID, c[jss::currency].asString()))
            {
                jvStatus = rpcError(rpcSRC_CUR_MALFORMED);
                return PFR_PJ_INVALID;
            }

            // Optional issuer
            AccountID srcIssuerID;
            if (c.isMember(jss::issuer) &&
                (!c[jss::issuer].isString() || !to_issuer(srcIssuerID, c[jss::issuer].asString())))
            {
                jvStatus = rpcError(rpcSRC_ISR_MALFORMED);
                return PFR_PJ_INVALID;
            }

            if (srcCurrencyID.isZero())
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

            if (saSendMax)
            {
                // If the currencies don't match, ignore the source currency.
                if (srcCurrencyID == saSendMax->getCurrency())
                {
                    // If neither is the source and they are not equal, then the
                    // source issuer is illegal.
                    if (srcIssuerID != *raSrcAccount && saSendMax->getIssuer() != *raSrcAccount &&
                        srcIssuerID != saSendMax->getIssuer())
                    {
                        jvStatus = rpcError(rpcSRC_ISR_MALFORMED);
                        return PFR_PJ_INVALID;
                    }

                    // If both are the source, use the source.
                    // Otherwise, use the one that's not the source.
                    if (srcIssuerID != *raSrcAccount)
                    {
                        sciSourceCurrencies.insert({srcCurrencyID, srcIssuerID});
                    }
                    else if (saSendMax->getIssuer() != *raSrcAccount)
                    {
                        sciSourceCurrencies.insert({srcCurrencyID, saSendMax->getIssuer()});
                    }
                    else
                    {
                        sciSourceCurrencies.insert({srcCurrencyID, *raSrcAccount});
                    }
                }
            }
            else
            {
                sciSourceCurrencies.insert({srcCurrencyID, srcIssuerID});
            }
        }
    }

    if (jvParams.isMember(jss::id))
        jvId = jvParams[jss::id];

    if (jvParams.isMember(jss::domain))
    {
        uint256 num;
        if (!jvParams[jss::domain].isString() || !num.parseHex(jvParams[jss::domain].asString()))
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
    JLOG(journal_.debug()) << iIdentifier << " closed";
    std::lock_guard sl(lock_);
    jvStatus[jss::closed] = true;
    return jvStatus;
}

Json::Value
PathRequest::doStatus(Json::Value const&)
{
    std::lock_guard sl(lock_);
    jvStatus[jss::status] = jss::success;
    return jvStatus;
}

void
PathRequest::doAborting() const
{
    JLOG(journal_.info()) << iIdentifier << " aborting early";
}

std::unique_ptr<Pathfinder> const&
PathRequest::getPathFinder(
    std::shared_ptr<RippleLineCache> const& cache,
    hash_map<Currency, std::unique_ptr<Pathfinder>>& currencyMap,
    Currency const& currency,
    STAmount const& dstAmount,
    int const level,
    std::function<bool(void)> const& continueCallback)
{
    auto i = currencyMap.find(currency);
    if (i != currencyMap.end())
        return i->second;
    auto pathfinder = std::make_unique<Pathfinder>(
        cache,
        *raSrcAccount,
        *raDstAccount,
        currency,
        std::nullopt,
        dstAmount,
        saSendMax,
        domain,
        app_);
    if (pathfinder->findPaths(level, continueCallback))
        pathfinder->computePathRanks(max_paths_, continueCallback);
    else
        pathfinder.reset();  // It's a bad request - clear it.
    return currencyMap[currency] = std::move(pathfinder);
}

bool
PathRequest::findPaths(
    std::shared_ptr<RippleLineCache> const& cache,
    int const level,
    Json::Value& jvArray,
    std::function<bool(void)> const& continueCallback)
{
    auto sourceCurrencies = sciSourceCurrencies;
    if (sourceCurrencies.empty() && saSendMax)
    {
        sourceCurrencies.insert(saSendMax->issue());
    }
    if (sourceCurrencies.empty())
    {
        auto currencies = accountSourceCurrencies(*raSrcAccount, cache, true);
        bool const sameAccount = *raSrcAccount == *raDstAccount;
        for (auto const& c : currencies)
        {
            if (!sameAccount || c != saDstAmount.getCurrency())
            {
                if (sourceCurrencies.size() >= RPC::Tuning::max_auto_src_cur)
                    return false;
                sourceCurrencies.insert({c, c.isZero() ? xrpAccount() : *raSrcAccount});
            }
        }
    }

    auto const dstAmount = convertAmount(saDstAmount, convert_all_);
    hash_map<Currency, std::unique_ptr<Pathfinder>> currencyMap;
    for (auto const& issue : sourceCurrencies)
    {
        if (continueCallback && !continueCallback())
            break;
        JLOG(journal_.debug()) << iIdentifier
                               << " Trying to find paths: " << STAmount(issue, 1).getFullText();

        auto& pathfinder =
            getPathFinder(cache, currencyMap, issue.currency, dstAmount, level, continueCallback);
        if (!pathfinder)
        {
            JLOG(journal_.debug()) << iIdentifier << " No paths found";
            continue;
        }

        STPath fullLiquidityPath;
        auto ps = pathfinder->getBestPaths(
            max_paths_, fullLiquidityPath, context_[issue], issue.account, continueCallback);
        context_[issue] = ps;

        auto const& sourceAccount = [&] {
            if (!isXRP(issue.account))
                return issue.account;

            if (isXRP(issue.currency))
                return xrpAccount();

            return *raSrcAccount;
        }();

        STAmount saMaxAmount =
            saSendMax.value_or(STAmount(Issue{issue.currency, sourceAccount}, 1u, 0, true));

        JLOG(journal_.debug()) << iIdentifier << " Paths found, calling rippleCalc";

        path::RippleCalc::Input rcInput;
        if (convert_all_)
            rcInput.partialPaymentAllowed = true;
        auto sandbox = std::make_unique<PaymentSandbox>(&*cache->getLedger(), tapNONE);
        auto rc = path::RippleCalc::rippleCalculate(
            *sandbox,
            saMaxAmount,    // --> Amount to send is unlimited
                            //     to get an estimate.
            dstAmount,      // --> Amount to deliver.
            *raDstAccount,  // --> Account to deliver to.
            *raSrcAccount,  // --> Account sending from.
            ps,             // --> Path set.
            domain,         // --> Domain.
            app_.logs(),
            &rcInput);

        if (!convert_all_ && !fullLiquidityPath.empty() &&
            (rc.result() == terNO_LINE || rc.result() == tecPATH_PARTIAL))
        {
            JLOG(journal_.debug()) << iIdentifier << " Trying with an extra path element";

            ps.push_back(fullLiquidityPath);
            sandbox = std::make_unique<PaymentSandbox>(&*cache->getLedger(), tapNONE);
            rc = path::RippleCalc::rippleCalculate(
                *sandbox,
                saMaxAmount,    // --> Amount to send is unlimited
                                //     to get an estimate.
                dstAmount,      // --> Amount to deliver.
                *raDstAccount,  // --> Account to deliver to.
                *raSrcAccount,  // --> Account sending from.
                ps,             // --> Path set.
                domain,         // --> Domain.
                app_.logs());

            if (rc.result() != tesSUCCESS)
            {
                JLOG(journal_.warn())
                    << iIdentifier << " Failed with covering path " << transHuman(rc.result());
            }
            else
            {
                JLOG(journal_.debug())
                    << iIdentifier << " Extra path element gives " << transHuman(rc.result());
            }
        }

        if (rc.result() == tesSUCCESS)
        {
            Json::Value jvEntry(Json::objectValue);
            rc.actualAmountIn.setIssuer(sourceAccount);
            jvEntry[jss::source_amount] = rc.actualAmountIn.getJson(JsonOptions::none);
            jvEntry[jss::paths_computed] = ps.getJson(JsonOptions::none);

            if (convert_all_)
                jvEntry[jss::destination_amount] = rc.actualAmountOut.getJson(JsonOptions::none);

            if (hasCompletion())
            {
                // Old ripple_path_find API requires this
                jvEntry[jss::paths_canonical] = Json::arrayValue;
            }

            jvArray.append(jvEntry);
        }
        else
        {
            JLOG(journal_.debug())
                << iIdentifier << " rippleCalc returns " << transHuman(rc.result());
        }
    }

    /*  The resource fee is based on the number of source currencies used.
        The minimum cost is 50 and the maximum is 400. The cost increases
        after four source currencies, 50 - (4 * 4) = 34.
    */
    int const size = sourceCurrencies.size();
    consumer_.charge({std::clamp(size * size + 34, 50, 400), "path update"});
    return true;
}

Json::Value
PathRequest::doUpdate(
    std::shared_ptr<RippleLineCache> const& cache,
    bool fast,
    std::function<bool(void)> const& continueCallback)
{
    using namespace std::chrono;
    JLOG(journal_.debug()) << iIdentifier << " update " << (fast ? "fast" : "normal");

    {
        std::lock_guard sl(lock_);

        if (!isValid(cache))
            return jvStatus;
    }

    Json::Value newStatus = Json::objectValue;

    if (hasCompletion())
    {
        // Old ripple_path_find API gives destination_currencies
        auto& destCurrencies = (newStatus[jss::destination_currencies] = Json::arrayValue);
        auto usCurrencies = accountDestCurrencies(*raDstAccount, cache, true);
        for (auto const& c : usCurrencies)
            destCurrencies.append(to_string(c));
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

    JLOG(journal_.debug()) << iIdentifier << " processing at level " << iLevel;

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
        owner_.reportFast(duration_cast<milliseconds>(quick_reply_ - created_));
    }
    else if (!fast && full_reply_ == steady_clock::time_point{})
    {
        full_reply_ = steady_clock::now();
        owner_.reportFull(duration_cast<milliseconds>(full_reply_ - created_));
    }

    {
        std::lock_guard sl(lock_);
        jvStatus = newStatus;
    }

    JLOG(journal_.debug()) << iIdentifier << " update finished " << (fast ? "fast" : "normal");
    return newStatus;
}

InfoSub::pointer
PathRequest::getSubscriber() const
{
    return wpSubscriber.lock();
}

}  // namespace xrpl
