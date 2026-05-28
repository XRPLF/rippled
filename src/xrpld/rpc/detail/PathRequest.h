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

    ~PathRequest() override;

    bool
    isNew();
    bool
    needsUpdate(bool newOnly, LedgerIndex index);

    // Called when the PathRequest update is complete.
    void
    updateComplete();

    std::pair<bool, json::Value>
    doCreate(std::shared_ptr<AssetCache> const&, json::Value const&);

    json::Value
    doClose() override;
    json::Value
    doStatus(json::Value const&) override;
    void
    doAborting() const;

    // update jvStatus
    json::Value
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
        json::Value&,
        std::function<bool(void)> const&);

    int
    parseJson(json::Value const&);

    Application& app_;
    beast::Journal journal_;

    std::recursive_mutex lock_;

    PathRequestManager& owner_;

    std::weak_ptr<InfoSub> wpSubscriber_;  // Who this request came from
    std::function<void(void)> fCompletion_;
    Resource::Consumer& consumer_;  // Charge according to source currencies

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

    std::recursive_mutex indexLock_;
    LedgerIndex lastIndex_;
    bool inProgress_;

    int iLevel_;
    bool bLastSuccess_;

    int const iIdentifier_;

    std::chrono::steady_clock::time_point const created_;
    std::chrono::steady_clock::time_point quickReply_;
    std::chrono::steady_clock::time_point fullReply_;

    static unsigned int const kMaxPaths = 4;
};

}  // namespace xrpl
