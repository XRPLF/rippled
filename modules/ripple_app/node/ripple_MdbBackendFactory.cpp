//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#if RIPPLE_MDB_AVAILABLE

class MdbBackendFactory::Backend
    : public NodeStore::Backend
    , public NodeStore::BatchWriter::Callback
    , LeakChecked <MdbBackendFactory::Backend>
{
public:
    typedef NodeStore::Batch Batch;
    typedef NodeStore::EncodedBlob EncodedBlob;
    typedef NodeStore::DecodedBlob DecodedBlob;

    explicit Backend (size_t keyBytes,
                      StringPairArray const& keyValues,
                      NodeStore::Scheduler& scheduler)
        : m_keyBytes (keyBytes)
        , m_scheduler (scheduler)
        , m_batch (*this, scheduler)
        , m_env (nullptr)
    {
        String path (keyValues ["path"]);

        m_name = path.toStdString();

        if (path.isEmpty ())
            Throw (std::runtime_error ("Missing path in MDB backend"));

        int error = 0;

        error = mdb_env_create (&m_env);

        if (error == 0) // Should use the size of the file plus the free space on the disk
            error = mdb_env_set_mapsize (m_env, 512L * 1024L * 1024L * 1024L);

        if (error == 0)
            error = mdb_env_open (
                        m_env,
                        m_name.c_str (),
                        MDB_NOTLS,
                        0664);

        MDB_txn* txn;

        if (error == 0)
            error = mdb_txn_begin (m_env, NULL, 0, &txn);

        if (error == 0)
            error = mdb_dbi_open (txn, NULL, 0, &m_dbi);

        if (error == 0)
            error = mdb_txn_commit (txn);

        if (error != 0)
        {
            String s;
            s << "Error #" << error << " creating mdb environment";
            Throw (std::runtime_error (s.toStdString ()));
        }
    }

    ~Backend ()
    {
        if (m_env != nullptr)
        {
            mdb_dbi_close (m_env, m_dbi);
            mdb_env_close (m_env);
        }
    }

    std::string getName()
    {
        return m_name;
    }

    //--------------------------------------------------------------------------

    template <class T>
    unsigned char* mdb_cast (T* p)
    {
        return const_cast <unsigned char*> (static_cast <unsigned char const*> (p));
    }

    Status fetch (void const* key, NodeObject::Ptr* pObject)
    {
        pObject->reset ();

        Status status (ok);

        MDB_txn* txn = nullptr;

        int error = 0;

        error = mdb_txn_begin (m_env, NULL, MDB_RDONLY, &txn);

        if (error == 0)
        {
            MDB_val dbkey;
            MDB_val data;

            dbkey.mv_size = m_keyBytes;
            dbkey.mv_data = mdb_cast (key);

            error = mdb_get (txn, m_dbi, &dbkey, &data);

            if (error == 0)
            {
                DecodedBlob decoded (key, data.mv_data, data.mv_size);

                if (decoded.wasOk ())
                {
                    *pObject = decoded.createObject ();
                }
                else
                {
                    status = dataCorrupt;
                }
            }
            else if (error == MDB_NOTFOUND)
            {
                status = notFound;
            }
            else
            {
                status = unknown;

                WriteLog (lsWARNING, NodeObject) << "MDB txn failed, code=" << error;
            }

            mdb_txn_abort (txn);
        }
        else
        {
            status = unknown;

            WriteLog (lsWARNING, NodeObject) << "MDB txn failed, code=" << error;
        }

        return status;
    }

    void store (NodeObject::ref object)
    {
        m_batch.store (object);
    }

    void storeBatch (Batch const& batch)
    {
        MDB_txn* txn = nullptr;

        int error = 0;

        error = mdb_txn_begin (m_env, NULL, 0, &txn);

        if (error == 0)
        {
            EncodedBlob::Pool::ScopedItem item (m_blobPool);

            BOOST_FOREACH (NodeObject::Ptr const& object, batch)
            {
                EncodedBlob& encoded (item.getObject ());

                encoded.prepare (object);

                MDB_val key;
                key.mv_size = m_keyBytes;
                key.mv_data = mdb_cast (encoded.getKey ());

                MDB_val data;
                data.mv_size = encoded.getSize ();
                data.mv_data = mdb_cast (encoded.getData ());

                error = mdb_put (txn, m_dbi, &key, &data, 0);

                if (error != 0)
                {
                    WriteLog (lsWARNING, NodeObject) << "mdb_put failed, error=" << error;
                    break;
                }
            }

            if (error == 0)
            {
                error = mdb_txn_commit(txn);

                if (error != 0)
                {
                    WriteLog (lsWARNING, NodeObject) << "mdb_txn_commit failed, error=" << error;
                }
            }
            else
            {
                mdb_txn_abort (txn);
            }
        }
        else
        {
            WriteLog (lsWARNING, NodeObject) << "mdb_txn_begin failed, error=" << error;
        }
    }

    void visitAll (VisitCallback& callback)
    {
        // VFALCO TODO Implement this!
        bassertfalse;
    }

    int getWriteLoad ()
    {
        return m_batch.getWriteLoad ();
    }

    //--------------------------------------------------------------------------

    void writeBatch (Batch const& batch)
    {
        storeBatch (batch);
    }

private:
    size_t const m_keyBytes;
    NodeStore::Scheduler& m_scheduler;
    NodeStore::BatchWriter m_batch;
    NodeStore::EncodedBlob::Pool m_blobPool;
    std::string m_name;
    MDB_env*    m_env;
    MDB_dbi     m_dbi;
};

//------------------------------------------------------------------------------

MdbBackendFactory::MdbBackendFactory ()
{
}

MdbBackendFactory::~MdbBackendFactory ()
{
}

MdbBackendFactory& MdbBackendFactory::getInstance ()
{
    static MdbBackendFactory instance;

    return instance;
}

String MdbBackendFactory::getName () const
{
    return "mdb";
}

NodeStore::Backend* MdbBackendFactory::createInstance (
    size_t keyBytes,
    StringPairArray const& keyValues,
    NodeStore::Scheduler& scheduler)
{
    return new MdbBackendFactory::Backend (keyBytes, keyValues, scheduler);
}

#endif
