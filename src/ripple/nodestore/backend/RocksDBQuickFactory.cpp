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
#include <ripple/nodestore/impl/DecodedBlob.h>
#include <ripple/nodestore/impl/EncodedBlob.h>
#include <beast/threads/Thread.h>
#include <atomic>
#include <memory>

namespace ripple {
namespace NodeStore {

class RocksDBQuickEnv : public rocksdb::EnvWrapper
{
public:
    RocksDBQuickEnv ()
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
        EnvWrapper::StartThread (&RocksDBQuickEnv::thread_entry, p);
    }
};

//------------------------------------------------------------------------------

class RocksDBQuickBackend
    : public Backend
{
private:
    std::atomic <bool> m_deletePath;

public:
    beast::Journal m_journal;
    size_t const m_keyBytes;
    std::string m_name;
    std::unique_ptr <rocksdb::DB> m_db;

    RocksDBQuickBackend (int keyBytes, Section const& keyValues,
        Scheduler& scheduler, beast::Journal journal, RocksDBQuickEnv* env)
        : m_deletePath (false)
        , m_journal (journal)
        , m_keyBytes (keyBytes)
        , m_name (get<std::string>(keyValues, "path"))
    {
        if (m_name.empty())
            Throw<std::runtime_error> (
                "Missing path in RocksDBQuickFactory backend");

        // Defaults
        std::uint64_t budget = 512 * 1024 * 1024;  // 512MB
        std::string style("level");
        std::uint64_t threads=4;

        get_if_exists (keyValues, "budget", budget);
        get_if_exists (keyValues, "style", style);
        get_if_exists (keyValues, "threads", threads);


        // Set options
        rocksdb::Options options;
        options.create_if_missing = true;
        options.env = env;

        if (style == "level")
            options.OptimizeLevelStyleCompaction(budget);

        if (style == "universal")
            options.OptimizeUniversalStyleCompaction(budget);

        if (style == "point")
            options.OptimizeForPointLookup(budget / 1024 / 1024);  // In MB

        options.IncreaseParallelism(threads);

        // Allows hash indexes in blocks
        options.prefix_extractor.reset(rocksdb::NewNoopTransform());

        // overrride OptimizeLevelStyleCompaction
        options.min_write_buffer_number_to_merge = 1;

        rocksdb::BlockBasedTableOptions table_options;
        // Use hash index
        table_options.index_type =
            rocksdb::BlockBasedTableOptions::kHashSearch;
        table_options.filter_policy.reset(
            rocksdb::NewBloomFilterPolicy(10));
        options.table_factory.reset(
            NewBlockBasedTableFactory(table_options));

        // Higher values make reads slower
        // table_options.block_size = 4096;

        // No point when DatabaseImp has a cache
        // table_options.block_cache =
        //     rocksdb::NewLRUCache(64 * 1024 * 1024);

        options.memtable_factory.reset(rocksdb::NewHashSkipListRepFactory());
        // Alternative:
        // options.memtable_factory.reset(
        //     rocksdb::NewHashCuckooRepFactory(options.write_buffer_size));

        get_if_exists (keyValues, "open_files", options.max_open_files);

        if (keyValues.exists ("compression") &&
            (get<int>(keyValues, "compression") == 0))
            options.compression = rocksdb::kNoCompression;

        rocksdb::DB* db = nullptr;

        rocksdb::Status status = rocksdb::DB::Open (options, m_name, &db);
        if (! status.ok () || ! db)
            Throw<std::runtime_error> (
                std::string("Unable to open/create RocksDBQuick: ") +
                    status.ToString());

        m_db.reset (db);
    }

    ~RocksDBQuickBackend ()
    {
        close();
    }

    std::string
    getName() override
    {
        return m_name;
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

                m_journal.error << getStatus.ToString ();
            }
        }

        return status;
    }

    bool
    canFetchBatch() override
    {
        return false;
    }

    void
    store (std::shared_ptr<NodeObject> const& object) override
    {
        storeBatch(Batch{object});
    }

    std::vector<std::shared_ptr<NodeObject>>
    fetchBatch (std::size_t n, void const* const* keys) override
    {
        Throw<std::runtime_error> ("pure virtual called");
        return {};
    }

    void
    storeBatch (Batch const& batch) override
    {
        rocksdb::WriteBatch wb;

        EncodedBlob encoded;

        for (auto const& e : batch)
        {
            encoded.prepare (e);

            wb.Put(
                rocksdb::Slice(reinterpret_cast<char const*>(encoded.getKey()),
                               m_keyBytes),
                rocksdb::Slice(reinterpret_cast<char const*>(encoded.getData()),
                               encoded.getSize()));
        }

        rocksdb::WriteOptions options;

        // Crucial to ensure good write speed and non-blocking writes to memtable
        options.disableWAL = true;

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
                    if (m_journal.fatal) m_journal.fatal <<
                        "Corrupt NodeObject #" <<
                        from_hex_text<uint256>(it->key ().data ());
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
    getWriteLoad () override
    {
        return 0;
    }

    void
    setDeletePath() override
    {
        m_deletePath = true;
    }

    //--------------------------------------------------------------------------

    void
    writeBatch (Batch const& batch)
    {
        storeBatch (batch);
    }

    void
    verify() override
    {
    }
};

//------------------------------------------------------------------------------

class RocksDBQuickFactory : public Factory
{
public:
    RocksDBQuickEnv m_env;

    RocksDBQuickFactory()
    {
        Manager::instance().insert(*this);
    }

    ~RocksDBQuickFactory()
    {
        Manager::instance().erase(*this);
    }

    std::string
    getName () const
    {
        return "RocksDBQuick";
    }

    std::unique_ptr <Backend>
    createInstance (
        size_t keyBytes,
        Section const& keyValues,
        Scheduler& scheduler,
        beast::Journal journal)
    {
        return std::make_unique <RocksDBQuickBackend> (
            keyBytes, keyValues, scheduler, journal, &m_env);
    }
};

static RocksDBQuickFactory rocksDBQuickFactory;

}
}

#endif
