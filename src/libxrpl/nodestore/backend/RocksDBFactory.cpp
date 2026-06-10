#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/config/BasicConfig.h>
#include <xrpl/config/Constants.h>
#include <xrpl/nodestore/Backend.h>
#include <xrpl/nodestore/NodeObject.h>
#include <xrpl/nodestore/Scheduler.h>
#include <xrpl/nodestore/Types.h>

#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>

#include <rocksdb/advanced_options.h>
#include <rocksdb/cache.h>
#include <rocksdb/compression_type.h>
#include <rocksdb/convenience.h>
#include <rocksdb/db.h>
#include <rocksdb/env.h>
#include <rocksdb/filter_policy.h>
#include <rocksdb/iterator.h>
#include <rocksdb/options.h>
#include <rocksdb/slice.h>
#include <rocksdb/table.h>
#include <rocksdb/write_batch.h>

#include <bit>
#include <cstddef>
#include <functional>
#include <stdexcept>
#include <string>

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

namespace xrpl::NodeStore {

class RocksDBEnv : public rocksdb::EnvWrapper
{
public:
    RocksDBEnv() : EnvWrapper(rocksdb::Env::Default())
    {
    }

    struct ThreadParams
    {
        ThreadParams(void (*f)(void*), void* a) : f(f), a(a)
        {
        }

        void (*f)(void*);
        void* a;
    };

    static void
    threadEntry(void* ptr)
    {
        ThreadParams const* const p(reinterpret_cast<ThreadParams*>(ptr));
        auto const f = p->f;

        void* a(p->a);
        delete p;

        static std::atomic<std::size_t> kN;
        std::size_t const id(++kN);
        beast::setCurrentThreadName("rocksdb #" + std::to_string(id));

        f(a);
    }

    void
    StartThread(void (*f)(void*), void* a) override
    {
        ThreadParams* const p(new ThreadParams(f, a));
        EnvWrapper::StartThread(&RocksDBEnv::threadEntry, p);
    }
};

//------------------------------------------------------------------------------

class RocksDBBackend : public Backend, public BatchWriter::Callback
{
private:
    std::atomic<bool> deletePath_;

public:
    beast::Journal journal;
    size_t const keyBytes;
    BatchWriter batch;
    std::string name;
    std::unique_ptr<rocksdb::DB> db;
    int fdMinRequired = 2048;
    rocksdb::Options options;

