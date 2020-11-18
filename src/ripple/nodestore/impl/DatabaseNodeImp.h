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
        std::string const& name,
        Scheduler& scheduler,
        int readThreads,
        Stoppable& parent,
        std::shared_ptr<Backend> backend,
        Section const& config,
        beast::Journal j)
        : Database(name, parent, scheduler, readThreads, config, j)
        , backend_(std::move(backend))
    {
        assert(backend_);
        setParent(parent);
    }

    ~DatabaseNodeImp() override
    {
        // Stop read threads in base before data members are destroyed
        stopReadThreads();
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

    bool
    asyncFetch(
        uint256 const& hash,
        std::uint32_t ledgerSeq,
        std::shared_ptr<NodeObject>& nodeObject,
        std::function<void(
            std::shared_ptr<NodeObject>&)>&& callback) override;

    bool
    storeLedger(std::shared_ptr<Ledger const> const& srcLedger) override
    {
        return Database::storeLedger(*srcLedger, backend_);
    }

    void
    sweep() override;

private:
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
