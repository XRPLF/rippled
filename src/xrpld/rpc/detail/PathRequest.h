#pragma once

#include <xrpld/app/main/Application.h>
#include <xrpld/rpc/detail/AssetCache.h>
#include <xrpld/rpc/detail/Pathfinder.h>
#include <xrpld/rpc/detail/Tuning.h>

#include <xrpl/basics/CountedObject.h>
#include <xrpl/basics/UnorderedContainers.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/PathAsset.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STPathSet.h>
#include <xrpl/resource/Consumer.h>
#include <xrpl/server/InfoSub.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <utility>

namespace xrpl {

// A pathfinding request submitted by a client
// The request issuer must maintain a strong pointer

class AssetCache;
class PathRequestManager;

// Return values from parseJson <0 = invalid, >0 = valid
#define PFR_PJ_INVALID (-1)
#define PFR_PJ_NOCHANGE 0

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
    // path_find semantics
    // Subscriber is updated
    PathRequest(
        Application& app,
        std::shared_ptr<InfoSub> const& subscriber,
        int id,
        PathRequestManager&,
        beast::Journal journal);

    // ripple_path_find semantics
    // Completion function is called after path update is complete
    PathRequest(
        Application& app,
        std::function<void(void)> completion,
        resource::Consumer& consumer,
        int id,
        PathRequestManager&,
        beast::Journal journal);

    ~PathRequest() override;

    bool
    isNew() const;
    bool
    needsUpdate(bool newOnly, LedgerIndex index);

    /**
     * Finish a claimed update slot.
     * @param pinSeq If set, record lastIndex_ so same-seq reprocess is skipped
     *        (typically closed ledgers only).
     * @param completedWork If false, only clear inProgress_ (abandoned claim /
     *        drop without finishing). If true, clear isNew() even when pinSeq
     *        is nullopt (open first-update: allow same-seq closed wave).
     */
    void
    updateComplete(std::optional<LedgerIndex> pinSeq = std::nullopt, bool completedWork = false);

    std::pair<bool, json::Value>
    doCreate(std::shared_ptr<AssetCache> const&, json::Value const&);

    json::Value
    doClose() override;
    json::Value
    doStatus(json::Value const&) override;
    void
    doAborting() const;

    // update jvStatus
    /**
     * @param revalidateOnly When true (mid-close / periodic refresh only), only
     *        re-run rippleCalculate on known paths. Never starts Pathfinder and
     *        never escalates a failed revalidate into a full graph search.
     *        Closed-ledger waves pass false so rediscovery / failed recovery work.
     * @param calcLedger Optional ledger for PaymentSandbox (open mid-close).
     *        When null, uses cache->getLedger(). Line vectors still come from cache.
     */
    json::Value
    doUpdate(
        std::shared_ptr<AssetCache> const&,
        bool fast,
        std::function<bool(void)> const& continueCallback = {},
        bool revalidateOnly = false,
        std::shared_ptr<ReadView const> const& calcLedger = {});
    InfoSub::pointer
    getSubscriber() const;
    bool
    hasCompletion();

    /**
     * Called from ~PathRequestManager before the manager is destroyed so
     * subsequent ~PathRequest does not call into a freed owner.
     */
    void
    detachFromManager() noexcept;

    /**
     * Unique id for AssetCache session pins / release.
     */
    [[nodiscard]] int
    id() const
    {
        return iIdentifier_;
    }

private:
    bool
    isValid(std::shared_ptr<AssetCache> const& crCache);

    std::unique_ptr<Pathfinder> const&
    getPathFinder(
        std::shared_ptr<AssetCache> const&,
        hash_map<PathAsset, std::unique_ptr<Pathfinder>>&,
        PathAsset const&,
        STAmount const&,
        int const,
        std::function<bool(void)> const&);

