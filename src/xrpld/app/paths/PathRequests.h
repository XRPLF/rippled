#ifndef XRPL_APP_PATHS_PATHREQUESTS_H_INCLUDED
#define XRPL_APP_PATHS_PATHREQUESTS_H_INCLUDED

#include <xrpld/app/main/Application.h>
#include <xrpld/app/paths/PathRequest.h>
#include <xrpld/app/paths/RippleLineCache.h>

#include <atomic>
#include <mutex>
#include <vector>

namespace ripple {

class PathRequests
{
public:
    /** A collection of all PathRequest instances. */
    PathRequests(
        Application& app,
        beast::Journal journal,
        beast::insight::Collector::ptr const& collector)
        : app_(app), mJournal(journal), mLastIdentifier(0)
    {
        mFast = collector->make_event("pathfind_fast");
        mFull = collector->make_event("pathfind_full");
    }

    /** Update all of the contained PathRequest instances.

        @param ledger Ledger we are pathfinding in.
     */
    void
    updateAll(std::shared_ptr<ReadView const> const& ledger);

    bool
    requestsPending() const;

    std::shared_ptr<RippleLineCache>
    getLineCache(
        std::shared_ptr<ReadView const> const& ledger,
        bool authoritative);

    // Create a new-style path request that pushes
    // updates to a subscriber
    Json::Value
    makePathRequest(
        std::shared_ptr<InfoSub> const& subscriber,
        std::shared_ptr<ReadView const> const& ledger,
        Json::Value const& request);

    // Create an old-style path request that is
    // managed by a coroutine and updated by
    // the path engine
    Json::Value
    makeLegacyPathRequest(
        PathRequest::pointer& req,
        std::function<void(void)> completion,
        Resource::Consumer& consumer,
        std::shared_ptr<ReadView const> const& inLedger,
        Json::Value const& request);

    // Execute an old-style path request immediately
    // with the ledger specified by the caller
    Json::Value
    doLegacyPathRequest(
        Resource::Consumer& consumer,
        std::shared_ptr<ReadView const> const& inLedger,
        Json::Value const& request);

    void
    reportFast(std::chrono::milliseconds ms)
    {
        mFast.notify(ms);
    }

    void
    reportFull(std::chrono::milliseconds ms)
    {
        mFull.notify(ms);
    }

private:
    void
    insertPathRequest(PathRequest::pointer const&);

    Application& app_;
    beast::Journal mJournal;

    beast::insight::Event mFast;
    beast::insight::Event mFull;

    // Track all requests
    std::vector<PathRequest::wptr> requests_;

    // Use a RippleLineCache
    std::weak_ptr<RippleLineCache> lineCache_;

    std::atomic<int> mLastIdentifier;

    std::recursive_mutex mutable mLock;
};

}  // namespace ripple

#endif
