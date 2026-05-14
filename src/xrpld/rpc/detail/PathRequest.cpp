/** @file
 *  Per-request pathfinding state machine for the XRP Ledger.
 *
 *  Each `PathRequest` represents one outstanding client request — either a
 *  persistent `path_find` subscription or a one-shot `ripple_path_find` call.
 *  `PathRequestManager` owns and schedules these objects; this file contains
 *  the full lifecycle: JSON parsing, ledger-state validation, adaptive-depth
 *  pathfinding via `Pathfinder` + `RippleCalc`, and timing instrumentation.
 */
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

/** Constructs a subscription-mode path request (`path_find` semantics).
 *
 *  Results are pushed to `subscriber` on every ledger close.  The subscriber
 *  is held as a `weak_ptr` so that network-layer destruction does not stall
 *  background pathfinding threads.
 *
 *  @param app        The running Application instance.
 *  @param subscriber The WebSocket subscriber that issued the request.
 *  @param id         Numeric identifier for log correlation.
 *  @param owner      The managing `PathRequestManager`.
 *  @param journal    Logging sink.
 */
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

/** Constructs a one-shot path request (`ripple_path_find` semantics).
 *
 *  `completion` is invoked exactly once when `updateComplete()` is called,
 *  then cleared so subsequent calls are no-ops.  The caller supplies
 *  `consumer` directly because there is no persistent subscriber object.
 *
 *  @param app        The running Application instance.
 *  @param completion Callback fired when the single pathfinding pass finishes.
 *  @param consumer   Resource consumer used to charge for path complexity.
 *  @param id         Numeric identifier for log correlation.
 *  @param owner      The managing `PathRequestManager`.
 *  @param journal    Logging sink.
 */
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

/** Logs fast-reply, full-reply, and total lifetime latencies at `info` level.
 *
 *  Only emits if the `info` stream is active.  Times are reported in
 *  milliseconds relative to `created_`.
 */
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

/** Returns true if this request has never completed a full pathfinding pass.
 *
 *  Used by `PathRequestManager::updateAll` to prioritise new requests.
 *  Thread-safe; acquires `indexLock_`.
 *
 *  @return `true` until the first successful `doUpdate` records a ledger index.
 */
bool
PathRequest::isNew()
{
    std::scoped_lock const sl(indexLock_);
    return lastIndex_ == 0;
}

/** Atomically claims this request for processing by the calling thread.
 *
 *  Returns `true` and sets `inProgress_` if all of the following hold:
 *  no other thread is already processing this request, the `newOnly` filter
 *  does not exclude it, and `index` is strictly newer than `lastIndex_`.
 *  Callers must call `updateComplete()` when finished to release the claim.
 *
 *  @param newOnly If `true`, only requests that have never been processed
 *      (i.e. `isNew()`) are eligible.
 *  @param index   The ledger sequence number being processed; skipped if
 *      `lastIndex_` is already >= this value.
 *  @return `true` if the caller now owns the update slot; `false` otherwise.
 */
bool
PathRequest::needsUpdate(bool newOnly, LedgerIndex index)
{
    std::scoped_lock const sl(indexLock_);

    if (inProgress_)
        return false;

    if (newOnly && (lastIndex_ != 0))
        return false;

    if (lastIndex_ >= index)
        return false;

    inProgress_ = true;
    return true;
}

/** Returns true if this is a one-shot `ripple_path_find` request.
 *
 *  Used throughout the file to branch between subscription push behaviour
 *  and the legacy one-shot callback path.  After `updateComplete()` fires
 *  `fCompletion_` and clears it, subsequent calls return `false`.
 *
 *  @return `true` while a completion callback is still pending.
 */
bool
PathRequest::hasCompletion()
{
    return bool(fCompletion_);
}

/** Releases the in-progress claim and fires the one-shot completion callback.
 *
 *  Must be called by whichever thread received `true` from `needsUpdate()`,
 *  even if `doUpdate` failed.  For one-shot requests, fires `fCompletion_`
 *  then clears it so the callback is invoked at most once.
 */
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

/** Validates request parameters against live ledger state in `crCache`.
 *
 *  Verifies that the source account exists, and that the destination account
 *  can receive the requested asset (non-existent destinations may only receive
 *  XRP at or above reserve).  Populates `jvStatus_[destination_currencies]`
 *  from the destination account's current trust lines, respecting `lsfDisallowXRP`.
 *  This side-effect allows the client to build a currency picker without a
 *  separate RPC call.
 *
 *  Called both from `doCreate` and at the start of every `doUpdate`, because
 *  ledger state can change between updates.  Must be called with `lock_` held.
 *
 *  @param crCache  Snapshot of the current ledger state.
 *  @return `true` if the request is consistent with current ledger state;
 *      `false` with `jvStatus_` set to the appropriate RPC error otherwise.
 */
