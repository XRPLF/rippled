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

#include <ripple/basics/log/LogPartition.h>
#include <ripple/nodestore/Database.h>
#include <beast/threads/Thread.h>
#include <chrono>
#include <condition_variable>
#include <set>
#include <thread>

namespace ripple {
namespace NodeStore {

class DatabaseImp
    : public Database
    , public beast::LeakChecked <DatabaseImp>
{
public:
    beast::Journal m_journal;
    Scheduler& m_scheduler;
    // Persistent key/value storage.
    std::unique_ptr <Backend> m_backend;
    // Larger key/value storage, but not necessarily persistent.
    std::unique_ptr <Backend> m_fastBackend;

    // Positive cache
    TaggedCache <uint256, NodeObject> m_cache;

    // Negative cache
    KeyCache <uint256> m_negCache;

    std::mutex                m_readLock;
    std::condition_variable   m_readCondVar;
    std::condition_variable   m_readGenCondVar;
    std::set <uint256>        m_readSet;        // set of reads to do
    uint256                   m_readLast;       // last hash read
    std::vector <std::thread> m_readThreads;
    bool                      m_readShut;
    uint64_t                  m_readGen;        // current read generation

    DatabaseImp (std::string const& name,
                 Scheduler& scheduler,
                 int readThreads,
                 std::unique_ptr <Backend> backend,
                 std::unique_ptr <Backend> fastBackend,
                 beast::Journal journal)
        : m_journal (journal)
        , m_scheduler (scheduler)
        , m_backend (std::move (backend))
        , m_fastBackend (std::move (fastBackend))
        , m_cache ("NodeStore", cacheTargetSize, cacheTargetSeconds,
            get_seconds_clock (), LogPartition::getJournal <TaggedCacheLog> ())
        , m_negCache ("NodeStore", get_seconds_clock (),
            cacheTargetSize, cacheTargetSeconds)
        , m_readShut (false)
        , m_readGen (0)
    {
        for (int i = 0; i < readThreads; ++i)
            m_readThreads.push_back (std::thread (&DatabaseImp::threadEntry, this));
    }

    ~DatabaseImp ()
    {
        {
            std::unique_lock <std::mutex> lock (m_readLock);
            m_readShut = true;
            m_readCondVar.notify_all ();
            m_readGenCondVar.notify_all ();
        }

        for (auto& e : m_readThreads)
            e.join();
    }

    beast::String getName () const
    {
        return m_backend->getName ();
    }

    //------------------------------------------------------------------------------

    bool asyncFetch (uint256 const& hash, NodeObject::pointer& object)
    {
        // See if the object is in cache
        object = m_cache.fetch (hash);
        if (object || m_negCache.touch_if_exists (hash))
            return true;

        {
            // No. Post a read
            std::unique_lock <std::mutex> lock (m_readLock);
            if (m_readSet.insert (hash).second)
                m_readCondVar.notify_one ();
        }

        return false;
    }

    void waitReads ()
    {
        {
            std::unique_lock <std::mutex> lock (m_readLock);

            // Wake in two generations
            std::uint64_t const wakeGeneration = m_readGen + 2;

            while (!m_readShut && !m_readSet.empty () && (m_readGen < wakeGeneration))
                m_readGenCondVar.wait (lock);
        }

    }

    int getDesiredAsyncReadCount ()
    {
        // We prefer a client not fill our cache
        // We don't want to push data out of the cache
        // before it's retrieved
        return m_cache.getTargetSize() / asyncDivider;
    }

    NodeObject::Ptr fetch (uint256 const& hash) override
    {
        return doTimedFetch (hash, false);
    }

    /** Perform a fetch and report the time it took */
    NodeObject::Ptr doTimedFetch (uint256 const& hash, bool isAsync)
    {
        FetchReport report;
        report.isAsync = isAsync;
        report.wentToDisk = false;

        auto const before = std::chrono::steady_clock::now();
        NodeObject::Ptr ret = doFetch (hash, report);
        report.elapsed = std::chrono::duration_cast <std::chrono::milliseconds>
            (std::chrono::steady_clock::now() - before);

        report.wasFound = (ret != nullptr);
        m_scheduler.onFetch (report);

        return ret;
    }

    NodeObject::Ptr doFetch (uint256 const& hash, FetchReport &report)
    {
        // See if the object already exists in the cache
        //
        NodeObject::Ptr obj = m_cache.fetch (hash);

        if (obj != nullptr)
            return obj;

        if (m_negCache.touch_if_exists (hash))
            return obj;

        // Check the database(s).

        bool foundInFastBackend = false;
        report.wentToDisk = true;

        // Check the fast backend database if we have one
        //
        if (m_fastBackend != nullptr)
        {
            obj = fetchInternal (*m_fastBackend, hash);

            // If we found the object, avoid storing it again later.
            if (obj != nullptr)
                foundInFastBackend = true;
        }

        // Are we still without an object?
        //
        if (obj == nullptr)
        {
            // Yes so at last we will try the main database.
            //
            obj = fetchInternal (*m_backend, hash);
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

            if (! foundInFastBackend)
            {
                // If we have a fast back end, store it there for later.
                //
                if (m_fastBackend != nullptr)
                    m_fastBackend->store (obj);

                // Since this was a 'hard' fetch, we will log it.
                //
                if (m_journal.trace) m_journal.trace <<
                    "HOS: " << hash << " fetch: in db";
            }
        }

        return obj;
    }

    NodeObject::Ptr fetchInternal (Backend& backend,
        uint256 const& hash)
    {
        NodeObject::Ptr object;

        Status const status = backend.fetch (hash.begin (), &object);

        switch (status)
        {
        case ok:
        case notFound:
            break;

        case dataCorrupt:
            // VFALCO TODO Deal with encountering corrupt data!
            //
            if (m_journal.fatal) m_journal.fatal <<
                "Corrupt NodeObject #" << hash;
            break;

        default:
            if (m_journal.warning) m_journal.warning <<
                "Unknown status=" << status;
            break;
        }

        return object;
    }

    //------------------------------------------------------------------------------

    void store (NodeObjectType type,
                std::uint32_t index,
                Blob&& data,
                uint256 const& hash)
    {
        NodeObject::Ptr object = NodeObject::createObject(type, index, std::move(data), hash);

        #if RIPPLE_VERIFY_NODEOBJECT_KEYS
        assert (hash == Serializer::getSHA512Half (data));
        #endif

        m_cache.canonicalize (hash, object, true);

        m_backend->store (object);

        m_negCache.erase (hash);

        if (m_fastBackend)
            m_fastBackend->store (object);
    }

    //------------------------------------------------------------------------------

    float getCacheHitRate ()
    {
        return m_cache.getHitRate ();
    }

    void tune (int size, int age)
    {
        m_cache.setTargetSize (size);
        m_cache.setTargetAge (age);
        m_negCache.setTargetSize (size);
        m_negCache.setTargetAge (age);
    }

    void sweep ()
    {
        m_cache.sweep ();
        m_negCache.sweep ();
    }

    int getWriteLoad ()
    {
        return m_backend->getWriteLoad ();
    }

    //------------------------------------------------------------------------------

    // Entry point for async read threads
    void threadEntry ()
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

    void for_each (std::function <void(NodeObject::Ptr)> f)
    {
        m_backend->for_each (f);
    }

    void import (Database& source)
    {
        Batch b;
        b.reserve (batchWritePreallocationSize);

        source.for_each ([&](NodeObject::Ptr object)
        {
            if (b.size () >= batchWritePreallocationSize)
            {
                this->m_backend->storeBatch (b);
                b.clear ();
                b.reserve (batchWritePreallocationSize);
            }

            b.push_back (object);
        });

        if (! b.empty ())
            m_backend->storeBatch (b);
    }
};

}
}

#endif
