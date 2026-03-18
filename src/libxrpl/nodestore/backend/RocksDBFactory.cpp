#include <xrpl/basics/rocksdb.h>

#if XRPL_ROCKSDB_AVAILABLE
#include <xrpl/basics/ByteUtilities.h>
#include <xrpl/basics/contract.h>
#include <xrpl/basics/safe_cast.h>
#include <xrpl/beast/core/CurrentThreadName.h>
#include <xrpl/nodestore/Factory.h>
#include <xrpl/nodestore/Manager.h>
#include <xrpl/nodestore/detail/BatchWriter.h>
#include <xrpl/nodestore/detail/DecodedBlob.h>
#include <xrpl/nodestore/detail/EncodedBlob.h>

#include <atomic>
#include <memory>
#include <thread>

namespace xrpl {
namespace NodeStore {

class RocksDBEnv : public rocksdb::EnvWrapper
{
public:
    RocksDBEnv() : EnvWrapper(rocksdb::Env::Default())
    {
    }

    struct ThreadParams
    {
        ThreadParams(void (*f_)(void*), void* a_) : f(f_), a(a_)
        {
        }

        void (*f)(void*);
        void* a;
    };

    static void
    thread_entry(void* ptr)
    {
        ThreadParams* const p(reinterpret_cast<ThreadParams*>(ptr));
        void (*f)(void*) = p->f;
        void* a(p->a);
        delete p;

        static std::atomic<std::size_t> n;
        std::size_t const id(++n);
        std::stringstream ss;
        ss << "rocksdb #" << id;
        beast::setCurrentThreadName(ss.str());

        (*f)(a);
    }

    void
    StartThread(void (*f)(void*), void* a) override
    {
        ThreadParams* const p(new ThreadParams(f, a));
        EnvWrapper::StartThread(&RocksDBEnv::thread_entry, p);
    }
};

//------------------------------------------------------------------------------

class RocksDBBackend : public Backend, public BatchWriter::Callback
{
private:
    std::atomic<bool> m_deletePath;

public:
    beast::Journal m_journal;
    size_t const m_keyBytes;
    BatchWriter m_batch;
    std::string m_name;
    std::unique_ptr<rocksdb::DB> m_db;
    int fdRequired_ = 2048;
    rocksdb::Options m_options;

