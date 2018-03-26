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

#ifndef RIPPLE_NODESTORE_DATABASENODEIMP_H_INCLUDED
#define RIPPLE_NODESTORE_DATABASENODEIMP_H_INCLUDED

#include <ripple/nodestore/Database.h>
#include <ripple/basics/chrono.h>

namespace ripple {
namespace NodeStore {

class DatabaseNodeImp : public Database
{
public:
    DatabaseNodeImp() = delete;
    DatabaseNodeImp(DatabaseNodeImp const&) = delete;
    DatabaseNodeImp& operator=(DatabaseNodeImp const&) = delete;

    DatabaseNodeImp(
        std::string const& name,
        Scheduler& scheduler,
        int readThreads,
        Stoppable& parent,
        std::unique_ptr<Backend> backend,
        Section const& config,
        beast::Journal j)
        : Database(name, parent, scheduler, readThreads, config, j)
        , pCache_(std::make_shared<TaggedCache<uint256, NodeObject>>(
            name, cacheTargetSize, cacheTargetSeconds, stopwatch(), j))
        , nCache_(std::make_shared<KeyCache<uint256>>(
            name, stopwatch(), cacheTargetSize, cacheTargetSeconds))
        , backend_(std::move(backend))
    {
        assert(backend_);
    }

    ~DatabaseNodeImp() override
    {
        // Stop threads before data members are destroyed.
        stopThreads();
    }

    std::string
    getName() const override
    {
        return backend_->getName();
    }

    std::int32_t
    getWriteLoad() const override
    {
        return backend_->getWriteLoad();
    }

    void
    import(Database& source) override
    {
        importInternal(*backend_.get(), source);
    }

    void
    store(NodeObjectType type, Blob&& data,
        uint256 const& hash, std::uint32_t seq) override;

    std::shared_ptr<NodeObject>
    fetch(uint256 const& hash, std::uint32_t seq) override
    {
        return doFetch(hash, seq, *pCache_, *nCache_, false);
    }

    bool
    asyncFetch(uint256 const& hash, std::uint32_t seq,
        std::shared_ptr<NodeObject>& object) override;

    bool
    copyLedger(std::shared_ptr<Ledger const> const& ledger) override
    {
        return Database::copyLedger(
            *backend_, *ledger, pCache_, nCache_, nullptr);
    }

    int
    getDesiredAsyncReadCount(std::uint32_t seq) override
    {
        // We prefer a client not fill our cache
        // We don't want to push data out of the cache
        // before it's retrieved
        return pCache_->getTargetSize() / asyncDivider;
    }

    float
    getCacheHitRate() override {return pCache_->getHitRate();}

    void
    tune(int size, int age) override;

    void
    sweep() override;

private:
    // Positive cache
    std::shared_ptr<TaggedCache<uint256, NodeObject>> pCache_;

    // Negative cache
    std::shared_ptr<KeyCache<uint256>> nCache_;

    // Persistent key/value storage
    std::unique_ptr<Backend> backend_;

    std::shared_ptr<NodeObject>
    fetchFrom(uint256 const& hash, std::uint32_t seq) override
    {
        return fetchInternal(hash, *backend_);
    }

    void
    for_each(std::function<void(std::shared_ptr<NodeObject>)> f) override
    {
        backend_->for_each(f);
    }
};

}
}

#endif
