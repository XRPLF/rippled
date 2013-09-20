//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

class SophiaBackendFactory::Backend
    : public NodeStore::Backend
    , LeakChecked <SophiaBackendFactory::Backend>
{
public:
    typedef RecycledObjectPool <std::string> StringPool;
    typedef NodeStore::Batch Batch;
    typedef NodeStore::EncodedBlob EncodedBlob;
    typedef NodeStore::DecodedBlob DecodedBlob;

    //--------------------------------------------------------------------------

    Backend (int keyBytes,
             StringPairArray const& keyValues,
             NodeStore::Scheduler& scheduler)
        : m_keyBytes (keyBytes)
        , m_scheduler (scheduler)
        , m_name (keyValues ["path"].toStdString ())
        , m_env (nullptr)
        , m_db (nullptr)
    {
        if (m_name.empty())
            Throw (std::runtime_error ("Missing path in Sophia backend"));

        m_env = sp_env ();

        if (m_env != nullptr)
        {
            sp_ctl (m_env, SPDIR, SPO_RDWR | SPO_CREAT, m_name.c_str());
            m_db = sp_open (m_env);
        }
    }

    ~Backend ()
    {
        if (m_db != nullptr)
            sp_destroy (m_db);

        if (m_env != nullptr)
            sp_destroy (m_env);
    }

    std::string getName()
    {
        return m_name;
    }

    //--------------------------------------------------------------------------

    Status fetch (void const* key, NodeObject::Ptr* pObject)
    {
        pObject->reset ();

        Status status (unknown);

        void* v (nullptr);
        std::size_t vsize;

        int rc (sp_get (m_db, key, m_keyBytes, &v, &vsize));

        if (rc == 1)
        {
            DecodedBlob decoded (key, v, vsize);

            if (decoded.wasOk ())
            {
                *pObject = decoded.createObject ();
                status = ok;
            }
            else
            {
                status = dataCorrupt;
            }

            ::free (v);
        }
        else if (rc == 0)
        {
            status = notFound;
        }
        else
        {
            String s;
            s << "Sophia failed with error code " << rc;
            Throw (std::runtime_error (s.toStdString()), __FILE__, __LINE__);
            status = notFound;
        }

        return status;
    }

    void store (NodeObject::ref object)
    {
        EncodedBlob::Pool::ScopedItem item (m_blobPool);
        EncodedBlob& encoded (item.getObject ());
        encoded.prepare (object);

        int rv (sp_set (m_db,
            encoded.getKey(), m_keyBytes,
                encoded.getData(), encoded.getSize()));

        if (rv != 0)
        {
            String s;
            s << "Sophia failed with error code " << rv;
            Throw (std::runtime_error (s.toStdString()), __FILE__, __LINE__);
        }
    }

    void storeBatch (Batch const& batch)
    {
        for (NodeStore::Batch::const_iterator iter (batch.begin());
            iter != batch.end(); ++iter)
        {
            store (*iter);
        }
    }

    void visitAll (VisitCallback& callback)
    {
    }

    int getWriteLoad ()
    {
        return 0;
    }

    void stopAsync ()
    {
        m_scheduler.scheduledTasksStopped ();
    }

private:
    size_t const m_keyBytes;
    NodeStore::Scheduler& m_scheduler;
    StringPool m_stringPool;
    NodeStore::EncodedBlob::Pool m_blobPool;
    std::string m_name;
    void* m_env;
    void* m_db;
};

//------------------------------------------------------------------------------

SophiaBackendFactory::SophiaBackendFactory ()
{
    leveldb::Options options;
    options.create_if_missing = true;
    options.block_cache = leveldb::NewLRUCache (getConfig ().getSize (
        siHashNodeDBCache) * 1024 * 1024);
}

SophiaBackendFactory::~SophiaBackendFactory ()
{
}

SophiaBackendFactory* SophiaBackendFactory::getInstance ()
{
    return new SophiaBackendFactory;
}

String SophiaBackendFactory::getName () const
{
    return "sophia";
}

NodeStore::Backend* SophiaBackendFactory::createInstance (
    size_t keyBytes,
    StringPairArray const& keyValues,
    NodeStore::Scheduler& scheduler)
{
    return new SophiaBackendFactory::Backend (keyBytes, keyValues, scheduler);
}

//------------------------------------------------------------------------------

