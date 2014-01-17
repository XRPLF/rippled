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

namespace ripple {
namespace NodeStore {

class DatabaseImp
    : public Database
    , public LeakChecked <DatabaseImp>
{
public:
    Journal m_journal;
    Scheduler& m_scheduler;
    // Persistent key/value storage.
    std::unique_ptr <Backend> m_backend;
    // Larger key/value storage, but not necessarily persistent.
    std::unique_ptr <Backend> m_fastBackend;
    TaggedCacheType <uint256, NodeObject> m_cache;

    DatabaseImp (std::string const& name,
                 Scheduler& scheduler,
                 std::unique_ptr <Backend> backend,
                 std::unique_ptr <Backend> fastBackend,
                 Journal journal)
        : m_journal (journal)
        , m_scheduler (scheduler)
        , m_backend (std::move (backend))
        , m_fastBackend (std::move (fastBackend))
        , m_cache ("NodeStore", cacheTargetSize, cacheTargetSeconds,
            get_abstract_clock <std::chrono::steady_clock, std::chrono::seconds> (),
                LogPartition::getJournal <TaggedCacheLog> ())
    {
    }

    ~DatabaseImp ()
    {
    }

    String getName () const
    {
        return m_backend->getName ();
    }

    //------------------------------------------------------------------------------

    NodeObject::Ptr fetch (uint256 const& hash)
    {
        // See if the object already exists in the cache
        //
        NodeObject::Ptr obj = m_cache.fetch (hash);

        if (obj == nullptr)
        {
            // There's still a chance it could be in one of the databases.

            bool foundInFastBackend = false;

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
                {
                    // Monitor this operation's load since it is expensive.
                    //
                    // VFALCO TODO Why is this an autoptr? Why can't it just be a plain old object?
                    //
                    // VFALCO NOTE Commented this out because it breaks the unit test!
                    //
                    //LoadEvent::autoptr event (getApp().getJobQueue ().getLoadEventAP (jtHO_READ, "HOS::retrieve"));

                    obj = fetchInternal (*m_backend, hash);
                }

            }

            // Did we finally get something?
            //
            if (obj != nullptr)
            {
                // Yes it so canonicalize. This solves the problem where
                // more than one thread has its own copy of the same object.
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
                    WriteLog (lsTRACE, NodeObject) << "HOS: " << hash << " fetch: in db";
                }
            }
        }
        else
        {
            // found it!
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
            WriteLog (lsFATAL, NodeObject) << "Corrupt NodeObject #" << hash;
            break;

        default:
            WriteLog (lsWARNING, NodeObject) << "Unknown status=" << status;
            break;
        }

        return object;
    }

    //------------------------------------------------------------------------------

    void store (NodeObjectType type,
                uint32 index,
                Blob& data,
                uint256 const& hash)
    {
        bool const keyFoundAndObjectCached = m_cache.refreshIfPresent (hash);

        // VFALCO NOTE What happens if the key is found, but the object
        //             fell out of the cache? We will end up passing it
        //             to the backend anyway.
        //
        if (! keyFoundAndObjectCached)
        {
        #if RIPPLE_VERIFY_NODEOBJECT_KEYS
            assert (hash == Serializer::getSHA512Half (data));
        #endif

            NodeObject::Ptr object = NodeObject::createObject (
                type, index, data, hash);

            if (!m_cache.canonicalize (hash, object))
            {
                m_backend->store (object);

                if (m_fastBackend)
                    m_fastBackend->store (object);
            }

        }
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
    }

    void sweep ()
    {
        m_cache.sweep ();
    }

    int getWriteLoad ()
    {
        return m_backend->getWriteLoad ();
    }

    //------------------------------------------------------------------------------

    void visitAll (VisitCallback& callback)
    {
        m_backend->visitAll (callback);
    }

    void import (Database& sourceDatabase)
    {
        class ImportVisitCallback : public VisitCallback
        {
        public:
            explicit ImportVisitCallback (Backend& backend)
                : m_backend (backend)
            {
                m_objects.reserve (batchWritePreallocationSize);
            }

            ~ImportVisitCallback ()
            {
                if (! m_objects.empty ())
                    m_backend.storeBatch (m_objects);
            }

            void visitObject (NodeObject::Ptr const& object)
            {
                if (m_objects.size () >= batchWritePreallocationSize)
                {
                    m_backend.storeBatch (m_objects);

                    m_objects.clear ();
                    m_objects.reserve (batchWritePreallocationSize);
                }

                m_objects.push_back (object);
            }

        private:
            Backend& m_backend;
            Batch m_objects;
        };

        //--------------------------------------------------------------------------

        ImportVisitCallback callback (*m_backend);

        sourceDatabase.visitAll (callback);
    }
};

}
}

#endif
