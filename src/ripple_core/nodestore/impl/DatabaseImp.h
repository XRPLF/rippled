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

namespace NodeStore
{

class DatabaseImp
    : public Database
    , LeakChecked <DatabaseImp>
{
public:
    DatabaseImp (char const* name,
                 Scheduler& scheduler,
                 Parameters const& backendParameters,
                 Parameters const& fastBackendParameters)
        : m_scheduler (scheduler)
        , m_backend (createBackend (backendParameters, scheduler))
        , m_fastBackend ((fastBackendParameters.size () > 0)
            ? createBackend (fastBackendParameters, scheduler) : nullptr)
        , m_cache ("NodeStore", 16384, 300)
        , m_negativeCache ("NoteStoreNegativeCache", 0, 120)
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
            // It's not in the cache, see if we can skip checking the db.
            //
            if (! m_negativeCache.isPresent (hash))
            {
                // There's still a chance it could be in one of the databases.

                bool foundInFastBackend = false;

                // Check the fast backend database if we have one
                //
                if (m_fastBackend != nullptr)
                {
                    obj = fetchInternal (m_fastBackend, hash);

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

                        obj = fetchInternal (m_backend, hash);
                    }

                    // If it's not in the main database, remember that so we
                    // can skip the lookup for the same object again later.
                    //
                    if (obj == nullptr)
                        m_negativeCache.add (hash);
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
                // hash is known not to be in the database
            }
        }
        else
        {
            // found it!
        }

        return obj;
    }

    NodeObject::Ptr fetchInternal (Backend* backend, uint256 const& hash)
    {
        NodeObject::Ptr object;

        Status const status = backend->fetch (hash.begin (), &object);

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

            m_negativeCache.del (hash);
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
        m_negativeCache.sweep ();
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

    //------------------------------------------------------------------------------

    static void missing_backend ()
    {
        fatal_error ("Your rippled.cfg is missing a [node_db] entry, please see the rippled-example.cfg file!");
    }

    static Backend* createBackend (Parameters const& parameters, Scheduler& scheduler)
    {
        Backend* backend = nullptr;

        String const& type = parameters ["type"];

        if (type.isNotEmpty ())
        {
            Factory* factory (Factories::get().find (type));

            if (factory != nullptr)
            {
                backend = factory->createInstance (NodeObject::keyBytes, parameters, scheduler);
            }
            else
            {
                missing_backend ();
            }
        }
        else
        {
            missing_backend ();
        }

        return backend;
    }

    //------------------------------------------------------------------------------

private:
    Scheduler& m_scheduler;

    // Persistent key/value storage.
    ScopedPointer <Backend> m_backend;

    // Larger key/value storage, but not necessarily persistent.
    ScopedPointer <Backend> m_fastBackend;

    // VFALCO NOTE What are these things for? We need comments.
    TaggedCacheType <uint256, NodeObject, UptimeTimerAdapter> m_cache;
    KeyCache <uint256, UptimeTimerAdapter> m_negativeCache;
};

//------------------------------------------------------------------------------

void Database::addFactory (Factory* factory)
{
    Factories::get().add (factory);
}

void Database::addAvailableBackends ()
{
    // This is part of the ripple_app module since it has dependencies
    //addFactory (SqliteFactory::getInstance ());

    addFactory (LevelDBFactory::getInstance ());

    addFactory (MemoryFactory::getInstance ());
    addFactory (NullFactory::getInstance ());

#if RIPPLE_HYPERLEVELDB_AVAILABLE
    addFactory (HyperDBFactory::getInstance ());
#endif

#if RIPPLE_MDB_AVAILABLE
    addFactory (MdbFactory::getInstance ());
#endif

#if RIPPLE_SOPHIA_AVAILABLE
    addFactory (SophiaFactory::getInstance ());
#endif

    addFactory (KeyvaDBFactory::getInstance ());
}

//------------------------------------------------------------------------------

Database* Database::New (char const* name,
                         Scheduler& scheduler,
                         Parameters const& backendParameters,
                         Parameters fastBackendParameters)
{
    return new DatabaseImp (name,
        scheduler, backendParameters, fastBackendParameters);
}

}
