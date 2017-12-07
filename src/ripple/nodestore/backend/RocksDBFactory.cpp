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

#include <BeastConfig.h>

#include <ripple/unity/rocksdb.h>

#if RIPPLE_ROCKSDB_AVAILABLE

#include <ripple/basics/contract.h>
#include <ripple/core/Config.h> // VFALCO Bad dependency
#include <ripple/nodestore/Factory.h>
#include <ripple/nodestore/Manager.h>
#include <ripple/nodestore/impl/BatchWriter.h>
#include <ripple/nodestore/impl/DecodedBlob.h>
#include <ripple/nodestore/impl/EncodedBlob.h>
#include <ripple/beast/core/CurrentThreadName.h>
#include <atomic>
#include <memory>

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
        beast::setCurrentThreadName (ss.str());

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
{
private:
    std::atomic <bool> m_deletePath;

public:
    beast::Journal m_journal;
    size_t const m_keyBytes;
    Scheduler& m_scheduler;
    BatchWriter m_batch;
    std::string m_name;
    std::unique_ptr <rocksdb::DB> m_db;
    int fdlimit_ = 2048;

    RocksDBBackend (int keyBytes, Section const& keyValues,
        Scheduler& scheduler, beast::Journal journal, RocksDBEnv* env)
        : m_deletePath (false)
        , m_journal (journal)
        , m_keyBytes (keyBytes)
        , m_scheduler (scheduler)
        , m_batch (*this, scheduler)
    {
        if (! get_if_exists(keyValues, "path", m_name))
            Throw<std::runtime_error> ("Missing path in RocksDBFactory backend");

        rocksdb::Options options;
        rocksdb::BlockBasedTableOptions table_options;
        options.create_if_missing = true;
        options.env = env;

        if (keyValues.exists ("cache_mb"))
            table_options.block_cache = rocksdb::NewLRUCache (
                get<int>(keyValues, "cache_mb") * 1024L * 1024L);

        if (auto const v = get<int>(keyValues, "filter_bits"))
        {
            bool filter_blocks = !keyValues.exists("filter_full") ||
                (get<int>(keyValues, "filter_full") == 0);
            table_options.filter_policy.reset (rocksdb::NewBloomFilterPolicy (v, filter_blocks));
        }

        if (get_if_exists (keyValues, "open_files", options.max_open_files))
            fdlimit_ = options.max_open_files;

        if (keyValues.exists ("file_size_mb"))
        {
            options.target_file_size_base = 1024 * 1024 * get<int>(keyValues,"file_size_mb");
            options.max_bytes_for_level_base = 5 * options.target_file_size_base;
            options.write_buffer_size = 2 * options.target_file_size_base;
        }

        get_if_exists (keyValues, "file_size_mult", options.target_file_size_multiplier);

        if (keyValues.exists ("bg_threads"))
        {
            options.env->SetBackgroundThreads
                (get<int>(keyValues, "bg_threads"), rocksdb::Env::LOW);
        }

        if (keyValues.exists ("high_threads"))
        {
            auto const highThreads = get<int>(keyValues, "high_threads");
            options.env->SetBackgroundThreads (highThreads, rocksdb::Env::HIGH);

            // If we have high-priority threads, presumably we want to
            // use them for background flushes
            if (highThreads > 0)
                options.max_background_flushes = highThreads;
        }

        if (keyValues.exists ("compression") &&
            (get<int>(keyValues, "compression") == 0))
        {
            options.compression = rocksdb::kNoCompression;
        }

        get_if_exists (keyValues, "block_size", table_options.block_size);

        if (keyValues.exists ("universal_compaction") &&
            (get<int>(keyValues, "universal_compaction") != 0))
        {
            options.compaction_style = rocksdb::kCompactionStyleUniversal;
            options.min_write_buffer_number_to_merge = 2;
            options.max_write_buffer_number = 6;
            options.write_buffer_size = 6 * options.target_file_size_base;
        }

        if (keyValues.exists("bbt_options"))
        {
            auto const s = rocksdb::GetBlockBasedTableOptionsFromString(
                table_options,
                get<std::string>(keyValues, "bbt_options"),
                &table_options);
            if (! s.ok())
                Throw<std::runtime_error> (
                    std::string("Unable to set RocksDB bbt_options: ") + s.ToString());
        }

        options.table_factory.reset(NewBlockBasedTableFactory(table_options));

        if (keyValues.exists("options"))
        {
            auto const s = rocksdb::GetOptionsFromString(
                options, get<std::string>(keyValues, "options"), &options);
            if (! s.ok())
                Throw<std::runtime_error> (
                    std::string("Unable to set RocksDB options: ") + s.ToString());
        }

        rocksdb::DB* db = nullptr;
        rocksdb::Status status = rocksdb::DB::Open (options, m_name, &db);
        if (! status.ok () || ! db)
            Throw<std::runtime_error> (
                std::string("Unable to open/create RocksDB: ") + status.ToString());

        m_db.reset (db);

        std::string s1, s2;
        rocksdb::GetStringFromDBOptions(&s1, options, "; ");
        rocksdb::GetStringFromColumnFamilyOptions(&s2, options, "; ");
        JLOG(m_journal.debug()) << "RocksDB DBOptions: " << s1;
        JLOG(m_journal.debug()) << "RocksDB CFOptions: " << s2;
    }

    ~RocksDBBackend ()
    {
        close();
    }

    void
    close() override
    {
        if (m_db)
        {
            m_db.reset();
            if (m_deletePath)
            {
                boost::filesystem::path dir = m_name;
                boost::filesystem::remove_all (dir);
            }
        }
    }

    std::string
    getName() override
    {
        return m_name;
    }

    //--------------------------------------------------------------------------

    Status
    fetch (void const* key, std::shared_ptr<NodeObject>* pObject) override
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

                JLOG(m_journal.error()) << getStatus.ToString ();
            }
        }

        return status;
    }

    bool
    canFetchBatch() override
    {
        return false;
    }

    std::vector<std::shared_ptr<NodeObject>>
    fetchBatch (std::size_t n, void const* const* keys) override
    {
        Throw<std::runtime_error> ("pure virtual called");
        return {};
    }

    void
    store (std::shared_ptr<NodeObject> const& object) override
    {
        m_batch.store (object);
    }

    void
    storeBatch (Batch const& batch) override
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

        auto ret = m_db->Write (options, &wb);

        if (! ret.ok ())
            Throw<std::runtime_error> ("storeBatch failed: " + ret.ToString());
    }

    void
    for_each (std::function <void(std::shared_ptr<NodeObject>)> f) override
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
                    JLOG(m_journal.fatal()) <<
                        "Corrupt NodeObject #" <<
                        from_hex_text<uint256>(it->key ().data ());
                }
            }
            else
            {
                // VFALCO NOTE What does it mean to find an
                //             incorrectly sized key? Corruption?
                JLOG(m_journal.fatal()) <<
                    "Bad key size = " << it->key ().size ();
            }
        }
    }

    int
    getWriteLoad () override
    {
        return m_batch.getWriteLoad ();
    }

    void
    setDeletePath() override
    {
        m_deletePath = true;
    }

    //--------------------------------------------------------------------------

    void
    writeBatch (Batch const& batch) override
    {
        storeBatch (batch);
    }

    void
    verify() override
    {
    }

    /** Returns the number of file handles the backend expects to need */
    int
    fdlimit() const override
    {
        return fdlimit_;
    }
};

//------------------------------------------------------------------------------

class RocksDBFactory : public Factory
{
public:
    RocksDBEnv m_env;

    RocksDBFactory ()
    {
        Manager::instance().insert(*this);
    }

    ~RocksDBFactory ()
    {
        Manager::instance().erase(*this);
    }

    std::string
    getName () const
    {
        return "RocksDB";
    }

    std::unique_ptr <Backend>
    createInstance (
        size_t keyBytes,
        Section const& keyValues,
        Scheduler& scheduler,
        beast::Journal journal)
    {
        return std::make_unique <RocksDBBackend> (
            keyBytes, keyValues, scheduler, journal, &m_env);
    }
};

static RocksDBFactory rocksDBFactory;

}
}

#endif