    RocksDBBackend(
        int keyBytes,
        Section const& keyValues,
        Scheduler& scheduler,
        beast::Journal journal,
        RocksDBEnv* env)
        : deletePath_(false), journal(journal), keyBytes(keyBytes), batch(*this, scheduler)
    {
        if (!getIfExists(keyValues, Keys::kPath, name))
            Throw<std::runtime_error>("Missing path in RocksDBFactory backend");

        rocksdb::BlockBasedTableOptions tableOptions;
        options.env = env;

        bool const hardSet =
            keyValues.exists(Keys::kHardSet) && get<bool>(keyValues, Keys::kHardSet);

        if (keyValues.exists(Keys::kCacheMb))
        {
            auto size = get<int>(keyValues, Keys::kCacheMb);

            if (!hardSet && size == 256)
                size = 1024;

            tableOptions.block_cache = rocksdb::NewLRUCache(megabytes(size));
        }

        if (auto const v = get<int>(keyValues, Keys::kFilterBits))
        {
            bool const filterBlocks = !keyValues.exists(Keys::kFilterFull) ||
                (get<int>(keyValues, Keys::kFilterFull) == 0);
            tableOptions.filter_policy.reset(rocksdb::NewBloomFilterPolicy(v, filterBlocks));
        }

        if (getIfExists(keyValues, Keys::kOpenFiles, options.max_open_files))
        {
            if (!hardSet && options.max_open_files == 2000)
                options.max_open_files = 8000;

            fdMinRequired = options.max_open_files + 128;
        }

        if (keyValues.exists(Keys::kFileSizeMb))
        {
            auto fileSizeMb = get<int>(keyValues, Keys::kFileSizeMb);

            if (!hardSet && fileSizeMb == 8)
                fileSizeMb = 256;

            options.target_file_size_base = megabytes(fileSizeMb);
            options.max_bytes_for_level_base = 5 * options.target_file_size_base;
            options.write_buffer_size = 2 * options.target_file_size_base;
        }

        getIfExists(keyValues, Keys::kFileSizeMult, options.target_file_size_multiplier);

        if (keyValues.exists(Keys::kBgThreads))
        {
            options.env->SetBackgroundThreads(
                get<int>(keyValues, Keys::kBgThreads), rocksdb::Env::LOW);
        }

        if (keyValues.exists(Keys::kHighThreads))
        {
            auto const highThreads = get<int>(keyValues, Keys::kHighThreads);
            options.env->SetBackgroundThreads(highThreads, rocksdb::Env::HIGH);

            // If we have high-priority threads, presumably we want to
            // use them for background flushes
            if (highThreads > 0)
                options.max_background_flushes = highThreads;
        }

        options.compression = rocksdb::kSnappyCompression;

        getIfExists(keyValues, Keys::kBlockSize, tableOptions.block_size);

        if (keyValues.exists(Keys::kUniversalCompaction) &&
            (get<int>(keyValues, Keys::kUniversalCompaction) != 0))
        {
            options.compaction_style = rocksdb::kCompactionStyleUniversal;
            options.min_write_buffer_number_to_merge = 2;
            options.max_write_buffer_number = 6;
            options.write_buffer_size = 6 * options.target_file_size_base;
        }

        if (keyValues.exists(Keys::kBbtOptions))
        {
            rocksdb::ConfigOptions const configOptions;
            auto const s = rocksdb::GetBlockBasedTableOptionsFromString(
                configOptions, tableOptions, get(keyValues, Keys::kBbtOptions), &tableOptions);
            if (!s.ok())
            {
                Throw<std::runtime_error>(
                    std::string("Unable to set RocksDB bbt_options: ") + s.ToString());
            }
        }

        options.table_factory.reset(NewBlockBasedTableFactory(tableOptions));

        if (keyValues.exists(Keys::kOptions))
        {
            auto const s =
                rocksdb::GetOptionsFromString(options, get(keyValues, Keys::kOptions), &options);
            if (!s.ok())
            {
                Throw<std::runtime_error>(
                    std::string("Unable to set RocksDB options: ") + s.ToString());
            }
        }

        std::string s1, s2;
        rocksdb::GetStringFromDBOptions(&s1, options, "; ");
        rocksdb::GetStringFromColumnFamilyOptions(&s2, options, "; ");
        JLOG(journal.debug()) << "RocksDB DBOptions: " << s1;
        JLOG(journal.debug()) << "RocksDB CFOptions: " << s2;
    }

    ~RocksDBBackend() override
    {
        close();
    }

    void
    open(bool createIfMissing) override
    {
        if (db)
        {
            // LCOV_EXCL_START
            UNREACHABLE(
                "xrpl::NodeStore::RocksDBBackend::open : database is already "
                "open");
            JLOG(journal.error()) << "database is already open";
            return;
            // LCOV_EXCL_STOP
        }
        rocksdb::DB* localDb = nullptr;
        options.create_if_missing = createIfMissing;
        rocksdb::Status const status = rocksdb::DB::Open(options, name, &localDb);
        if (!status.ok() || (localDb == nullptr))
        {
            Throw<std::runtime_error>(
                std::string("Unable to open/create RocksDB: ") + status.ToString());
        }
        db.reset(localDb);
    }

    bool
    isOpen() override
    {
        return static_cast<bool>(db);
    }

