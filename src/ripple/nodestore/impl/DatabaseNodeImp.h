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

#include <ripple/basics/chrono.h>
#include <ripple/nodestore/Database.h>

namespace ripple {
namespace NodeStore {

class DatabaseNodeImp : public Database
{
public:
    DatabaseNodeImp() = delete;
    DatabaseNodeImp(DatabaseNodeImp const&) = delete;
    DatabaseNodeImp&
    operator=(DatabaseNodeImp const&) = delete;

    DatabaseNodeImp(
        Scheduler& scheduler,
        int readThreads,
        std::shared_ptr<Backend> backend,
        Section const& config,
        beast::Journal j)
        : Database(scheduler, readThreads, config, j)
        , backend_(std::move(backend))
    {
        std::optional<int> cacheSize, cacheAge;
        if (config.exists("cache_size"))
        {
            cacheSize = get<int>(config, "cache_size");
            if (cacheSize.value() < 0)
            {
                Throw<std::runtime_error>(
                    "Specified negative value for cache_size");
            }
        }
        if (config.exists("cache_age"))
        {
            cacheAge = get<int>(config, "cache_age");
            if (cacheAge.value() < 0)
            {
                Throw<std::runtime_error>(
                    "Specified negative value for cache_age");
            }
        }
        if (cacheSize || cacheAge)
        {
            if (!cacheSize || *cacheSize == 0)
                cacheSize = 16384;
            if (!cacheAge || *cacheAge == 0)
                cacheAge = 5;
            cache_ = std::make_shared<TaggedCache<uint256, NodeObject>>(
                "DatabaseNodeImp",
                cacheSize.value(),
                std::chrono::minutes{cacheAge.value()},
                stopwatch(),
                j);
        }
        assert(backend_);
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
    store(NodeObjectType type, Blob&& data, uint256 const& hash, std::uint32_t)
        override;

    bool isSameDB(std::uint32_t, std::uint32_t) override
    {
        // only one database
        return true;
    }
    void
    sync() override
    {
        backend_->sync();
    }

    std::vector<std::shared_ptr<NodeObject>>
    fetchBatch(std::vector<uint256> const& hashes);

    bool
    storeLedger(std::shared_ptr<Ledger const> const& srcLedger) override
    {
        return Database::storeLedger(*srcLedger, backend_);
    }

    void
    sweep() override;

    Backend&
    getBackend() override
    {
        return *backend_;
    };

private:
    // Cache for database objects. This cache is not always initialized. Check
    // for null before using.
    std::shared_ptr<TaggedCache<uint256, NodeObject>> cache_;
    // Persistent key/value storage
    std::shared_ptr<Backend> backend_;

    std::shared_ptr<NodeObject>
    fetchNodeObject(
        uint256 const& hash,
        std::uint32_t,
        FetchReport& fetchReport) override;

    void
    for_each(std::function<void(std::shared_ptr<NodeObject>)> f) override
    {
        backend_->for_each(f);
    }
};

}  // namespace NodeStore
}  // namespace ripple

#endif
