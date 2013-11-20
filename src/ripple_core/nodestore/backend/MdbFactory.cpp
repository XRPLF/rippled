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

#if RIPPLE_MDB_AVAILABLE

namespace NodeStore
{

class MdbFactory::BackendImp
    : public Backend
    , public BatchWriter::Callback
    , public LeakChecked <MdbFactory::BackendImp>
{
public:
    explicit BackendImp (size_t keyBytes,
                      Parameters const& keyValues,
                      Scheduler& scheduler)
        : m_keyBytes (keyBytes)
        , m_scheduler (scheduler)
        , m_batch (*this, scheduler)
        , m_env (nullptr)
    {
        String path (keyValues ["path"]);

        if (path.isEmpty ())
            Throw (std::runtime_error ("Missing path in MDB backend"));

        m_basePath = path.toStdString();

        // Regarding the path supplied to mdb_env_open:
        // This directory must already exist and be writable.
        //
        File dir (File::getCurrentWorkingDirectory().getChildFile (path));
        Result result = dir.createDirectory ();

        if (result.wasOk ())
        {
            int error = mdb_env_create (&m_env);

            // Should use the size of the file plus the free space on the disk
            if (error == 0)
                error = mdb_env_set_mapsize (m_env, 512L * 1024L * 1024L * 1024L);

            if (error == 0)
                error = mdb_env_open (
                            m_env,
                            m_basePath.c_str (),
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
        else
        {
            String s;
            s << "MDB Backend failed to create directory, " << result.getErrorMessage ();
            Throw (std::runtime_error (s.toStdString().c_str()));
        }
    }

    ~BackendImp ()
    {
        if (m_env != nullptr)
        {
            mdb_dbi_close (m_env, m_dbi);
            mdb_env_close (m_env);
        }
    }

    std::string getName()
    {
        return m_basePath;
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
    Scheduler& m_scheduler;
    BatchWriter m_batch;
    EncodedBlob::Pool m_blobPool;
    std::string m_basePath;
    MDB_env*    m_env;
    MDB_dbi     m_dbi;
};

//------------------------------------------------------------------------------

MdbFactory::MdbFactory ()
{
}

MdbFactory::~MdbFactory ()
{
}

MdbFactory* MdbFactory::getInstance ()
{
    return new MdbFactory;
}

String MdbFactory::getName () const
{
    return "mdb";
}

Backend* MdbFactory::createInstance (
    size_t keyBytes,
    Parameters const& keyValues,
    Scheduler& scheduler)
{
    return new MdbFactory::BackendImp (keyBytes, keyValues, scheduler);
}

}

#endif
