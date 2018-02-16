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
    DatabaseRotatingImp& operator=(DatabaseRotatingImp const&) = delete;

    DatabaseRotatingImp(
        std::string const& name,
        Scheduler& scheduler,
        int readThreads,
        Stoppable& parent,
        std::unique_ptr<Backend> writableBackend,
        std::unique_ptr<Backend> archiveBackend,
        Section const& config,
        beast::Journal j);

    ~DatabaseRotatingImp() override
    {
        // Stop threads before data members are destroyed.
        stopThreads();
    }

    std::unique_ptr<Backend> const&
    getWritableBackend() const override
    {
        std::lock_guard <std::mutex> lock (rotateMutex_);
        return writableBackend_;
    }

    std::unique_ptr<Backend>
    rotateBackends(std::unique_ptr<Backend> newBackend) override;

    std::mutex& peekMutex() const override
    {
        return rotateMutex_;
    }

    std::string getName() const override
    {
        return getWritableBackend()->getName();
    }

    std::int32_t getWriteLoad() const override
    {
        return getWritableBackend()->getWriteLoad();
    }

    void import (Database& source) override
    {
        importInternal (source, *getWritableBackend());
    }

    void store(NodeObjectType type, Blob&& data,
        uint256 const& hash, std::uint32_t seq) override;

    std::shared_ptr<NodeObject>
    fetch(uint256 const& hash, std::uint32_t seq) override
    {
        return doFetch(hash, seq, pCache_, nCache_, false);
    }

    bool
    asyncFetch(uint256 const& hash, std::uint32_t seq,
        std::shared_ptr<NodeObject>& object) override;

    bool
    copyLedger(std::shared_ptr<Ledger const> const& ledger) override;

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

    TaggedCache<uint256, NodeObject> const&
    getPositiveCache() override {return *pCache_;}

private:
    // Positive cache
    std::shared_ptr<TaggedCache<uint256, NodeObject>> pCache_;

    // Negative cache
    std::shared_ptr<KeyCache<uint256>> nCache_;

    std::unique_ptr<Backend> writableBackend_;
    std::unique_ptr<Backend> archiveBackend_;
    mutable std::mutex rotateMutex_;

    struct Backends {
        std::unique_ptr<Backend> const& writableBackend;
        std::unique_ptr<Backend> const& archiveBackend;
    };

    Backends getBackends() const
    {
        std::lock_guard <std::mutex> lock (rotateMutex_);
        return Backends {writableBackend_, archiveBackend_};
    }

    std::shared_ptr<NodeObject> fetchFrom(
        uint256 const& hash, std::uint32_t seq) override;

    void
    for_each(std::function <void(std::shared_ptr<NodeObject>)> f) override
    {
        Backends b = getBackends();
        b.archiveBackend->for_each(f);
        b.writableBackend->for_each(f);
    }
};

}
}

#endif