    void
    close() override
    {
        if (db)
        {
            db.reset();
            if (deletePath_)
            {
                boost::filesystem::path const dir = name;
                boost::filesystem::remove_all(dir);
            }
        }
    }

    std::string
    getName() override
    {
        return name;
    }

    //--------------------------------------------------------------------------

    Status
    fetch(uint256 const& hash, std::shared_ptr<NodeObject>* pObject) override
    {
        XRPL_ASSERT(db, "xrpl::NodeStore::RocksDBBackend::fetch : non-null database");
        pObject->reset();

        Status status = Status::Ok;

        rocksdb::ReadOptions const options;
        rocksdb::Slice const slice(std::bit_cast<char const*>(hash.data()), keyBytes);

        std::string string;

        rocksdb::Status const getStatus = db->Get(options, slice, &string);

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
                status = Status::DataCorrupt;
            }
        }
        else
        {
            if (getStatus.IsCorruption())
            {
                status = Status::DataCorrupt;
            }
            else if (getStatus.IsNotFound())
            {
                status = Status::NotFound;
            }
            else
            {
                status = static_cast<Status>(
                    static_cast<int>(Status::CustomCode) + unsafeCast<int>(getStatus.code()));

                JLOG(journal.error()) << getStatus.ToString();
            }
        }

        return status;
    }

    void
    store(std::shared_ptr<NodeObject> const& object) override
    {
        batch.store(object);
    }

    void
    storeBatch(Batch const& batch) override
    {
        XRPL_ASSERT(
            db,
            "xrpl::NodeStore::RocksDBBackend::storeBatch : non-null "
            "database");
        rocksdb::WriteBatch wb;

        for (auto const& e : batch)
        {
            EncodedBlob const encoded(e);

            wb.Put(
                rocksdb::Slice(std::bit_cast<char const*>(encoded.getKey()), keyBytes),
                rocksdb::Slice(std::bit_cast<char const*>(encoded.getData()), encoded.getSize()));
        }

        rocksdb::WriteOptions const options;

        auto ret = db->Write(options, &wb);

        if (!ret.ok())
            Throw<std::runtime_error>("storeBatch failed: " + ret.ToString());
    }

    void
    sync() override
    {
    }

    void
    forEach(std::function<void(std::shared_ptr<NodeObject>)> f) override
    {
        XRPL_ASSERT(db, "xrpl::NodeStore::RocksDBBackend::forEach : non-null database");
        rocksdb::ReadOptions const options;

        std::unique_ptr<rocksdb::Iterator> it(db->NewIterator(options));

        for (it->SeekToFirst(); it->Valid(); it->Next())
        {
            if (it->key().size() == keyBytes)
            {
                DecodedBlob decoded(it->key().data(), it->value().data(), it->value().size());

                if (decoded.wasOk())
                {
                    f(decoded.createObject());
                }
                else
                {
                    // Uh oh, corrupted data!
                    JLOG(journal.fatal()) << "Corrupt NodeObject #" << it->key().ToString(true);
                }
            }
            else
            {
                // VFALCO NOTE What does it mean to find an
                //             incorrectly sized key? Corruption?
                JLOG(journal.fatal()) << "Bad key size = " << it->key().size();
            }
        }
    }

    int
    getWriteLoad() override
    {
        return batch.getWriteLoad();
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
    [[nodiscard]] int
    fdRequired() const override
    {
        return fdMinRequired;
    }
};

//------------------------------------------------------------------------------

class RocksDBFactory : public Factory
{
private:
    Manager& manager_;

public:
    RocksDBEnv env;

    RocksDBFactory(Manager& manager) : manager_(manager)
    {
        manager_.insert(*this);
    }

    [[nodiscard]] std::string
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
        return std::make_unique<RocksDBBackend>(keyBytes, keyValues, scheduler, journal, &env);
    }
};

void
registerRocksDBFactory(Manager& manager)
{
    static RocksDBFactory const kInstance{manager};
}

}  // namespace xrpl::NodeStore

#endif
