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

#include <ripple/module/core/functional/Config.h>
#include <beast/threads/Thread.h>
#include <atomic>

namespace ripple {
namespace NodeStore {

class RocksDBEnv : public rocksdb::EnvWrapper
{
public:
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
    
    static
    void
    thread_entry (void* ptr)
    {
        ThreadParams* const p (reinterpret_cast <ThreadParams*> (ptr));
        void (*f)(void*) = p->f;
        void* a (p->a);
        delete p;

        static std::atomic <std::size_t> n;
        std::size_t const id (++n);
        std::stringstream ss;
        ss << "rocksdb #" << id;
        beast::Thread::setCurrentThreadName (ss.str());

        (*f)(a);
    }

    void
    StartThread (void (*f)(void*), void* a)
    {
        ThreadParams* const p (new ThreadParams (f, a));
        EnvWrapper::StartThread (&RocksDBEnv::thread_entry, p);
    }
};

//------------------------------------------------------------------------------

class RocksDBBackend
    : public Backend
    , public BatchWriter::Callback
    , public beast::LeakChecked <RocksDBBackend>
{
public:
    beast::Journal m_journal;
    size_t const m_keyBytes;
    Scheduler& m_scheduler;
    BatchWriter m_batch;
    std::string m_name;
    std::unique_ptr <rocksdb::DB> m_db;

    RocksDBBackend (int keyBytes, Parameters const& keyValues,
        Scheduler& scheduler, beast::Journal journal, RocksDBEnv* env)
        : m_journal (journal)
        , m_keyBytes (keyBytes)
        , m_scheduler (scheduler)
        , m_batch (*this, scheduler)
        , m_name (keyValues ["path"].toStdString ())
    {
        if (m_name.empty())
            throw std::runtime_error ("Missing path in RocksDBFactory backend");

        rocksdb::Options options;
        options.create_if_missing = true;
        options.env = env;

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

        if (! keyValues["bg_threads"].isEmpty())
        {
            options.env->SetBackgroundThreads
                (keyValues["bg_threads"].getIntValue(), rocksdb::Env::LOW);
        }

        if (! keyValues["high_threads"].isEmpty())
        {
            auto const highThreads = keyValues["high_threads"].getIntValue();
            options.env->SetBackgroundThreads (highThreads, rocksdb::Env::HIGH);

            // If we have high-priority threads, presumably we want to
            // use them for background flushes
            if (highThreads > 0)
                options.max_background_flushes = highThreads;
        }

        if (! keyValues["compression"].isEmpty ())
        {
            if (keyValues["compression"].getIntValue () == 0)
            {
                options.compression = rocksdb::kNoCompression;
            }
        }

        rocksdb::DB* db = nullptr;
        rocksdb::Status status = rocksdb::DB::Open (options, m_name, &db);
        if (!status.ok () || !db)
            throw std::runtime_error (std::string("Unable to open/create RocksDB: ") + status.ToString());

        m_db.reset (db);
    }

    ~RocksDBBackend ()
    {
    }

    std::string 
    getName()
    {
        return m_name;
    }

    //--------------------------------------------------------------------------

    Status
    fetch (void const* key, NodeObject::Ptr* pObject)
    {
        pObject->reset ();

        Status status (ok);

        rocksdb::ReadOptions const options;
        rocksdb::Slice const slice (static_cast <char const*> (key), m_keyBytes);

        std::string string;

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

        return status;
    }

    void
    store (NodeObject::ref object)
    {
        m_batch.store (object);
    }

    void
    storeBatch (Batch const& batch)
    {
        rocksdb::WriteBatch wb;

        EncodedBlob encoded;

        for (auto const& e : batch)
        {
            encoded.prepare (e);

            wb.Put (
                rocksdb::Slice (reinterpret_cast <char const*> (
                    encoded.getKey ()), m_keyBytes),
                rocksdb::Slice (reinterpret_cast <char const*> (
                    encoded.getData ()), encoded.getSize ()));
        }

        rocksdb::WriteOptions const options;

        m_db->Write (options, &wb).ok ();
    }

    void
    for_each (std::function <void(NodeObject::Ptr)> f)
    {
        rocksdb::ReadOptions const options;

        std::unique_ptr <rocksdb::Iterator> it (m_db->NewIterator (options));

        for (it->SeekToFirst (); it->Valid (); it->Next ())
        {
            if (it->key ().size () == m_keyBytes)
            {
                DecodedBlob decoded (it->key ().data (),
                                                it->value ().data (),
                                                it->value ().size ());

                if (decoded.wasOk ())
                {
                    f (decoded.createObject ());
                }
                else
                {
                    // Uh oh, corrupted data!
                    if (m_journal.fatal) m_journal.fatal <<
                        "Corrupt NodeObject #" << uint256 (it->key ().data ());
                }
            }
            else
            {
                // VFALCO NOTE What does it mean to find an
                //             incorrectly sized key? Corruption?
                if (m_journal.fatal) m_journal.fatal <<
                    "Bad key size = " << it->key ().size ();
            }
        }
    }

    int
    getWriteLoad ()
    {
        return m_batch.getWriteLoad ();
    }

    //--------------------------------------------------------------------------

    void
    writeBatch (Batch const& batch)
    {
        storeBatch (batch);
    }
};

//------------------------------------------------------------------------------

class RocksDBFactory : public Factory
{
public:
    std::shared_ptr <rocksdb::Cache> m_lruCache;
    RocksDBEnv m_env;

    RocksDBFactory ()
    {
        rocksdb::Options options;
        options.create_if_missing = true;
        options.block_cache = rocksdb::NewLRUCache (
            getConfig ().getSize (siHashNodeDBCache) * 1024 * 1024);

        m_lruCache = options.block_cache;
    }

    ~RocksDBFactory ()
    {
    }

    beast::String
    getName () const
    {
        return "RocksDB";
    }

    std::unique_ptr <Backend> 
    createInstance (
        size_t keyBytes,
        Parameters const& keyValues,
        Scheduler& scheduler,
        beast::Journal journal)
    {
        return std::make_unique <RocksDBBackend> (
            keyBytes, keyValues, scheduler, journal, &m_env);
    }
};

//------------------------------------------------------------------------------

std::unique_ptr <Factory>
make_RocksDBFactory ()
{
    return std::make_unique <RocksDBFactory> ();
}

}
}

#endif
