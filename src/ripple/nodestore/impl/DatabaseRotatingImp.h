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
        std::shared_ptr <Backend> writableBackend;
        std::shared_ptr <Backend> archiveBackend;
    };

    Backends getBackends() const
    {
        Backends b;
        {
            std::lock_guard <std::mutex> l (rotateMutex_);
            b.writableBackend = writableBackend_;
            b.archiveBackend = archiveBackend_;
        }

        return b;
    }

public:
    DatabaseRotatingImp (std::string const& name,
                 Scheduler& scheduler,
                 int readThreads,
                 std::shared_ptr <Backend> writableBackend,
                 std::shared_ptr <Backend> archiveBackend,
                 std::unique_ptr <Backend> fastBackend,
                 beast::Journal journal)
            : DatabaseImp (name, scheduler, readThreads,
                    std::unique_ptr <Backend>(), std::move (fastBackend),
                    journal)
            , writableBackend_ (writableBackend)
            , archiveBackend_ (archiveBackend)
    {}

    std::shared_ptr <Backend> getWritableBackend (
            bool unlocked=false) const override
    {
        if (unlocked)
            return writableBackend_;

        std::lock_guard <std::mutex> l (rotateMutex_);
        return writableBackend_;
    }

    std::shared_ptr <Backend> getArchiveBackend (
            bool unlocked=false) const override
    {
        if (unlocked)
        {
            return archiveBackend_;
        }
        else
        {
            std::lock_guard <std::mutex> l (rotateMutex_);
            return archiveBackend_;
        }
    }

    // make sure to call it already locked!
    std::shared_ptr <Backend> rotateBackends (
            std::shared_ptr <Backend> newBackend) override
    {
        std::shared_ptr <Backend> oldBackend = archiveBackend_;
        archiveBackend_ = writableBackend_;
        writableBackend_ = newBackend;

        return oldBackend;
    }

    std::unique_lock <std::mutex> getRotateLock() const override
    {
        return std::unique_lock <std::mutex>(rotateMutex_);
    }

    std::string getName() const override
    {
        return getWritableBackend()->getName();
    }

    std::int32_t getWriteLoad() const override
    {
        return getWritableBackend()->getWriteLoad();
    }

    void for_each (std::function <void(NodeObject::Ptr)> f) override
    {
        Backends b = getBackends();
        b.archiveBackend->for_each (f);
        b.writableBackend->for_each (f);
    }

    void import (Database& source) override
    {
        importInternal (source, *getWritableBackend().get());
    }

    void store (NodeObjectType type,
                std::uint32_t index,
                Blob&& data,
                uint256 const& hash) override
    {
        storeInternal (type, index, std::move(data), hash,
                *getWritableBackend().get());
    }

    NodeObject::Ptr fetchNode (uint256 const& hash) override
    {
        return fetchFrom (hash);
    }

    NodeObject::Ptr fetchFrom (uint256 const& hash) override
    {
        Backends b = getBackends();
        NodeObject::Ptr object = fetchInternal (*b.writableBackend.get(), hash);
        if (!object)
        {
            object = fetchInternal (*b.archiveBackend.get(), hash);
            if (object)
            {
                getWritableBackend()->store (object);
                m_negCache.erase (hash);
            }
        }

        return object;
    }

    TaggedCache <uint256, NodeObject>& getPositiveCache() override
    {
        return m_cache;
    }
};

}
}

#endif