    /**
     * Finds and sets a PathSet in the JSON argument.
     * Returns false if the source currencies are invalid.
     *
     * @param fullSearch If false and context_ has prior paths, only re-run
     *        rippleCalculate on those paths (skip Pathfinder graph search).
     * @param allowEscalate If false, a failed revalidate does NOT fall through
     *        to Pathfinder (used for mid-close ticks).
     * @param didFullSearch Set true if Pathfinder ran (vs pure revalidate).
     * @param calcLedger Ledger for PaymentSandbox; null → cache->getLedger().
     */
    bool
    findPaths(
        std::shared_ptr<AssetCache> const&,
        int const,
        json::Value&,
        std::function<bool(void)> const&,
        bool fullSearch,
        bool allowEscalate,
        bool& didFullSearch,
        std::shared_ptr<ReadView const> const& calcLedger = {});

    /**
     * Re-estimate liquidity for an existing path set on calcLedger (or cache).
     * Returns true if rippleCalculate succeeded (tesSUCCESS).
     */
    bool
    revalidatePaths(
        std::shared_ptr<AssetCache> const& cache,
        Asset const& asset,
        STPathSet const& paths,
        STAmount const& dstAmount,
        json::Value& jvArray,
        std::shared_ptr<ReadView const> const& calcLedger = {});

    int
    parseJson(json::Value const&);

    Application& app_;
    beast::Journal journal_;

    std::recursive_mutex lock_;

    // Nullable so ~PathRequestManager can detach live sessions before destroy
    // (WS InfoSub may outlive the manager briefly during Application teardown).
    // Atomic: detachFromManager races with ~PathRequest / reportFast / doClose
    // (manager dtor or force-drop vs WS teardown on another thread).
    std::atomic<PathRequestManager*> owner_;

    std::weak_ptr<InfoSub> wpSubscriber_;  // Who this request came from
    std::function<void(void)> fCompletion_;
    resource::Consumer& consumer_;  // Charge according to source currencies

    json::Value jvId_;
    json::Value jvStatus_;  // Last result

    // Client request parameters
    std::optional<AccountID> raSrcAccount_;
    std::optional<AccountID> raDstAccount_;
    STAmount saDstAmount_;
    std::optional<STAmount> saSendMax_;

    std::set<Asset> sciSourceAssets_;
    std::map<Asset, STPathSet> context_;

    std::optional<uint256> domain_;

    bool convertAll_{};

    /**
     * Set when WS auto source-currency soft cap truncates the account set.
     */
    bool sourceCurrenciesTruncated_{false};

    mutable std::recursive_mutex indexLock_;
    /**
     * After a completed update that pins a ledger seq, needsUpdate skips the
     * same (or older) ledger. 0 until the first closed-ledger pin (open first
     * updates set firstUpdateDone_ without pinning so same-seq closed still runs).
     */
    LedgerIndex lastIndex_;
    /**
     * True after any finished doUpdate (open or closed). isNew() is the inverse.
     * Distinct from lastIndex_ so open first-update can leave isNew false without
     * suppressing the subsequent closed wave at the same sequence.
     */
    bool firstUpdateDone_{false};
    bool inProgress_;

    int iLevel_;
    bool bLastSuccess_;

    /**
     * Ledger index of the last *non-fast* Pathfinder search (0 = never completed
     * a non-fast full search). Fast doCreate must not stamp this, or the first
     * updateAll never runs Pathfinder at pathSearch depth.
     */
    LedgerIndex lastFullSearchIndex_;

    /**
     * AssetCache::lineEpoch() observed at the last Pathfinder-driven update.
     * When the shared cache loads more trust-line chunks, this lags and the
     * next non-revalidate update escalates to Pathfinder.
     */
    std::uint64_t lastLineEpoch_{0};

    int const iIdentifier_;

    std::chrono::steady_clock::time_point const created_;
    std::chrono::steady_clock::time_point quickReply_;
    std::chrono::steady_clock::time_point fullReply_;

    static unsigned int const kMaxPaths = rpc::tuning::kPathFindMaxPaths;
};

}  // namespace xrpl
