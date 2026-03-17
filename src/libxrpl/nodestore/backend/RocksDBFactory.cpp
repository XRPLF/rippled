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
    std::atomic<bool> deletePath_;

public:
    beast::Journal journal_;
    size_t const keyBytes_;
    BatchWriter batch_;
    std::string name_;
    std::unique_ptr<rocksdb::DB> db_;
    int fdRequired_ = 2048;
    rocksdb::Options options_;

    RocksDBBackend(
        int keyBytes,
        Section const& keyValues,
        Scheduler& scheduler,
        beast::Journal journal,
        RocksDBEnv* env)
        : deletePath_(false), journal_(journal), keyBytes_(keyBytes), batch_(*this, scheduler)
    {
        if (!get_if_exists(keyValues, "path", name_))
            Throw<std::runtime_error>("Missing path in RocksDBFactory backend");

        rocksdb::BlockBasedTableOptions table_options;
        options_.env = env;

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

        if (get_if_exists(keyValues, "open_files", options_.max_open_files))
        {
            if (!hard_set && options_.max_open_files == 2000)
                options_.max_open_files = 8000;

            fdRequired_ = options_.max_open_files + 128;
        }

        if (keyValues.exists("file_size_mb"))
        {
            auto file_size_mb = get<int>(keyValues, "file_size_mb");

            if (!hard_set && file_size_mb == 8)
                file_size_mb = 256;

            options_.target_file_size_base = megabytes(file_size_mb);
            options_.max_bytes_for_level_base = 5 * options_.target_file_size_base;
            options_.write_buffer_size = 2 * options_.target_file_size_base;
        }

        get_if_exists(keyValues, "file_size_mult", options_.target_file_size_multiplier);

        if (keyValues.exists("bg_threads"))
        {
            options_.env->SetBackgroundThreads(
                get<int>(keyValues, "bg_threads"), rocksdb::Env::LOW);
        }

        if (keyValues.exists("high_threads"))
        {
            auto const highThreads = get<int>(keyValues, "high_threads");
            options_.env->SetBackgroundThreads(highThreads, rocksdb::Env::HIGH);

            // If we have high-priority threads, presumably we want to
            // use them for background flushes
            if (highThreads > 0)
                options_.max_background_flushes = highThreads;
        }

        options_.compression = rocksdb::kSnappyCompression;

        get_if_exists(keyValues, "block_size", table_options.block_size);

        if (keyValues.exists("universal_compaction") &&
            (get<int>(keyValues, "universal_compaction") != 0))
        {
            options_.compaction_style = rocksdb::kCompactionStyleUniversal;
            options_.min_write_buffer_number_to_merge = 2;
            options_.max_write_buffer_number = 6;
            options_.write_buffer_size = 6 * options_.target_file_size_base;
        }

        if (keyValues.exists("bbt_options"))
        {
            rocksdb::ConfigOptions config_options;
            auto const s = rocksdb::GetBlockBasedTableOptionsFromString(
                config_options, table_options, get(keyValues, "bbt_options"), &table_options);
            if (!s.ok())
                Throw<std::runtime_error>(
                    std::string("Unable to set RocksDB bbt_options: ") + s.ToString());
        }

        options_.table_factory.reset(NewBlockBasedTableFactory(table_options));

        if (keyValues.exists("options"))
        {
            auto const s =
                rocksdb::GetOptionsFromString(options_, get(keyValues, "options"), &options_);
            if (!s.ok())
                Throw<std::runtime_error>(
                    std::string("Unable to set RocksDB options: ") + s.ToString());
        }

        std::string s1, s2;
        rocksdb::GetStringFromDBOptions(&s1, options_, "; ");
        rocksdb::GetStringFromColumnFamilyOptions(&s2, options_, "; ");
        JLOG(journal_.debug()) << "RocksDB DBOptions: " << s1;
        JLOG(journal_.debug()) << "RocksDB CFOptions: " << s2;
    }

    ~RocksDBBackend() override
    {
        close();
    }

    void
    open(bool createIfMissing) override
    {
        if (db_)
        {
            // LCOV_EXCL_START
            UNREACHABLE(
                "xrpl::NodeStore::RocksDBBackend::open : database is already "
                "open");
            JLOG(journal_.error()) << "database is already open";
            return;
            // LCOV_EXCL_STOP
        }
        rocksdb::DB* db = nullptr;
        options_.create_if_missing = createIfMissing;
        rocksdb::Status status = rocksdb::DB::Open(options_, name_, &db);
        if (!status.ok() || !db)
            Throw<std::runtime_error>(
                std::string("Unable to open/create RocksDB: ") + status.ToString());
        db_.reset(db);
    }

    bool
    isOpen() override
    {
        return static_cast<bool>(db_);
    }

    void
    close() override
    {
        if (db_)
        {
            db_.reset();
            if (deletePath_)
            {
                boost::filesystem::path dir = name_;
                boost::filesystem::remove_all(dir);
            }
        }
    }

    std::string
    getName() override
    {
        return name_;
    }

    //--------------------------------------------------------------------------

    Status
    fetch(uint256 const& hash, std::shared_ptr<NodeObject>* pObject) override
    {
        XRPL_ASSERT(db_, "xrpl::NodeStore::RocksDBBackend::fetch : non-null database");
        pObject->reset();

        Status status(ok);

        rocksdb::ReadOptions const options;
        rocksdb::Slice const slice(std::bit_cast<char const*>(hash.data()), keyBytes_);

        std::string string;

        rocksdb::Status getStatus = db_->Get(options, slice, &string);

        if (getStatus.ok())
        {
            DecodedBlob decoded(hash.data(), string.data(), string.size());

            if (decoded.wasOk())
            {
                *pObject = decoded.createObject();
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

                JLOG(journal_.error()) << getStatus.ToString();
            }
        }

        return status;
    }

    std::pair<std::vector<std::shared_ptr<NodeObject>>, Status>
    fetchBatch(std::vector<uint256> const& hashes) override
    {
        std::vector<std::shared_ptr<NodeObject>> results;
        results.reserve(hashes.size());
        for (auto const& h : hashes)
        {
            std::shared_ptr<NodeObject> nObj;
            Status status = fetch(h, &nObj);
            if (status != ok)
                results.push_back({});
            else
                results.push_back(nObj);
        }

        return {results, ok};
    }

    void
    store(std::shared_ptr<NodeObject> const& object) override
    {
        batch_.store(object);
    }

    void
    storeBatch(Batch const& batch) override
    {
        XRPL_ASSERT(
            db_,
            "xrpl::NodeStore::RocksDBBackend::storeBatch : non-null "
            "database");
        rocksdb::WriteBatch wb;

        for (auto const& e : batch)
        {
            EncodedBlob encoded(e);

            wb.Put(
                rocksdb::Slice(std::bit_cast<char const*>(encoded.getKey()), keyBytes_),
                rocksdb::Slice(std::bit_cast<char const*>(encoded.getData()), encoded.getSize()));
        }

        rocksdb::WriteOptions const options;

        auto ret = db_->Write(options, &wb);

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
        XRPL_ASSERT(db_, "xrpl::NodeStore::RocksDBBackend::for_each : non-null database");
        rocksdb::ReadOptions const options;

        std::unique_ptr<rocksdb::Iterator> it(db_->NewIterator(options));

        for (it->SeekToFirst(); it->Valid(); it->Next())
        {
            if (it->key().size() == keyBytes_)
            {
                DecodedBlob decoded(it->key().data(), it->value().data(), it->value().size());

                if (decoded.wasOk())
                {
                    f(decoded.createObject());
                }
                else
                {
                    // Uh oh, corrupted data!
                    JLOG(journal_.fatal()) << "Corrupt NodeObject #" << it->key().ToString(true);
                }
            }
            else
            {
                // VFALCO NOTE What does it mean to find an
                //             incorrectly sized key? Corruption?
                JLOG(journal_.fatal()) << "Bad key size = " << it->key().size();
            }
        }
    }

    int
    getWriteLoad() override
    {
        return batch_.getWriteLoad();
    }

    void
    setDeletePath() override
    {
        deletePath_ = true;
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
    RocksDBEnv env_;

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
        return std::make_unique<RocksDBBackend>(keyBytes, keyValues, scheduler, journal, &env_);
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