    RocksDBBackend(
        int keyBytes,
        Section const& keyValues,
        Scheduler& scheduler,
        beast::Journal journal,
        RocksDBEnv* env)
        : m_deletePath(false), m_journal(journal), m_keyBytes(keyBytes), m_batch(*this, scheduler)
    {
        if (!get_if_exists(keyValues, "path", m_name))
            Throw<std::runtime_error>("Missing path in RocksDBFactory backend");

        rocksdb::BlockBasedTableOptions table_options;
        m_options.env = env;

        bool hard_set = keyValues.exists("hard_set") && get<bool>(keyValues, "hard_set");

        if (keyValues.exists("cache_mb"))
        {
            auto size = get<int>(keyValues, "cache_mb");

            if (!hard_set && size == 256)
                size = 1024;

            table_options.block_cache = rocksdb::NewLRUCache(megabytes(size));
        }

        if (auto const v = get<int>(keyValues, "filter_bits"))
        {
            bool const filter_blocks =
                !keyValues.exists("filter_full") || (get<int>(keyValues, "filter_full") == 0);
            table_options.filter_policy.reset(rocksdb::NewBloomFilterPolicy(v, filter_blocks));
        }

        if (get_if_exists(keyValues, "open_files", m_options.max_open_files))
        {
            if (!hard_set && m_options.max_open_files == 2000)
                m_options.max_open_files = 8000;

            fdRequired_ = m_options.max_open_files + 128;
        }

        if (keyValues.exists("file_size_mb"))
        {
            auto file_size_mb = get<int>(keyValues, "file_size_mb");

            if (!hard_set && file_size_mb == 8)
                file_size_mb = 256;

            m_options.target_file_size_base = megabytes(file_size_mb);
            m_options.max_bytes_for_level_base = 5 * m_options.target_file_size_base;
            m_options.write_buffer_size = 2 * m_options.target_file_size_base;
        }

        get_if_exists(keyValues, "file_size_mult", m_options.target_file_size_multiplier);

        if (keyValues.exists("bg_threads"))
        {
            m_options.env->SetBackgroundThreads(
                get<int>(keyValues, "bg_threads"), rocksdb::Env::LOW);
        }

        if (keyValues.exists("high_threads"))
        {
            auto const highThreads = get<int>(keyValues, "high_threads");
            m_options.env->SetBackgroundThreads(highThreads, rocksdb::Env::HIGH);

            // If we have high-priority threads, presumably we want to
            // use them for background flushes
            if (highThreads > 0)
                m_options.max_background_flushes = highThreads;
        }

        m_options.compression = rocksdb::kSnappyCompression;

        get_if_exists(keyValues, "block_size", table_options.block_size);

        if (keyValues.exists("universal_compaction") &&
            (get<int>(keyValues, "universal_compaction") != 0))
        {
            m_options.compaction_style = rocksdb::kCompactionStyleUniversal;
            m_options.min_write_buffer_number_to_merge = 2;
            m_options.max_write_buffer_number = 6;
            m_options.write_buffer_size = 6 * m_options.target_file_size_base;
        }

        if (keyValues.exists("bbt_options"))
        {
            rocksdb::ConfigOptions config_options;
            auto const s = rocksdb::GetBlockBasedTableOptionsFromString(
                config_options, table_options, get(keyValues, "bbt_options"), &table_options);
            if (!s.ok())
            {
                Throw<std::runtime_error>(
                    std::string("Unable to set RocksDB bbt_options: ") + s.ToString());
            }
        }

        m_options.table_factory.reset(NewBlockBasedTableFactory(table_options));

        if (keyValues.exists("options"))
        {
            auto const s =
                rocksdb::GetOptionsFromString(m_options, get(keyValues, "options"), &m_options);
            if (!s.ok())
            {
                Throw<std::runtime_error>(
                    std::string("Unable to set RocksDB options: ") + s.ToString());
            }
        }

        // Enable pipelined writes for better write concurrency.
        m_options.enable_pipelined_write = true;

        // Set background job parallelism for better compaction/flush performance to the number of
        // hardware threads, unless the value is explicitly provided in the config. The default is
        // 2 (see include/rocksdb/options.h in the Conan dependency directory), so don't use fewer
        // than that.
        if (auto v = get<unsigned int>(keyValues, "max_background_jobs", 0); v > 2)
        {
            m_options.max_background_jobs = v;
        }
        else if (v = numHardwareThreads; v > 2)
        {
            m_options.max_background_jobs = v;
        }

        // Set subcompactions for parallel compaction within a job to the number of hardware
        // threads, unless the value is explicitly provided in the config. The default is 1 (see
        // include/rocksdb/options.h in the Conan dependency directory), so don't use fewer
        // than that if no value is explicitly provided.
        if (auto v = get<unsigned int>(keyValues, "max_subcompactions", 0); v > 1)
        {
            m_options.max_subcompactions = v;
        }
        else if (v = numHardwareThreads / 2; v > 1)
        {
            m_options.max_subcompactions = v;
        }

        // Enable direct I/O by default unless explicitly disabled in the config. This bypasses the
        // OS page cache for better predictable performance on SSDs.
        m_options.use_direct_reads = get<bool>(keyValues, "use_direct_io", true);
        m_options.use_direct_io_for_flush_and_compaction =
            get<bool>(keyValues, "use_direct_io", true);

        std::string s1, s2;
        rocksdb::GetStringFromDBOptions(&s1, m_options, "; ");
        rocksdb::GetStringFromColumnFamilyOptions(&s2, m_options, "; ");
        JLOG(m_journal.debug()) << "RocksDB DBOptions: " << s1;
        JLOG(m_journal.debug()) << "RocksDB CFOptions: " << s2;
    }

