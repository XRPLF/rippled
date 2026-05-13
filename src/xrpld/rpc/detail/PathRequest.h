/** @file
 *  Per-request pathfinding state machine for the XRP Ledger RPC layer.
 *
 *  Declares `PathRequest`, which backs a single in-flight payment-path query.
 *  Each instance operates in one of two modes: a persistent `path_find`
 *  WebSocket subscription or a one-shot `ripple_path_find` call.
 *  `PathRequestManager` schedules and owns (weakly) these objects; the calling
 *  context is responsible for holding the strong reference.
 */
#pragma once

#include <xrpld/rpc/detail/AssetCache.h>
#include <xrpld/rpc/detail/Pathfinder.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/Ledger.h>
#include <xrpl/protocol/PathAsset.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/server/InfoSub.h>

#include <map>
#include <mutex>
#include <optional>
#include <set>

namespace xrpl {

class AssetCache;
class PathRequestManager;

/** Return value from `parseJson` indicating a parse or validation error. */
#define PFR_PJ_INVALID (-1)

/** Return value from `parseJson` indicating success (no changed state to
 *  signal; the zero value is a relic of the original C-style API). */
#define PFR_PJ_NOCHANGE 0

/** Per-request pathfinding state machine bridging the RPC layer and
 *  `Pathfinder`/`RippleCalc`.
 *
 *  Each instance backs one client query — either a persistent `path_find`
 *  WebSocket subscription (results pushed on every ledger close) or a
 *  one-shot `ripple_path_find` call (runs once, fires a completion callback).
 *  The two modes are distinguished at construction time by which constructor
 *  is called and by `hasCompletion()` throughout the implementation.
 *
 *  **Ownership invariant**: `PathRequestManager` stores only `weak_ptr`
 *  references.  The calling context — the WebSocket subscriber for
 *  `path_find`, or the coroutine for `ripple_path_find` — must hold the
 *  strong pointer for the lifetime of the request.  When the strong pointer
 *  is released the request is destroyed and automatically pruned from the
 *  manager's weak-reference vector.
 *
 *  **Thread safety**: `lock_` serialises access to `jvStatus_`.
 *  `indexLock_` serialises the scheduler fields `lastIndex_` and
 *  `inProgress_`.  Both are `recursive_mutex` because completion callbacks
 *  may re-enter through the call chain.  `doUpdate()` must not hold `lock_`
 *  while executing the potentially expensive `findPaths()` — it takes a
 *  fresh snapshot of valid state, releases the lock, does the work, then
 *  re-acquires to store the result.
 */
class PathRequest final : public InfoSubRequest,
                          public std::enable_shared_from_this<PathRequest>,
                          public CountedObject<PathRequest>
{
public:
    using wptr = std::weak_ptr<PathRequest>;
    using pointer = std::shared_ptr<PathRequest>;
    using ref = pointer const&;
    using wref = wptr const&;

public:
    /** Constructs a subscription-mode (`path_find`) path request.
     *
     *  Results are pushed to `subscriber` after every ledger close via
     *  `doUpdate()`.  The subscriber is held as a `weak_ptr` so that
     *  network-layer destruction does not stall background pathfinding
     *  threads.  `getSubscriber()` returning null is the signal to abort.
     *
     *  @param app        The running Application instance.
     *  @param subscriber The WebSocket subscriber that issued the request.
     *  @param id         Numeric identifier used for log correlation.
     *  @param owner      The `PathRequestManager` that schedules updates.
     *  @param journal    Logging sink.
     */
    PathRequest(
        Application& app,
        std::shared_ptr<InfoSub> const& subscriber,
        int id,
        PathRequestManager&,
        beast::Journal journal);

    /** Constructs a one-shot (`ripple_path_find`) path request.
     *
     *  `completion` is invoked exactly once when `updateComplete()` is
     *  called, then cleared to prevent double-firing.  There is no
     *  subscriber; `consumer` is supplied directly by the caller.
     *
     *  @param app        The running Application instance.
     *  @param completion Callback fired when the single pathfinding pass
     *      finishes; cleared by `updateComplete()` after invocation.
     *  @param consumer   Resource consumer used to charge for path
     *      complexity.
     *  @param id         Numeric identifier used for log correlation.
     *  @param owner      The `PathRequestManager` that schedules updates.
     *  @param journal    Logging sink.
     */
    PathRequest(
        Application& app,
        std::function<void(void)> const& completion,
        Resource::Consumer& consumer,
        int id,
        PathRequestManager&,
        beast::Journal journal);

    ~PathRequest() override;

    /** Returns true if this request has never completed a pathfinding pass.
     *
     *  Used by `PathRequestManager::updateAll` to prioritise new requests
     *  over ones that already have a cached result.  Thread-safe; acquires
     *  `indexLock_`.
     *
     *  @return `true` until the first successful `doUpdate` records a ledger
     *      index in `lastIndex_`.
     */
    bool
    isNew();

    /** Atomically claims this request for processing by the calling thread.
     *
     *  Returns `true` and sets `inProgress_` if all of the following hold:
     *  no other thread is already processing this request; the `newOnly`
     *  filter does not exclude it; and `index` is strictly newer than
     *  `lastIndex_`.  The caller must invoke `updateComplete()` when
     *  finished to release the claim, even if `doUpdate` fails.
     *
     *  @param newOnly If `true`, only requests that have never been
     *      processed (i.e. `isNew()`) are eligible.
     *  @param index   Ledger sequence number being processed; the request
     *      is skipped if `lastIndex_` is already >= this value.
     *  @return `true` if the caller now owns the update slot; `false` if
     *      another thread is already processing or the ledger index is
     *      not new enough.
     */
    bool
    needsUpdate(bool newOnly, LedgerIndex index);

    /** Releases the in-progress claim and fires the one-shot completion
     *  callback.
     *
     *  Must be called by whichever thread received `true` from
     *  `needsUpdate()`.  For one-shot (`ripple_path_find`) requests, fires
     *  `fCompletion_` then clears it so the callback is invoked at most once.
     */
    void
    updateComplete();

    /** Initialises the request and, for subscription mode, runs a fast first
     *  pass.
     *
     *  Chains `parseJson` → `isValid`.  For subscription-mode requests a
     *  fast preliminary `doUpdate` is run immediately at `PATH_SEARCH_FAST`
     *  depth so the client receives an initial result in the same
     *  request/response cycle.  One-shot requests skip this fast pass and
     *  will be run once in full by `PathRequestManager`.
     *
     *  Errors from parsing or validation are embedded in `jvStatus_`.
     *  Callers should only enqueue the request when the returned flag is
     *  `true`.
     *
     *  @param cache  Ledger snapshot to validate against and, for
     *      subscription mode, to run the fast pass on.
     *  @param value  Raw JSON from the client's `path_find create` or
     *      `ripple_path_find` command.
     *  @return A pair of `{validity flag, current jvStatus_}`.
     */
    std::pair<bool, json::Value>
    doCreate(std::shared_ptr<AssetCache> const&, json::Value const&);

    /** Marks this request as closed and returns the final status snapshot.
     *
     *  Called by `PathRequestManager` when the subscriber disconnects or
     *  the subscription is explicitly cancelled.  Injects `"closed": true`
     *  into `jvStatus_` so the client can detect closure in buffered
     *  responses.
     *
     *  @return The final `jvStatus_` with `"closed": true` injected.
     */
    json::Value
    doClose() override;

    /** Returns the current cached status with `"status": "success"` injected.
     *
     *  Used by the `path_find status` sub-command to let a client poll the
     *  last computed result without triggering a new search.  Thread-safe;
     *  acquires `lock_`.
     *
     *  @param  Ignored; present for interface compatibility.
     *  @return A copy of `jvStatus_` with the success flag set.
     */
    json::Value
    doStatus(json::Value const&) override;

    /** Logs an early-abort event at `info` level.
     *
     *  Invoked by `PathRequestManager` when the subscriber has already gone
     *  away mid-update and the pathfinding work is being discarded before
     *  completion.
     */
    void
    doAborting() const;

    /** Runs one pathfinding pass, updates `jvStatus_`, and reports latency.
     *
     *  Validates the request against `cache` first; returns the current error
     *  status immediately if the request is no longer valid.  Adapts `iLevel_`
     *  (search depth) based on server load and whether the previous pass
     *  succeeded, then delegates to `findPaths`.
     *
     *  On the first fast pass, records `quick_reply_` and calls
     *  `owner_.reportFast`.  On the first full pass, records `full_reply_`
     *  and calls `owner_.reportFull` for metrics collection.
     *
     *  @param cache            Ledger snapshot for this update cycle.
     *  @param fast             `true` for a preliminary shallow search at
     *      `PATH_SEARCH_FAST` depth; `false` for the normal adaptive search.
     *  @param continueCallback Optional abort predicate forwarded to
     *      `findPaths`; pathfinding stops early if it returns `false` (e.g.
     *      when a new ledger closes and the current search is already stale).
     *  @return The newly computed status JSON object, also stored in
     *      `jvStatus_` under `lock_`.
     */
    json::Value
    doUpdate(
        std::shared_ptr<AssetCache> const&,
        bool fast,
        std::function<bool(void)> const& continueCallback = {});

    /** Attempts to lock the subscriber weak pointer.
     *
     *  Returns a null `shared_ptr` when the subscriber has been destroyed
     *  by the network layer.  Callers use a null return as the signal to
     *  abort background work and remove the request from
     *  `PathRequestManager`.
     *
     *  @return The locked subscriber, or `nullptr` if the subscription is
     *      gone.
     */
    InfoSub::pointer
    getSubscriber() const;

    /** Returns true while a one-shot completion callback is still pending.
     *
     *  Used throughout the implementation to branch between subscription
     *  push behaviour and the legacy one-shot callback path.  Returns
     *  `false` after `updateComplete()` has fired and cleared `fCompletion_`.
     *
     *  @return `true` if `fCompletion_` is non-empty.
     */
    bool
    hasCompletion();

private:
    /** Validates request parameters against live ledger state in `crCache`.
     *
     *  Verifies the source account exists and that the destination can
     *  receive the requested asset.  Populates
     *  `jvStatus_[destination_currencies]` from the destination account's
     *  current trust lines as a convenience for the caller.  Must be called
     *  with `lock_` held; re-invoked at the start of each `doUpdate` because
     *  ledger state can change between updates.
     *
     *  @param crCache Snapshot of the current ledger state.
     *  @return `true` if consistent with current ledger state; `false` with
     *      `jvStatus_` set to the appropriate RPC error otherwise.
     */
    bool
    isValid(std::shared_ptr<AssetCache> const& crCache);

    /** Returns (or constructs and ranks) a `Pathfinder` for a source asset.
     *
     *  Looks up `currency` in `currencyMap`; on a miss, constructs a fresh
     *  `Pathfinder`, runs `findPaths` at `level`, then `computePathRanks`
     *  limited to `kMAX_PATHS` (4).  If `findPaths` fails the entry is stored
     *  as `nullptr` so callers distinguish "not yet tried" from "tried and
     *  found nothing".  `currencyMap` is local to a single `findPaths` call
     *  so `Pathfinder` objects are never reused across ledger updates.
     *
     *  @param cache            Ledger snapshot for this update cycle.
     *  @param currencyMap      Per-call cache of already-constructed
     *      pathfinders; modified in place.
     *  @param currency         Source asset to find paths from.
     *  @param dstAmount        Effective destination amount (after
     *      `convert_all_` conversion).
     *  @param level            Search depth passed to `Pathfinder::findPaths`.
     *  @param continueCallback Abort predicate; pathfinding stops if it
     *      returns `false`.
     *  @return Reference to the (possibly null) `unique_ptr` stored in
     *      `currencyMap` for `currency`.
     */
    std::unique_ptr<Pathfinder> const&
    getPathFinder(
        std::shared_ptr<AssetCache> const&,
        hash_map<PathAsset, std::unique_ptr<Pathfinder>>&,
        PathAsset const&,
        STAmount const&,
        int const,
        std::function<bool(void)> const&);

    /** Finds and sets a PathSet in the JSON argument.
     *
     *  Enumerates viable payment paths for each candidate source asset and
     *  appends `RippleCalc`-estimated results to `jvArray`.  Source assets
     *  come from: explicit `sciSourceAssets_` (from `parseJson`); the asset
     *  of `saSendMax_` when no explicit set was provided; or automatic
     *  enumeration of the source account's holdings capped at
     *  `RPC::Tuning::kMAX_AUTO_SRC_CUR` (88).
     *
     *  If `RippleCalc` returns `terNO_LINE` or `tecPATH_PARTIAL` and
     *  `Pathfinder` produced a `fullLiquidityPath`, that path is appended
     *  and `rippleCalculate` is retried.  This two-shot fallback is skipped
     *  in `convert_all_` mode where partial payments are already allowed.
     *
     *  Resource cost: `clamp(size² + 34, 50, 400)` where `size` is the
     *  number of source assets evaluated.  The quadratic term reflects that
     *  path complexity grows super-linearly with source currencies.
     *
     *  @param cache            Ledger snapshot for this update cycle.
     *  @param level            Pathfinder search depth.
     *  @param jvArray          Output JSON array; successful path alternatives
     *      are appended.
     *  @param continueCallback Abort predicate; work stops early if it
     *      returns `false`.
     *  @return `false` if the source currencies are invalid; `true` otherwise
     *      (resource charge is applied regardless of whether paths were found).
     */
    bool
    findPaths(
        std::shared_ptr<AssetCache> const&,
        int const,
        json::Value&,
        std::function<bool(void)> const&);

    /** Parses and stores the client-supplied JSON request parameters.
     *
     *  Validates the presence and format of `source_account`,
     *  `destination_account`, and `destination_amount`.  Handles optional
     *  fields: `send_max` (requires `destination_amount == -1`),
     *  `source_currencies` (array of currency/issuer pairs or
     *  `mpt_issuance_id` hex strings, capped at `kMAX_SRC_CUR`), `domain`
     *  (256-bit hex restricting pathfinding to a permissioned domain), and
     *  `id` (opaque echo value forwarded unchanged to every update response).
     *
     *  @param jvParams Raw JSON from the client.
     *  @return `PFR_PJ_NOCHANGE` (0) on success; `PFR_PJ_INVALID` (-1) on
     *      any parse error with `jvStatus_` set to the relevant RPC error
     *      code.
     */
    int
    parseJson(json::Value const&);

    Application& app_;
    beast::Journal journal_;

    /** Serialises access to `jvStatus_`.  Separate from `indexLock_` so
     *  `doUpdate` does not hold this lock during the expensive `findPaths`
     *  computation. */
    std::recursive_mutex lock_;

    PathRequestManager& owner_;

    /** Weak reference to the WebSocket subscriber; null for one-shot
     *  requests.  Held weakly so subscriber destruction does not stall
     *  background pathfinding threads. */
    std::weak_ptr<InfoSub> wpSubscriber_;

    /** One-shot completion callback for `ripple_path_find` mode; empty for
     *  subscription-mode requests and after `updateComplete()` fires it. */
    std::function<void(void)> fCompletion_;

    /** Resource consumer charged per update cycle; quadratic in source
     *  currency count. */
    Resource::Consumer& consumer_;

    /** Opaque client-supplied `id` echoed back in every update response. */
    json::Value jvId_;

    /** Most recently computed result; protected by `lock_`. */
    json::Value jvStatus_;

    // --- Client request parameters (set once by parseJson) ---

    std::optional<AccountID> raSrcAccount_;
    std::optional<AccountID> raDstAccount_;
    STAmount saDstAmount_;
    std::optional<STAmount> saSendMax_;

    /** Explicit source assets from the client's `source_currencies` field;
     *  empty when auto-discovery via `accountSourceAssets` is used. */
    std::set<Asset> sciSourceAssets_;

    /** Best `STPathSet` per source asset from the previous update; used as
     *  seed paths by `getBestPaths` on the next cycle. */
    std::map<Asset, STPathSet> context_;

    /** Optional permissioned-domain filter for pathfinding; `nullopt` for
     *  unrestricted searches. */
    std::optional<uint256> domain_;

    /** `true` when `destination_amount` is the "convert all" sentinel,
     *  enabling partial-payment mode in `RippleCalc`. */
    bool convert_all_{};

    /** Serialises `lastIndex_` and `inProgress_`; separate from `lock_` so
     *  scheduler state checks never block on long pathfinding work. */
    std::recursive_mutex indexLock_;

    /** Ledger sequence number of the last completed update; 0 while the
     *  request is new. */
    LedgerIndex lastIndex_;

    /** `true` while a thread owns the update slot for this request. */
    bool inProgress_;

    /** Adaptive search depth; increments toward `PATH_SEARCH_MAX` when idle
     *  and the last search failed, decrements under load or after success. */
    int iLevel_;

    /** `true` if the last `findPaths` call produced at least one alternative;
     *  used to drive `iLevel_` adaptation. */
    bool bLastSuccess_;

    /** Numeric identifier for log-line correlation; set at construction. */
    int const iIdentifier_;

    /** Wall-clock instant this request was constructed; used to compute
     *  latency metrics reported to `PathRequestManager`. */
    std::chrono::steady_clock::time_point const created_;

    /** Wall-clock instant of the first fast (`PATH_SEARCH_FAST`) reply;
     *  zero-initialised until the fast pass completes. */
    std::chrono::steady_clock::time_point quick_reply_;

    /** Wall-clock instant of the first full reply; zero-initialised until
     *  the full pass completes. */
    std::chrono::steady_clock::time_point full_reply_;

    /** Maximum number of alternative paths returned per source currency.
     *  Balances practical usefulness against response payload size and the
     *  cost of `computePathRanks`. */
    static unsigned int const kMAX_PATHS = 4;
};

}  // namespace xrpl
