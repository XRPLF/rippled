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
        std::function<void(void)> const& completion,
        Resource::Consumer& consumer,
        int id,
        PathRequestManager&,
        beast::Journal journal);

    ~PathRequest();

    bool
    isNew();
    bool
    needsUpdate(bool newOnly, LedgerIndex index);

    // Called when the PathRequest update is complete.
    void
    updateComplete();

    std::pair<bool, Json::Value>
    doCreate(std::shared_ptr<AssetCache> const&, Json::Value const&);

    Json::Value
    doClose() override;
    Json::Value
    doStatus(Json::Value const&) override;
    void
    doAborting() const;

    // update jvStatus
    Json::Value
    doUpdate(
        std::shared_ptr<AssetCache> const&,
        bool fast,
        std::function<bool(void)> const& continueCallback = {});
    InfoSub::pointer
    getSubscriber() const;
    bool
    hasCompletion();

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

    /** Finds and sets a PathSet in the JSON argument.
        Returns false if the source currencies are invalid.
    */
    bool
    findPaths(
        std::shared_ptr<AssetCache> const&,
        int const,
        Json::Value&,
        std::function<bool(void)> const&);

    int
    parseJson(Json::Value const&);

    Application& app_;
    beast::Journal m_journal;

    std::recursive_mutex mLock;

    PathRequestManager& mOwner;

    std::weak_ptr<InfoSub> wpSubscriber;  // Who this request came from
    std::function<void(void)> fCompletion;
    Resource::Consumer& consumer_;  // Charge according to source currencies

    Json::Value jvId;
    Json::Value jvStatus;  // Last result

    // Client request parameters
    std::optional<AccountID> raSrcAccount;
    std::optional<AccountID> raDstAccount;
    STAmount saDstAmount;
    std::optional<STAmount> saSendMax;

    std::set<Asset> sciSourceAssets;
    std::map<Asset, STPathSet> mContext;

    std::optional<uint256> domain;

    bool convert_all_{};

    std::recursive_mutex mIndexLock;
    LedgerIndex mLastIndex;
    bool mInProgress;

    int iLevel;
    bool bLastSuccess;

    int const iIdentifier;

    std::chrono::steady_clock::time_point const created_;
    std::chrono::steady_clock::time_point quick_reply_;
    std::chrono::steady_clock::time_point full_reply_;

    static unsigned int const max_paths_ = 4;
};

}  // namespace xrpl