    ~RocksDBBackend() override
    {
        close();
    }

    void
    open(bool createIfMissing) override
    {
        if (m_db)
        {
            // LCOV_EXCL_START
            UNREACHABLE(
                "xrpl::NodeStore::RocksDBBackend::open : database is already "
                "open");
            JLOG(m_journal.error()) << "database is already open";
            return;
            // LCOV_EXCL_STOP
        }
        rocksdb::DB* db = nullptr;
        m_options.create_if_missing = createIfMissing;
        rocksdb::Status status = rocksdb::DB::Open(m_options, m_name, &db);
        if (!status.ok() || !db)
        {
            Throw<std::runtime_error>(
                std::string("Unable to open/create RocksDB: ") + status.ToString());
        }
        m_db.reset(db);
    }

    bool
    isOpen() override
    {
        return static_cast<bool>(m_db);
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
                boost::filesystem::remove_all(dir);
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
    fetch(uint256 const& hash, std::shared_ptr<NodeObject>* pObject) override
    {
        XRPL_ASSERT(m_db, "xrpl::NodeStore::RocksDBBackend::fetch : non-null database");
        pObject->reset();

        Status status(ok);

        rocksdb::ReadOptions const options;
        rocksdb::Slice const slice(std::bit_cast<char const*>(hash.data()), m_keyBytes);
        std::string string;
        rocksdb::Status getStatus = m_db->Get(options, slice, &string);

        if (getStatus.ok())
        {
            DecodedBlob decoded(hash.data(), string.data(), string.size());
            if (decoded.wasOk())
            {
                *pObject = decoded.createObject();
            }
            else
            {
                // Decoding failed, probably corrupted.
                status = dataCorrupt;
            }
        }
        else
        {
            if (getStatus.IsCorruption())
            {
                status = dataCorrupt;
            }
            else if (getStatus.IsNotFound())
            {
                status = notFound;
            }
            else
            {
                status = Status(customCode + unsafe_cast<int>(getStatus.code()));
                JLOG(m_journal.error()) << getStatus.ToString();
            }
        }

        return status;
    }

    std::pair<std::vector<std::shared_ptr<NodeObject>>, Status>
    fetchBatch(std::vector<uint256> const& hashes) override
    {
        XRPL_ASSERT(m_db, "xrpl::NodeStore::RocksDBBackend::fetchBatch : non-null database");

        if (hashes.empty())
            return {{}, ok};

        // Use MultiGet for parallel reads to allow RocksDB to fetch multiple keys concurrently,
        // significantly improving throughput compared to sequential fetch() calls.

        std::vector<rocksdb::Slice> keys;
        keys.reserve(hashes.size());
        for (auto const& h : hashes)
        {
            keys.emplace_back(std::bit_cast<char const*>(h.data()), m_keyBytes);
        }

        rocksdb::ReadOptions options;
        options.async_io = true;  // Enable for better concurrency on supported platforms.
        std::vector<std::string> values(hashes.size());
        auto statuses = m_db->MultiGet(options, keys, &values);

        std::vector<std::shared_ptr<NodeObject>> results(hashes.size());
        for (auto i = 0; i < hashes.size(); ++i)
        {
            if (statuses[i].ok())
            {
                DecodedBlob decoded(hashes[i].data(), values[i].data(), values[i].size());
                if (decoded.wasOk())
                {
                    results[i] = decoded.createObject();
                }
            }
            else if (!statuses[i].IsNotFound())
            {
                // Log other errors but continue processing.
                JLOG(m_journal.warn()) << "fetchBatch: MultiGet error for key "
                                       << keys[i].ToString() << ": " << statuses[i].ToString();
            }
        }

        return {results, ok};
    }

    void
    store(std::shared_ptr<NodeObject> const& object) override
    {
        m_batch.store(object);
    }

    void
    storeBatch(Batch const& batch) override
    {
        XRPL_ASSERT(m_db, "xrpl::NodeStore::RocksDBBackend::storeBatch : non-null database");
        rocksdb::WriteBatch wb;

        for (auto const& e : batch)
        {
            EncodedBlob encoded(e);

            wb.Put(
                rocksdb::Slice(std::bit_cast<char const*>(encoded.getKey()), m_keyBytes),
                rocksdb::Slice(std::bit_cast<char const*>(encoded.getData()), encoded.getSize()));
        }

        // Configure WriteOptions for high throughput.
        // Note: no_slowdown is intentionally NOT set here. When set to true, RocksDB returns an
        //       error instead of stalling when write buffers are full, which could cause write
        //       failures during high load. We prefer to accept brief stalls over dropped writes.
        rocksdb::WriteOptions options;

        // Setting `sync = false` improves write throughput significantly by allowing the OS to
        // batch fsync operations, rather than forcing immediate disk synchronization on every
        // write. The Write-Ahead Log (WAL) is still written and flushed, so database consistency is
        // maintained across clean restarts and crashes.
        //
        // Note: On hard shutdown up to a few seconds of recent writes (since the last OS-initiated
        //       flush) may be lost from this node. However, since ledger data is replicated across
        //       the network, lost writes can be re-synced from peers during startup.
        options.sync = false;

        // Keep WAL enabled for crash recovery consistency.
        options.disableWAL = false;

        // Ensure RocksDB will not aggressive throttle the writes.
        options.low_pri = false;

        auto ret = m_db->Write(options, &wb);

        if (!ret.ok())
            Throw<std::runtime_error>("storeBatch failed: " + ret.ToString());
    }

    void
    sync() override
    {
    }

    void
    for_each(std::function<void(std::shared_ptr<NodeObject>)> f) override
    {
        XRPL_ASSERT(m_db, "xrpl::NodeStore::RocksDBBackend::for_each : non-null database");
        rocksdb::ReadOptions const options;

        std::unique_ptr<rocksdb::Iterator> it(m_db->NewIterator(options));

        for (it->SeekToFirst(); it->Valid(); it->Next())
        {
            if (it->key().size() == m_keyBytes)
            {
                DecodedBlob decoded(it->key().data(), it->value().data(), it->value().size());

                if (decoded.wasOk())
                {
                    f(decoded.createObject());
                }
                else
                {
                    // Uh oh, corrupted data!
                    JLOG(m_journal.fatal()) << "Corrupt NodeObject #" << it->key().ToString(true);
                }
            }
            else
            {
                // VFALCO NOTE What does it mean to find an
                //             incorrectly sized key? Corruption?
                JLOG(m_journal.fatal()) << "Bad key size = " << it->key().size();
            }
        }
    }

    int
    getWriteLoad() override
    {
        return m_batch.getWriteLoad();
    }

    void
    setDeletePath() override
    {
        m_deletePath = true;
    }

    //--------------------------------------------------------------------------

    void
    writeBatch(Batch const& batch) override
    {
        storeBatch(batch);
    }

    /** Returns the number of file descriptors the backend expects to need */
    int
    fdRequired() const override
    {
        return fdRequired_;
    }
};

//------------------------------------------------------------------------------

class RocksDBFactory : public Factory
{
private:
    Manager& manager_;

public:
    RocksDBEnv m_env;

    RocksDBFactory(Manager& manager) : manager_(manager)
    {
        manager_.insert(*this);
    }

    std::string
    getName() const override
    {
        return "RocksDB";
    }

    std::unique_ptr<Backend>
    createInstance(
        size_t keyBytes,
        Section const& keyValues,
        std::size_t,
        Scheduler& scheduler,
        beast::Journal journal) override
    {
        return std::make_unique<RocksDBBackend>(keyBytes, keyValues, scheduler, journal, &m_env);
    }
};

void
registerRocksDBFactory(Manager& manager)
{
    static RocksDBFactory instance{manager};
}

}  // namespace NodeStore
}  // namespace xrpl

#endif
