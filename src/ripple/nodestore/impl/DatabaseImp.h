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

#ifndef RIPPLE_NODESTORE_DATABASEIMP_H_INCLUDED
#define RIPPLE_NODESTORE_DATABASEIMP_H_INCLUDED

#include <ripple/nodestore/Database.h>
#include <ripple/nodestore/Scheduler.h>
#include <ripple/nodestore/impl/Tuning.h>
#include <ripple/basics/KeyCache.h>
#include <ripple/basics/Log.h>
#include <ripple/basics/chrono.h>
#include <ripple/core/ThreadEntry.h>
#include <ripple/protocol/digest.h>
#include <ripple/basics/Slice.h>
#include <ripple/basics/TaggedCache.h>
#include <ripple/beast/core/Thread.h>
#include <chrono>
#include <condition_variable>
#include <set>
#include <thread>

namespace ripple {
namespace NodeStore {

class DatabaseImp
    : public Database
{
private:
    beast::Journal m_journal;
    Scheduler& m_scheduler;
    // Persistent key/value storage.
    std::unique_ptr <Backend> m_backend;
protected:
    // Positive cache
    TaggedCache <uint256, NodeObject> m_cache;

    // Negative cache
    KeyCache <uint256> m_negCache;
private:
    std::mutex                m_readLock;
    std::condition_variable   m_readCondVar;
    std::condition_variable   m_readGenCondVar;
    std::set <uint256>        m_readSet;        // set of reads to do
    uint256                   m_readLast;       // last hash read
    std::vector <std::thread> m_readThreads;
    bool                      m_readShut;
    uint64_t                  m_readGen;        // current read generation
    int                       fdlimit_;

public:
    DatabaseImp (std::string const& name,
                 Scheduler& scheduler,
                 int readThreads,
                 std::unique_ptr <Backend> backend,
                 beast::Journal journal)
        : m_journal (journal)
        , m_scheduler (scheduler)
        , m_backend (std::move (backend))
        , m_cache ("NodeStore", cacheTargetSize, cacheTargetSeconds,
            stopwatch(), journal)
        , m_negCache ("NodeStore", stopwatch(),
            cacheTargetSize, cacheTargetSeconds)
        , m_readShut (false)
        , m_readGen (0)
        , fdlimit_ (0)
        , m_storeCount (0)
        , m_fetchTotalCount (0)
        , m_fetchHitCount (0)
        , m_storeSize (0)
        , m_fetchSize (0)
    {
        for (int i = 0; i < readThreads; ++i)
            m_readThreads.emplace_back (&DatabaseImp::threadEntry, this);

        if (m_backend)
            fdlimit_ = m_backend->fdlimit();
    }

    ~DatabaseImp ()
    {
        {
            std::lock_guard <std::mutex> lock (m_readLock);
            m_readShut = true;
            m_readCondVar.notify_all ();
            m_readGenCondVar.notify_all ();
        }

        for (auto& e : m_readThreads)
            e.join();
    }

    std::string
    getName () const override
    {
        return m_backend->getName ();
    }

    void
    close() override
    {
        if (m_backend)
        {
            m_backend->close();
            m_backend = nullptr;
        }
    }

    //------------------------------------------------------------------------------

    bool asyncFetch (uint256 const& hash, std::shared_ptr<NodeObject>& object) override
    {
        // See if the object is in cache
        object = m_cache.fetch (hash);
        if (object || m_negCache.touch_if_exists (hash))
            return true;

        {
            // No. Post a read
            std::lock_guard <std::mutex> lock (m_readLock);
            if (m_readSet.insert (hash).second)
                m_readCondVar.notify_one ();
        }

        return false;
    }

    void waitReads() override
    {
        {
            std::unique_lock <std::mutex> lock (m_readLock);

            // Wake in two generations
            std::uint64_t const wakeGeneration = m_readGen + 2;

            while (!m_readShut && !m_readSet.empty () && (m_readGen < wakeGeneration))
                m_readGenCondVar.wait (lock);
        }

    }

    int getDesiredAsyncReadCount () override
    {
        // We prefer a client not fill our cache
        // We don't want to push data out of the cache
        // before it's retrieved
        return m_cache.getTargetSize() / asyncDivider;
    }

    std::shared_ptr<NodeObject> fetch (uint256 const& hash) override
    {
        return doTimedFetch (hash, false);
    }

    /** Perform a fetch and report the time it took */
    std::shared_ptr<NodeObject> doTimedFetch (uint256 const& hash, bool isAsync)
    {
        FetchReport report;
        report.isAsync = isAsync;
        report.wentToDisk = false;

        auto const before = std::chrono::steady_clock::now();
        std::shared_ptr<NodeObject> ret = doFetch (hash, report);
        report.elapsed = std::chrono::duration_cast <std::chrono::milliseconds>
            (std::chrono::steady_clock::now() - before);

        report.wasFound = (ret != nullptr);
        m_scheduler.onFetch (report);

        return ret;
    }

    std::shared_ptr<NodeObject> doFetch (uint256 const& hash, FetchReport &report)
    {
        // See if the object already exists in the cache
        //
        std::shared_ptr<NodeObject> obj = m_cache.fetch (hash);

        if (obj != nullptr)
            return obj;

        if (m_negCache.touch_if_exists (hash))
            return obj;

        // Check the database(s).

        report.wentToDisk = true;

        // Are we still without an object?
        //
        if (obj == nullptr)
        {
            // Yes so at last we will try the main database.
            //
            obj = fetchFrom (hash);
            ++m_fetchTotalCount;
        }

        if (obj == nullptr)
        {

            // Just in case a write occurred
            obj = m_cache.fetch (hash);

            if (obj == nullptr)
            {
                // We give up
                m_negCache.insert (hash);
            }
        }
        else
        {
            // Ensure all threads get the same object
            //
            m_cache.canonicalize (hash, obj);

            // Since this was a 'hard' fetch, we will log it.
            //
            JLOG(m_journal.trace()) <<
                "HOS: " << hash << " fetch: in db";
        }

        return obj;
    }

    virtual std::shared_ptr<NodeObject> fetchFrom (uint256 const& hash)
    {
        return fetchInternal (*m_backend, hash);
    }

    std::shared_ptr<NodeObject> fetchInternal (Backend& backend,
        uint256 const& hash)
    {
        std::shared_ptr<NodeObject> object;

        Status const status = backend.fetch (hash.begin (), &object);

        switch (status)
        {
        case ok:
            ++m_fetchHitCount;
            if (object)
                m_fetchSize += object->getData().size();
        case notFound:
            break;

        case dataCorrupt:
            // VFALCO TODO Deal with encountering corrupt data!
            //
            JLOG(m_journal.fatal()) <<
                "Corrupt NodeObject #" << hash;
            break;

        default:
            JLOG(m_journal.warn()) <<
                "Unknown status=" << status;
            break;
        }

        return object;
    }

    //------------------------------------------------------------------------------

    void store (NodeObjectType type,
                Blob&& data,
                uint256 const& hash) override
    {
        storeInternal (type, std::move(data), hash, *m_backend.get());
    }

    void storeInternal (NodeObjectType type,
                        Blob&& data,
                        uint256 const& hash,
                        Backend& backend)
    {
        #if RIPPLE_VERIFY_NODEOBJECT_KEYS
        assert (hash == sha512Hash(makeSlice(data)));
        #endif

        std::shared_ptr<NodeObject> object = NodeObject::createObject(
            type, std::move(data), hash);

        m_cache.canonicalize (hash, object, true);

        backend.store (object);
        ++m_storeCount;
        if (object)
            m_storeSize += object->getData().size();

        m_negCache.erase (hash);
    }

    //------------------------------------------------------------------------------

    float getCacheHitRate () override
    {
        return m_cache.getHitRate ();
    }

    void tune (int size, int age) override
    {
        m_cache.setTargetSize (size);
        m_cache.setTargetAge (age);
        m_negCache.setTargetSize (size);
        m_negCache.setTargetAge (age);
    }

    void sweep () override
    {
        m_cache.sweep ();
        m_negCache.sweep ();
    }

    std::int32_t getWriteLoad() const override
    {
        return m_backend->getWriteLoad();
    }

    //------------------------------------------------------------------------------

    // Uncaught exception handling for async read threads
    void threadEntry ()
    {
        ::ripple::threadEntry (
            this, &DatabaseImp::threadEntryImpl, "DatabaseImp::threadEntry()");
    }

    // Entry point for async read threads
    void threadEntryImpl ()
    {
        beast::Thread::setCurrentThreadName ("prefetch");
        while (1)
        {
            uint256 hash;

            {
                std::unique_lock <std::mutex> lock (m_readLock);

                while (!m_readShut && m_readSet.empty ())
                {
                    // all work is done
                    m_readGenCondVar.notify_all ();
                    m_readCondVar.wait (lock);
                }

                if (m_readShut)
                    break;

                // Read in key order to make the back end more efficient
                std::set <uint256>::iterator it = m_readSet.lower_bound (m_readLast);
                if (it == m_readSet.end ())
                {
                    it = m_readSet.begin ();

                    // A generation has completed
                    ++m_readGen;
                    m_readGenCondVar.notify_all ();
                }

                hash = *it;
                m_readSet.erase (it);
                m_readLast = hash;
            }

            // Perform the read
            doTimedFetch (hash, true);
         }
     }

    //------------------------------------------------------------------------------

    void for_each (std::function <void(std::shared_ptr<NodeObject>)> f) override
    {
        m_backend->for_each (f);
    }

    void import (Database& source) override
    {
        importInternal (source, *m_backend.get());
    }

    void importInternal (Database& source, Backend& dest)
    {
        Batch b;
        b.reserve (batchWritePreallocationSize);

        source.for_each ([&](std::shared_ptr<NodeObject> object)
        {
            if (b.size() >= batchWritePreallocationSize)
            {
                dest.storeBatch (b);
                b.clear();
                b.reserve (batchWritePreallocationSize);
            }

            b.push_back (object);
            ++m_storeCount;
            if (object)
                m_storeSize += object->getData().size();
        });

        if (! b.empty())
            dest.storeBatch (b);
    }

    std::uint32_t getStoreCount () const override
    {
        return m_storeCount;
    }

    std::uint32_t getFetchTotalCount () const override
    {
        return m_fetchTotalCount;
    }

    std::uint32_t getFetchHitCount () const override
    {
        return m_fetchHitCount;
    }

    std::uint32_t getStoreSize () const override
    {
        return m_storeSize;
    }

    std::uint32_t getFetchSize () const override
    {
        return m_fetchSize;
    }

    int fdlimit() const override
    {
        return fdlimit_;
    }

private:
    std::atomic <std::uint32_t> m_storeCount;
    std::atomic <std::uint32_t> m_fetchTotalCount;
    std::atomic <std::uint32_t> m_fetchHitCount;
    std::atomic <std::uint32_t> m_storeSize;
    std::atomic <std::uint32_t> m_fetchSize;
};

}
}

#endif
