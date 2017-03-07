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

#include <ripple/nodestore/impl/DatabaseImp.h>
#include <ripple/nodestore/DatabaseRotating.h>

namespace ripple {
namespace NodeStore {

class DatabaseRotatingImp
    : public DatabaseImp
    , public DatabaseRotating
{
private:
    std::shared_ptr <Backend> writableBackend_;
    std::shared_ptr <Backend> archiveBackend_;
    mutable std::mutex rotateMutex_;

    struct Backends {
        std::shared_ptr <Backend> const& writableBackend;
        std::shared_ptr <Backend> const& archiveBackend;
    };

    Backends getBackends() const
    {
        std::lock_guard <std::mutex> lock (rotateMutex_);
        return Backends {writableBackend_, archiveBackend_};
    }

public:
    DatabaseRotatingImp (std::string const& name,
                 Scheduler& scheduler,
                 int readThreads,
                 Stoppable& parent,
                 std::shared_ptr <Backend> writableBackend,
                 std::shared_ptr <Backend> archiveBackend,
                 beast::Journal journal)
            : DatabaseImp (
                name,
                scheduler,
                readThreads,
                parent,
                std::unique_ptr <Backend>(),
                journal)
            , writableBackend_ (writableBackend)
            , archiveBackend_ (archiveBackend)
    {}

    ~DatabaseRotatingImp () override
    {
        // Stop threads before data members are destroyed.
        DatabaseImp::stopThreads ();
    }

    std::shared_ptr <Backend> const& getWritableBackend() const override
    {
        std::lock_guard <std::mutex> lock (rotateMutex_);
        return writableBackend_;
    }

    std::shared_ptr <Backend> const& getArchiveBackend() const override
    {
        std::lock_guard <std::mutex> lock (rotateMutex_);
        return archiveBackend_;
    }

    std::shared_ptr <Backend> rotateBackends (
            std::shared_ptr <Backend> const& newBackend) override;
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

    void for_each (std::function <void(std::shared_ptr<NodeObject>)> f) override
    {
        Backends b = getBackends();
        b.archiveBackend->for_each (f);
        b.writableBackend->for_each (f);
    }

    void import (Database& source) override
    {
        importInternal (source, *getWritableBackend());
    }

    void store (NodeObjectType type,
                Blob&& data,
                uint256 const& hash) override
    {
        storeInternal (type, std::move(data), hash,
                *getWritableBackend());
    }

    std::shared_ptr<NodeObject> fetchNode (uint256 const& hash) override
    {
        return fetchFrom (hash);
    }

    std::shared_ptr<NodeObject> fetchFrom (uint256 const& hash) override;
    TaggedCache <uint256, NodeObject>& getPositiveCache() override
    {
        return m_cache;
    }
};

}
}

#endif