bool
PathRequest::isValid(std::shared_ptr<AssetCache> const& crCache)
{
    if (!raSrcAccount_ || !raDstAccount_)
        return false;

    if (!convert_all_ && (saSendMax_ || saDstAmount_ <= beast::kZERO))
    {
        jvStatus_ = rpcError(RpcDstAmtMalformed);
        return false;
    }

    auto const& lrLedger = crCache->getLedger();

    if (!lrLedger->exists(keylet::account(*raSrcAccount_)))
    {
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
            // Only XRP can be sent to a non-existent account.
            jvStatus_ = rpcError(RpcActNotFound);
            return false;
        }

        if (!convert_all_ && saDstAmount_ < STAmount(lrLedger->fees().reserve))
        {
            // Payment must meet the account reserve.
            jvStatus_ = rpcError(RpcDstAmtMalformed);
            return false;
        }
    }
    else
    {
        bool const disallowXRP((sleDest->getFlags() & lsfDisallowXRP) != 0u);

        auto const destAssets = accountDestAssets(*raDstAccount_, crCache, !disallowXRP);

        for (auto const& asset : destAssets)
            jvDestCur.append(to_string(asset));

        jvStatus_[jss::destination_tag] = (sleDest->getFlags() & lsfRequireDestTag);
    }

    jvStatus_[jss::ledger_hash] = to_string(lrLedger->header().hash);
    jvStatus_[jss::ledger_index] = lrLedger->seq();
    return true;
}

/** Initialises the request and, for subscription mode, runs a fast first pass.
 *
 *  Chains `parseJson` → `isValid`.  For subscription-mode requests (`path_find`)
 *  a fast preliminary `doUpdate` (depth = `PATH_SEARCH_FAST`) is run immediately
 *  so the client receives an initial result in the same request/response cycle.
 *  Legacy one-shot requests (`ripple_path_find`) skip this fast pass because
 *  they will be run once in full by `PathRequestManager`.
 *
 *  Errors from parsing or validation are embedded in `jvStatus_` and returned
 *  to the caller; the request is dead and should not be queued.
 *
 *  @param cache  Ledger snapshot to validate against and run the fast pass on.
 *  @param value  Raw JSON from the client's `path_find create` command.
 *  @return A pair of {validity flag, current `jvStatus_`}.  The caller should
 *      only enqueue the request when the flag is `true`.
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

/** Parses and stores the client-supplied JSON request parameters.
 *
 *  Validates the presence and format of `source_account`, `destination_account`,
 *  and `destination_amount`.  Optional fields handled:
 *  - `send_max`: only legal when `destination_amount` is the "convert all"
 *    sentinel (value `-1` / max-flag); enforces source-asset filtering and
 *    issuer reconciliation against `send_max`.
 *  - `source_currencies`: array of currency/issuer pairs or `mpt_issuance_id`
 *    hex strings, capped at `RPC::Tuning::kMAX_SRC_CUR`.
 *  - `domain`: 256-bit hex identifier restricting pathfinding to a permissioned
 *    domain; stored as `std::optional<uint256>`.
 *  - `id`: opaque echo value forwarded unchanged to every update response.
 *
 *  @param jvParams  Raw JSON from the client.
 *  @return `PFR_PJ_NOCHANGE` (0) on success, `PFR_PJ_INVALID` (-1) on any
 *      parse error (with `jvStatus_` set to the relevant RPC error code).
 *  @note `send_max` requires `destination_amount == -1`.  Providing `send_max`
 *      with a fixed destination amount is rejected with `RpcDstAmtMalformed`.
 */
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

    if (!validAsset(saDstAmount_.asset()) || (!convert_all_ && saDstAmount_ <= beast::kZERO))
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
            (*saSendMax_ <= beast::kZERO &&
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
            jvSrcCurrencies.size() > RPC::Tuning::kMAX_SRC_CUR)
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

/** Marks this request as closed and returns the final status snapshot.
 *
 *  Called by `PathRequestManager` when the subscriber disconnects or the
 *  subscription is explicitly cancelled.  Sets `jvStatus_["closed"] = true`
 *  so the client can detect the closure in buffered responses.
 *
 *  @return The final `jvStatus_` with `"closed": true` injected.
 */
