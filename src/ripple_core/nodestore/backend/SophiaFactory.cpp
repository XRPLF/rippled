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

#if RIPPLE_SOPHIA_AVAILABLE

namespace NodeStore
{

class SophiaFactory::BackendImp
    : public Backend
    , public BatchWriter::Callback
    , public LeakChecked <SophiaFactory::BackendImp>
{
public:
    typedef RecycledObjectPool <std::string> StringPool;

    //--------------------------------------------------------------------------

    BackendImp (int keyBytes,
             Parameters const& keyValues,
             Scheduler& scheduler)
        : m_keyBytes (keyBytes)
        , m_scheduler (scheduler)
        , m_batch (*this, scheduler)
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

    ~BackendImp ()
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
        m_batch.store (object);;
    }

    void storeBatch (Batch const& batch)
    {
        EncodedBlob::Pool::ScopedItem item (m_blobPool);

        for (Batch::const_iterator iter (batch.begin());
            iter != batch.end(); ++iter)
        {
            EncodedBlob& encoded (item.getObject ());
            encoded.prepare (*iter);

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
    }

    void visitAll (VisitCallback& callback)
    {
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
    StringPool m_stringPool;
    EncodedBlob::Pool m_blobPool;
    std::string m_name;
    void* m_env;
    void* m_db;
};

//------------------------------------------------------------------------------

SophiaFactory::SophiaFactory ()
{
    leveldb::Options options;
    options.create_if_missing = true;
    options.block_cache = leveldb::NewLRUCache (getConfig ().getSize (
        siHashNodeDBCache) * 1024 * 1024);
}

SophiaFactory::~SophiaFactory ()
{
}

SophiaFactory* SophiaFactory::getInstance ()
{
    return new SophiaFactory;
}

String SophiaFactory::getName () const
{
    return "sophia";
}

Backend* SophiaFactory::createInstance (
    size_t keyBytes,
    Parameters const& keyValues,
    Scheduler& scheduler)
{
    return new SophiaFactory::BackendImp (keyBytes, keyValues, scheduler);
}

}

#endif
