//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef RIPPLE_NODESTORE_DATABASEROTATINGIMP_H_INCLUDED
#define RIPPLE_NODESTORE_DATABASEROTATINGIMP_H_INCLUDED

#include <ripple/nodestore/DatabaseRotating.h>

namespace ripple {
namespace NodeStore {

class DatabaseRotatingImp : public DatabaseRotating
{
public:
    DatabaseRotatingImp() = delete;
    DatabaseRotatingImp(DatabaseRotatingImp const&) = delete;
    DatabaseRotatingImp&
    operator=(DatabaseRotatingImp const&) = delete;

    DatabaseRotatingImp(
        std::string const& name,
        Scheduler& scheduler,
        int readThreads,
        Stoppable& parent,
        std::shared_ptr<Backend> writableBackend,
        std::shared_ptr<Backend> archiveBackend,
        Section const& config,
        beast::Journal j);

    ~DatabaseRotatingImp() override
    {
        // Stop threads before data members are destroyed.
        stopThreads();
    }

    void
    rotateWithLock(
        std::function<std::unique_ptr<NodeStore::Backend>(
            std::string const& writableBackendName)> const& f) override;

    std::string
    getName() const override;

    std::int32_t
    getWriteLoad() const override;

    void
    import(Database& source) override;

    void
    store(
        NodeObjectType type,
        Blob&& data,
        uint256 const& hash,
        std::uint32_t seq) override;

    std::shared_ptr<NodeObject>
    fetch(uint256 const& hash, std::uint32_t seq) override
    {
        return doFetch(hash, seq, *pCache_, *nCache_, false);
    }

    bool
    asyncFetch(
        uint256 const& hash,
        std::uint32_t seq,
        std::shared_ptr<NodeObject>& object) override;

    bool
    storeLedger(std::shared_ptr<Ledger const> const& srcLedger) override;

    int
    getDesiredAsyncReadCount(std::uint32_t seq) override
    {
        // We prefer a client not fill our cache
        // We don't want to push data out of the cache
        // before it's retrieved
        return pCache_->getTargetSize() / asyncDivider;
    }

    float
    getCacheHitRate() override
    {
        return pCache_->getHitRate();
    }

    void
    tune(int size, std::chrono::seconds age) override;

    void
    sweep() override;

    TaggedCache<uint256, NodeObject> const&
    getPositiveCache() override
    {
        return *pCache_;
    }

private:
    // Positive cache
    std::shared_ptr<TaggedCache<uint256, NodeObject>> pCache_;

    // Negative cache
    std::shared_ptr<KeyCache<uint256>> nCache_;

    std::shared_ptr<Backend> writableBackend_;
    std::shared_ptr<Backend> archiveBackend_;
    mutable std::mutex mutex_;

    std::shared_ptr<NodeObject>
    fetchFrom(uint256 const& hash, std::uint32_t seq) override;

    void
    for_each(std::function<void(std::shared_ptr<NodeObject>)> f) override;
};

}  // namespace NodeStore
}  // namespace ripple

#endif