json::Value
PathRequest::doClose()
{
    JLOG(journal_.debug()) << iIdentifier_ << " closed";
    std::scoped_lock const sl(lock_);
    jvStatus_[jss::closed] = true;
    return jvStatus_;
}

/** Returns the current status snapshot with `"status": "success"` injected.
 *
 *  Used by `path_find status` sub-command to let a client poll the last
 *  computed result without triggering a new search.
 *
 *  @return A copy of `jvStatus_` with the success flag set.
 */
json::Value
PathRequest::doStatus(json::Value const&)
{
    std::scoped_lock const sl(lock_);
    jvStatus_[jss::status] = jss::success;
    return jvStatus_;
}

/** Logs an early-abort event at `info` level.
 *
 *  Invoked by `PathRequestManager` when the subscriber has already gone away
 *  mid-update and the pathfinding work is being discarded before completion.
 */
void
PathRequest::doAborting() const
{
    JLOG(journal_.info()) << iIdentifier_ << " aborting early";
}

/** Returns (or constructs and ranks) a `Pathfinder` for a given source asset.
 *
 *  Looks up `currency` in `currencyMap`; on a miss, constructs a fresh
 *  `Pathfinder`, runs `findPaths` at `level`, then `computePathRanks` limited
 *  to `kMAX_PATHS` (4).  If `findPaths` fails the entry is stored as `nullptr`
 *  so callers can distinguish "not yet tried" from "tried and found nothing".
 *
 *  `currencyMap` is local to a single `findPaths` call, so `Pathfinder`
 *  objects are never reused across ledger updates.
 *
 *  @param cache           Ledger snapshot for this update cycle.
 *  @param currencyMap     Per-call cache of already-constructed pathfinders.
 *  @param currency        The source asset to find paths from.
 *  @param dstAmount       Effective destination amount (after `convert_all_`
 *      conversion).
 *  @param level           Search depth passed to `Pathfinder::findPaths`.
 *  @param continueCallback Abort predicate; pathfinding stops if it returns
 *      `false`.
 *  @return Reference to the (possibly null) `unique_ptr` in `currencyMap`.
 *  @note `raSrcAccount_` and `raDstAccount_` are asserted non-null by
 *      `isValid()` before this is called.
 */
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
        pathfinder->computePathRanks(kMAX_PATHS, continueCallback);
    }
    else
    {
        pathfinder.reset();
    }
    return currencyMap[currency] = std::move(pathfinder);
}

