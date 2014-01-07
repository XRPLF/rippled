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

#if RIPPLE_ROCKSDB_AVAILABLE

namespace NodeStore {

//------------------------------------------------------------------------------

class RocksDBEnv : public rocksdb::EnvWrapper
{
public:
    static RocksDBEnv* get ()
    {
        static RocksDBEnv instance;
        return &instance;
    }

    RocksDBEnv ()
        : EnvWrapper (rocksdb::Env::Default())
    {
    }

    struct ThreadParams
    {
        ThreadParams (void (*f_)(void*), void* a_)
            : f (f_)
            , a (a_)
        {
        }

        void (*f)(void*);
        void* a;
    };

    static void thread_entry (void* ptr)
    {
        ThreadParams* const p (reinterpret_cast <ThreadParams*> (ptr));
        void (*f)(void*) = p->f;
        void* a (p->a);
        delete p;

        static Atomic <int> n;
        int const id (++n);
        std::stringstream ss;
        ss << "rocksdb #" << id;
        Thread::setCurrentThreadName (ss.str());

        (*f)(a);
    }

    void StartThread(void (*f)(void*), void* a)
    {
        ThreadParams* const p (new ThreadParams (f, a));
        EnvWrapper::StartThread (&RocksDBEnv::thread_entry, p);
    }
};

//------------------------------------------------------------------------------

class RocksDBFactory::BackendImp
    : public Backend
    , public BatchWriter::Callback
    , public LeakChecked <RocksDBFactory::BackendImp>
{
public:
    typedef RecycledObjectPool <std::string> StringPool;

    //--------------------------------------------------------------------------

    BackendImp (int keyBytes,
             Parameters const& keyValues,
             Scheduler& scheduler,
             Journal journal)
        : m_journal (journal)
        , m_keyBytes (keyBytes)
        , m_scheduler (scheduler)
        , m_batch (*this, scheduler)
        , m_name (keyValues ["path"].toStdString ())
    {
        if (m_name.empty())
            Throw (std::runtime_error ("Missing path in RocksDBFactory backend"));

        rocksdb::Options options;
        options.create_if_missing = true;

        if (keyValues["cache_mb"].isEmpty())
        {
            options.block_cache = rocksdb::NewLRUCache (getConfig ().getSize (siHashNodeDBCache) * 1024 * 1024);
        }
        else
        {
            options.block_cache = rocksdb::NewLRUCache (keyValues["cache_mb"].getIntValue() * 1024L * 1024L);
        }

        if (keyValues["filter_bits"].isEmpty())
        {
            if (getConfig ().NODE_SIZE >= 2)
                options.filter_policy = rocksdb::NewBloomFilterPolicy (10);
        }
        else if (keyValues["filter_bits"].getIntValue() != 0)
        {
            options.filter_policy = rocksdb::NewBloomFilterPolicy (keyValues["filter_bits"].getIntValue());
        }

        if (! keyValues["open_files"].isEmpty())
        {
            options.max_open_files = keyValues["open_files"].getIntValue();
        }

        if (! keyValues["file_size_mb"].isEmpty())
        {
            options.target_file_size_base = 1024 * 1024 * keyValues["file_size_mb"].getIntValue();
            options.max_bytes_for_level_base = 5 * options.target_file_size_base;
            options.write_buffer_size = 2 * options.target_file_size_base;
        }

        if (! keyValues["file_size_mult"].isEmpty())
        {
            options.target_file_size_multiplier = keyValues["file_size_mult"].getIntValue();
        }

        options.env = RocksDBEnv::get();

        rocksdb::DB* db = nullptr;
        rocksdb::Status status = rocksdb::DB::Open (options, m_name, &db);
        if (!status.ok () || !db)
            Throw (std::runtime_error (std::string("Unable to open/create RocksDB: ") + status.ToString()));

        m_db = db;
    }

    ~BackendImp ()
    {
    }

    std::string getName()
    {
        return m_name;
    }

    //--------------------------------------------------------------------------

    Status fetch (void const* key, NodeObject::Ptr* pObject)
    {
        pObject->reset ();

        Status status (ok);

        rocksdb::ReadOptions const options;
        rocksdb::Slice const slice (static_cast <char const*> (key), m_keyBytes);

        {
            // These are reused std::string objects,
            // required for RocksDB's funky interface.
            //
            StringPool::ScopedItem item (m_stringPool);
            std::string& string = item.getObject ();

            rocksdb::Status getStatus = m_db->Get (options, slice, &string);

            if (getStatus.ok ())
            {
                DecodedBlob decoded (key, string.data (), string.size ());

                if (decoded.wasOk ())
                {
                    *pObject = decoded.createObject ();
                }
                else
                {
                    // Decoding failed, probably corrupted!
                    //
                    status = dataCorrupt;
                }
            }
            else
            {
                if (getStatus.IsCorruption ())
                {
                    status = dataCorrupt;
                }
                else if (getStatus.IsNotFound ())
                {
                    status = notFound;
                }
                else
                {
                    status = Status (customCode + getStatus.code());

                    m_journal.error << getStatus.ToString ();
                }
            }
        }

        return status;
    }

    void store (NodeObject::ref object)
    {
        m_batch.store (object);
    }

    void storeBatch (Batch const& batch)
    {
        rocksdb::WriteBatch wb;

        {
            EncodedBlob::Pool::ScopedItem item (m_blobPool);

            BOOST_FOREACH (NodeObject::ref object, batch)
            {
                item.getObject ().prepare (object);

                wb.Put (
                    rocksdb::Slice (reinterpret_cast <char const*> (item.getObject ().getKey ()),
                                                                    m_keyBytes),
                    rocksdb::Slice (reinterpret_cast <char const*> (item.getObject ().getData ()),
                                                                    item.getObject ().getSize ()));
            }
        }

        rocksdb::WriteOptions const options;

        m_db->Write (options, &wb).ok ();
    }

    void visitAll (VisitCallback& callback)
    {
        rocksdb::ReadOptions const options;

        ScopedPointer <rocksdb::Iterator> it (m_db->NewIterator (options));

        for (it->SeekToFirst (); it->Valid (); it->Next ())
        {
            if (it->key ().size () == m_keyBytes)
            {
                DecodedBlob decoded (it->key ().data (),
                                                it->value ().data (),
                                                it->value ().size ());

                if (decoded.wasOk ())
                {
                    NodeObject::Ptr object (decoded.createObject ());

                    callback.visitObject (object);
                }
                else
                {
                    // Uh oh, corrupted data!
                    WriteLog (lsFATAL, NodeObject) << "Corrupt NodeObject #" << uint256 (it->key ().data ());
                }
            }
            else
            {
                // VFALCO NOTE What does it mean to find an
                //             incorrectly sized key? Corruption?
                WriteLog (lsFATAL, NodeObject) << "Bad key size = " << it->key ().size ();
            }
        }
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
    Journal m_journal;
    size_t const m_keyBytes;
    Scheduler& m_scheduler;
    BatchWriter m_batch;
    StringPool m_stringPool;
    EncodedBlob::Pool m_blobPool;
    std::string m_name;
    ScopedPointer <rocksdb::DB> m_db;
};

//------------------------------------------------------------------------------

class RocksDBFactoryImp : public RocksDBFactory
{
public:
    std::shared_ptr <rocksdb::Cache> m_lruCache;

    RocksDBFactoryImp ()
    {
        rocksdb::Options options;
        options.create_if_missing = true;
        options.block_cache = rocksdb::NewLRUCache (
            getConfig ().getSize (siHashNodeDBCache) * 1024 * 1024);

        m_lruCache = options.block_cache;
    }

    ~RocksDBFactoryImp ()
    {

    }

    String getName () const
    {
        return "RocksDB";
    }

    Backend* createInstance (
        size_t keyBytes, Parameters const& keyValues,
            Scheduler& scheduler, Journal journal)
    {
        return new RocksDBFactory::BackendImp (
            keyBytes, keyValues, scheduler, journal);
    }
};

//------------------------------------------------------------------------------

RocksDBFactory::~RocksDBFactory ()
{
}

RocksDBFactory* RocksDBFactory::New ()
{
    return new RocksDBFactoryImp;
}

}

#endif
