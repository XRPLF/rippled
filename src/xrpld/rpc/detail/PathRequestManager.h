#pragma once

#include <xrpld/app/main/Application.h>
#include <xrpld/rpc/detail/AssetCache.h>
#include <xrpld/rpc/detail/PathRequest.h>

#include <atomic>
#include <mutex>
#include <vector>

namespace xrpl {

class PathRequestManager
{
public:
    /** A collection of all PathRequest instances. */
    PathRequestManager(
        Application& app,
        beast::Journal journal,
        beast::insight::Collector::ptr const& collector)
        : app_(app), journal_(journal), lastIdentifier_(0)
    {
        fast_ = collector->makeEvent("pathfind_fast");
        full_ = collector->makeEvent("pathfind_full");
    }

    /** Update all of the contained PathRequest instances.

        @param ledger Ledger we are pathfinding in.
     */
    void
    updateAll(std::shared_ptr<ReadView const> const& ledger);

    bool
    requestsPending() const;

    std::shared_ptr<AssetCache>
    getAssetCache(std::shared_ptr<ReadView const> const& ledger, bool authoritative);

    // Create a new-style path request that pushes
    // updates to a subscriber
    json::Value
    makePathRequest(
        std::shared_ptr<InfoSub> const& subscriber,
        std::shared_ptr<ReadView const> const& ledger,
        json::Value const& request);

    // Create an old-style path request that is
    // managed by a coroutine and updated by
    // the path engine
    json::Value
    makeLegacyPathRequest(
        PathRequest::pointer& req,
        std::function<void(void)> completion,
        Resource::Consumer& consumer,
        std::shared_ptr<ReadView const> const& inLedger,
        json::Value const& request);

    // Execute an old-style path request immediately
    // with the ledger specified by the caller
    json::Value
    doLegacyPathRequest(
        Resource::Consumer& consumer,
        std::shared_ptr<ReadView const> const& inLedger,
        json::Value const& request);

    void
    reportFast(std::chrono::milliseconds ms)
    {
        fast_.notify(ms);
    }

    void
    reportFull(std::chrono::milliseconds ms)
    {
        full_.notify(ms);
    }

private:
    void
    insertPathRequest(PathRequest::pointer const&);

    Application& app_;
    beast::Journal journal_;

    beast::insight::Event fast_;
    beast::insight::Event full_;

    // Track all requests
    std::vector<PathRequest::wptr> requests_;

    // Use a AssetCache
    std::weak_ptr<AssetCache> assetCache_;

    std::atomic<int> lastIdentifier_;

    std::recursive_mutex mutable lock_;
};

}  // namespace xrpl