/** Enumerates viable payment paths for each candidate source asset and
 *  appends `RippleCalc`-estimated results to `jvArray`.
 *
 *  Source asset resolution order:
 *  1. Explicit `sciSourceAssets_` from `parseJson`.
 *  2. The asset of `saSendMax_` when no explicit set was given.
 *  3. Automatic enumeration from the source account's holdings via
 *     `accountSourceAssets`, capped at `RPC::Tuning::kMAX_AUTO_SRC_CUR`
 *     (88).  When source == destination, the destination asset is excluded.
 *
 *  For each source asset a `Pathfinder` is obtained or constructed via
 *  `getPathFinder` (per-call cache; never reused across ledger updates).
 *  `getBestPaths` selects up to `kMAX_PATHS` (4) candidates and may also
 *  return a `fullLiquidityPath` — a single path unlocking more liquidity
 *  at the cost of covering a wider graph segment.
 *
 *  `RippleCalc::rippleCalculate` is called on a non-committing
 *  `PaymentSandbox` to obtain realistic `actualAmountIn`/`actualAmountOut`
 *  estimates.  If the result is `terNO_LINE` or `tecPATH_PARTIAL` and a
 *  `fullLiquidityPath` exists, a second attempt appends it to the path
 *  set.  This two-shot fallback is skipped in `convert_all_` mode because
 *  partial payments are already allowed there.
 *
 *  Resource cost formula: `clamp(size² + 34, 50, 400)` where `size` is
 *  the number of source assets evaluated.  The quadratic term reflects
 *  that path complexity grows super-linearly with source currencies.
 *
 *  @param cache            Ledger snapshot for this update cycle.
 *  @param level            Pathfinder search depth.
 *  @param jvArray          Output JSON array; successful path alternatives
 *      are appended here.
 *  @param continueCallback Abort predicate; work stops early if it returns
 *      `false`.
 *  @return Always `true`; resource charge is applied regardless of whether
 *      any paths were found.
 *  @note Legacy `ripple_path_find` responses include an empty
 *      `"paths_canonical"` array for backwards compatibility.
 */
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
                            if (sourceAssets.size() >= RPC::Tuning::kMAX_AUTO_SRC_CUR)
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
            kMAX_PATHS, fullLiquidityPath, context_[asset], asset.getIssuer(), continueCallback);
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
            saMaxAmount,
            dstAmount,
            // NOLINTBEGIN(bugprone-unchecked-optional-access) isValid() ensures both are set
            *raDstAccount_,
            *raSrcAccount_,
            // NOLINTEND(bugprone-unchecked-optional-access)
            ps,
            domain_,
            app_,
            &rcInput);

        if (!convert_all_ && !fullLiquidityPath.empty() &&
            (rc.result() == terNO_LINE || rc.result() == tecPATH_PARTIAL))
        {
            // Two-shot fallback: append the full-liquidity covering path and
            // retry.  Often rescues paths that were partial due to limited
            // liquidity in the ranked set.
            JLOG(journal_.debug()) << iIdentifier_ << " Trying with an extra path element";

            ps.pushBack(fullLiquidityPath);
            sandbox = std::make_unique<PaymentSandbox>(&*cache->getLedger(), TapNone);
            rc = path::RippleCalc::rippleCalculate(
                *sandbox,
                saMaxAmount,
                dstAmount,
                // NOLINTBEGIN(bugprone-unchecked-optional-access) isValid() ensures both are set
                *raDstAccount_,
                *raSrcAccount_,
                // NOLINTEND(bugprone-unchecked-optional-access)
                ps,
                domain_,
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
                // Old ripple_path_find API requires this field (may be empty).
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

    // Resource cost: clamp(size² + 34, 50, 400).  The constant 34 = 50 - 4²
    // sets the inflection point at four source currencies.
    int const size = sourceAssets.size();
    consumer_.charge({std::clamp((size * size) + 34, 50, 400), "path update"});
    return true;
}

/** Runs one pathfinding pass, updates `jvStatus_`, and reports latency.
 *
 *  Calls `isValid` first; returns the current error status immediately if
 *  the request is no longer valid against `cache`.  Then adapts `iLevel_`
 *  (search depth) based on three signals:
 *  - **Server load** (`isLoadedLocal`): depth is capped at `PATH_SEARCH_FAST`.
 *  - **Fast pass**: first-pass depth is `PATH_SEARCH_FAST` regardless of load.
 *  - **Last-pass success** (`bLastSuccess_`): depth decrements toward
 *    `PATH_SEARCH` when paths were found; increments toward `PATH_SEARCH_MAX`
 *    when they were not (unless load prevents it).
 *
 *  Delegates to `findPaths` for the actual computation.  On the first fast
 *  pass, records `quick_reply_` and calls `owner_.reportFast`; on the first
 *  full pass, records `full_reply_` and calls `owner_.reportFull`.
 *
 *  For legacy `ripple_path_find` requests, `destination_currencies` is
 *  included in the result so clients can build a currency picker.
 *
 *  @param cache            Ledger snapshot for this update cycle.
 *  @param fast             `true` for a preliminary shallow search
 *      (`PATH_SEARCH_FAST` depth); `false` for the normal adaptive search.
 *  @param continueCallback Abort predicate forwarded to `findPaths`; work
 *      stops early if it returns `false`.
 *  @return The newly computed status JSON object (also stored in
 *      `jvStatus_` under `lock_`).
 */
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
        // Legacy ripple_path_find API includes destination_currencies so the
        // client can build a currency picker without a separate RPC call.
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

    // Adaptive depth: see doUpdate docstring for the full state-machine logic.
    if (iLevel_ == 0)
    {
        iLevel_ = (loaded || fast) ? app_.config().PATH_SEARCH_FAST : app_.config().PATH_SEARCH;
    }
    else if ((iLevel_ == app_.config().PATH_SEARCH_FAST) && !fast)
    {
        iLevel_ = app_.config().PATH_SEARCH;
        if (loaded && (iLevel_ > app_.config().PATH_SEARCH_FAST))
            --iLevel_;
    }
    else if (bLastSuccess_)
    {
        if (iLevel_ > app_.config().PATH_SEARCH ||
            (loaded && (iLevel_ > app_.config().PATH_SEARCH_FAST)))
            --iLevel_;
    }
    else
    {
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

/** Attempts to lock the subscriber weak pointer.
 *
 *  Returns a null `shared_ptr` if the subscriber has been destroyed by the
 *  network layer.  Callers use this as the signal to abort background work
 *  and remove the request from `PathRequestManager`.
 *
 *  @return The locked subscriber, or `nullptr` if the subscription is gone.
 */
InfoSub::pointer
PathRequest::getSubscriber() const
{
    return wpSubscriber_.lock();
}

}  // namespace xrpl
