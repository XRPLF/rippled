//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2017 Ripple Labs Inc.

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

#include <ripple/app/ledger/InboundLedger.h>
#include <ripple/app/main/DBInit.h>
#include <ripple/basics/StringUtilities.h>
#include <ripple/core/ConfigSections.h>
#include <ripple/nodestore/Manager.h>
#include <ripple/nodestore/impl/Shard.h>
#include <ripple/protocol/digest.h>

#include <boost/algorithm/string.hpp>
#include <boost/range/adaptor/transformed.hpp>

namespace ripple {
namespace NodeStore {

uint256 const Shard::finalKey{0};

Shard::Shard(
    Application& app,
    DatabaseShard const& db,
    std::uint32_t index,
    beast::Journal j)
    : Shard(app, db, index, "", j)
{
}

Shard::Shard(
    Application& app,
    DatabaseShard const& db,
    std::uint32_t index,
    boost::filesystem::path const& dir,
    beast::Journal j)
    : app_(app)
    , j_(j)
    , index_(index)
    , firstSeq_(db.firstLedgerSeq(index))
    , lastSeq_(std::max(firstSeq_, db.lastLedgerSeq(index)))
    , maxLedgers_(
          index == db.earliestShardIndex() ? lastSeq_ - firstSeq_ + 1
                                           : db.ledgersPerShard())
    , dir_((dir.empty() ? db.getRootDir() : dir) / std::to_string(index_))
{
}

Shard::~Shard()
{
    if (!removeOnDestroy_)
        return;

    if (backend_)
    {
        // Abort removal if the backend is in use
        if (backendCount_ > 0)
        {
            JLOG(j_.error()) << "shard " << index_
                             << " backend in use, unable to remove directory";
            return;
        }

        // Release database files first otherwise remove_all may fail
        backend_.reset();
        lgrSQLiteDB_.reset();
        txSQLiteDB_.reset();
        acquireInfo_.reset();
    }

    try
    {
        boost::filesystem::remove_all(dir_);
    }
    catch (std::exception const& e)
    {
        JLOG(j_.fatal()) << "shard " << index_
                         << ". Exception caught in function " << __func__
                         << ". Error: " << e.what();
    }
}

bool
Shard::init(Scheduler& scheduler, nudb::context& context)
{
    Section section{app_.config().section(ConfigSection::shardDatabase())};
    std::string const type{get<std::string>(section, "type", "nudb")};
    auto const factory{Manager::instance().find(type)};
    if (!factory)
    {
        JLOG(j_.error()) << "shard " << index_ << " failed to find factory for "
                         << type;
        return false;
    }
    section.set("path", dir_.string());

    std::lock_guard lock{mutex_};
    if (backend_)
    {
        JLOG(j_.error()) << "shard " << index_ << " already initialized";
        return false;
    }
    backend_ = factory->createInstance(
        NodeObject::keyBytes,
        section,
        megabytes(app_.config().getValueFor(SizedItem::burstSize, boost::none)),
        scheduler,
        context,
        j_);

    return open(lock);
}

bool
Shard::isOpen() const
{
    std::lock_guard lock(mutex_);
    if (!backend_)
    {
        JLOG(j_.error()) << "shard " << index_ << " not initialized";
        return false;
    }

    return backend_->isOpen();
}

bool
Shard::tryClose()
{
    // Keep database open if being acquired or finalized
    if (state_ != final)
        return false;

    std::lock_guard lock(mutex_);

    // Keep database open if in use
    if (backendCount_ > 0)
        return false;

    if (!backend_)
    {
        JLOG(j_.error()) << "shard " << index_ << " not initialized";
        return false;
    }
    if (!backend_->isOpen())
        return false;

    try
    {
        backend_->close();
    }
    catch (std::exception const& e)
    {
        JLOG(j_.fatal()) << "shard " << index_
                         << ". Exception caught in function " << __func__
                         << ". Error: " << e.what();
        return false;
    }

    lgrSQLiteDB_.reset();
    txSQLiteDB_.reset();
    acquireInfo_.reset();

    // Reset caches to reduce memory use
    pCache_->reset();
    nCache_->reset();
    app_.getShardFamily()->getFullBelowCache(lastSeq_)->reset();
    app_.getShardFamily()->getTreeNodeCache(lastSeq_)->reset();

    return true;
}

boost::optional<std::uint32_t>
Shard::prepare()
{
    if (state_ != acquire)
    {
        JLOG(j_.warn()) << "shard " << index_
                        << " prepare called when not acquiring";
        return boost::none;
    }

    std::lock_guard lock(mutex_);
    if (!acquireInfo_)
    {
        JLOG(j_.error()) << "shard " << index_
                         << " missing acquire SQLite database";
        return boost::none;
    }

    if (acquireInfo_->storedSeqs.empty())
        return lastSeq_;
    return prevMissing(acquireInfo_->storedSeqs, 1 + lastSeq_, firstSeq_);
}

bool
Shard::storeNodeObject(std::shared_ptr<NodeObject> const& nodeObject)
{
    if (state_ != acquire)
    {
        // The import node store case is an exception
        if (nodeObject->getHash() != finalKey)
        {
            // Ignore residual calls from InboundLedgers
            JLOG(j_.trace()) << "shard " << index_ << " not acquiring";
            return false;
        }
    }

    auto const scopedCount{makeBackendCount()};
    if (!scopedCount)
        return false;

    pCache_->canonicalize_replace_cache(nodeObject->getHash(), nodeObject);

    try
    {
        backend_->store(nodeObject);
    }
    catch (std::exception const& e)
    {
        JLOG(j_.fatal()) << "shard " << index_
                         << ". Exception caught in function " << __func__
                         << ". Error: " << e.what();
        return false;
    }

    nCache_->erase(nodeObject->getHash());
    return true;
}

std::shared_ptr<NodeObject>
Shard::fetchNodeObject(uint256 const& hash, FetchReport& fetchReport)
{
    auto const scopedCount{makeBackendCount()};
    if (!scopedCount)
        return nullptr;

    // See if the node object exists in the cache
    auto nodeObject{pCache_->fetch(hash)};
    if (!nodeObject && !nCache_->touch_if_exists(hash))
    {
        // Try the backend
        fetchReport.wentToDisk = true;

        Status status;
        try
        {
            status = backend_->fetch(hash.data(), &nodeObject);
        }
        catch (std::exception const& e)
        {
            JLOG(j_.fatal())
                << "shard " << index_ << ". Exception caught in function "
                << __func__ << ". Error: " << e.what();
            return nullptr;
        }

        switch (status)
        {
            case ok:
            case notFound:
                break;
            case dataCorrupt: {
                JLOG(j_.fatal())
                    << "shard " << index_ << ". Corrupt node object at hash "
                    << to_string(hash);
                break;
            }
            default: {
                JLOG(j_.warn())
                    << "shard " << index_ << ". Unknown status=" << status
                    << " fetching node object at hash " << to_string(hash);
                break;
            }
        }

        if (!nodeObject)
        {
            // Just in case a write occurred
            nodeObject = pCache_->fetch(hash);
            if (!nodeObject)
                // We give up
                nCache_->insert(hash);
        }
        else
        {
            // Ensure all threads get the same object
            pCache_->canonicalize_replace_client(hash, nodeObject);
            fetchReport.wasFound = true;

            // Since this was a 'hard' fetch, we will log it
            JLOG(j_.trace()) << "HOS: " << hash << " fetch: in shard db";
        }
    }

    return nodeObject;
}

bool
Shard::fetchNodeObjectFromCache(
    uint256 const& hash,
    std::shared_ptr<NodeObject>& nodeObject)
{
    auto const scopedCount{makeBackendCount()};
    if (!scopedCount)
        return false;

    nodeObject = pCache_->fetch(hash);
    if (nodeObject || nCache_->touch_if_exists(hash))
        return true;
    return false;
}

Shard::StoreLedgerResult
Shard::storeLedger(
    std::shared_ptr<Ledger const> const& srcLedger,
    std::shared_ptr<Ledger const> const& next)
{
    StoreLedgerResult result;
    if (state_ != acquire)
    {
        // Ignore residual calls from InboundLedgers
        JLOG(j_.trace()) << "shard " << index_ << ". Not acquiring";
        return result;
    }
    if (containsLedger(srcLedger->info().seq))
    {
        JLOG(j_.trace()) << "shard " << index_ << ". Ledger already stored";
        return result;
    }

    auto fail = [&](std::string const& msg) {
        JLOG(j_.error()) << "shard " << index_ << ". Source ledger sequence "
                         << srcLedger->info().seq << ". " << msg;
        result.error = true;
        return result;
    };

    if (srcLedger->info().hash.isZero())
        return fail("Invalid hash");
    if (srcLedger->info().accountHash.isZero())
        return fail("Invalid account hash");

    auto& srcDB{const_cast<Database&>(srcLedger->stateMap().family().db())};
    if (&srcDB == &(app_.getShardFamily()->db()))
        return fail("Source and destination databases are the same");

    auto const scopedCount{makeBackendCount()};
    if (!scopedCount)
        return fail("Failed to lock backend");

    Batch batch;
    batch.reserve(batchWritePreallocationSize);
    auto storeBatch = [&]() {
        std::uint64_t sz{0};
        for (auto const& nodeObject : batch)
        {
            pCache_->canonicalize_replace_cache(
                nodeObject->getHash(), nodeObject);
            nCache_->erase(nodeObject->getHash());
            sz += nodeObject->getData().size();
        }

        try
        {
            backend_->storeBatch(batch);
        }
        catch (std::exception const& e)
        {
            fail(
                std::string(". Exception caught in function ") + __func__ +
                ". Error: " + e.what());
            return false;
        }

        result.count += batch.size();
        result.size += sz;
        batch.clear();
        return true;
    };

    // Store ledger header
    {
        Serializer s(sizeof(std::uint32_t) + sizeof(LedgerInfo));
        s.add32(HashPrefix::ledgerMaster);
        addRaw(srcLedger->info(), s);
        auto nodeObject = NodeObject::createObject(
            hotLEDGER, std::move(s.modData()), srcLedger->info().hash);
        batch.emplace_back(std::move(nodeObject));
    }

    bool error = false;
    auto visit = [&](SHAMapTreeNode const& node) {
        if (!stop_)
        {
            if (auto nodeObject = srcDB.fetchNodeObject(
                    node.getHash().as_uint256(), srcLedger->info().seq))
            {
                batch.emplace_back(std::move(nodeObject));
                if (batch.size() < batchWritePreallocationSize || storeBatch())
                    return true;
            }
        }

        error = true;
        return false;
    };

    // Store the state map
    if (srcLedger->stateMap().getHash().isNonZero())
    {
        if (!srcLedger->stateMap().isValid())
            return fail("Invalid state map");

        if (next && next->info().parentHash == srcLedger->info().hash)
        {
            auto have = next->stateMap().snapShot(false);
            srcLedger->stateMap().snapShot(false)->visitDifferences(
                &(*have), visit);
        }
        else
            srcLedger->stateMap().snapShot(false)->visitNodes(visit);
        if (error)
            return fail("Failed to store state map");
    }

    // Store the transaction map
    if (srcLedger->info().txHash.isNonZero())
    {
        if (!srcLedger->txMap().isValid())
            return fail("Invalid transaction map");

        srcLedger->txMap().snapShot(false)->visitNodes(visit);
        if (error)
            return fail("Failed to store transaction map");
    }

    if (!batch.empty() && !storeBatch())
        return fail("Failed to store");

    return result;
}

bool
Shard::setLedgerStored(std::shared_ptr<Ledger const> const& ledger)
{
    if (state_ != acquire)
    {
        // Ignore residual calls from InboundLedgers
        JLOG(j_.trace()) << "shard " << index_ << " not acquiring";
        return false;
    }

    auto const ledgerSeq{ledger->info().seq};
    if (ledgerSeq < firstSeq_ || ledgerSeq > lastSeq_)
    {
        JLOG(j_.error()) << "shard " << index_ << " invalid ledger sequence "
                         << ledgerSeq;
        return false;
    }

    std::lock_guard lock(mutex_);
    if (!acquireInfo_)
    {
        JLOG(j_.error()) << "shard " << index_
                         << " missing acquire SQLite database";
        return false;
    }
    if (boost::icl::contains(acquireInfo_->storedSeqs, ledgerSeq))
    {
        // Ignore redundant calls
        JLOG(j_.debug()) << "shard " << index_ << " ledger sequence "
                         << ledgerSeq << " already stored";
        return true;
    }
    // storeSQLite looks at storedSeqs so insert before the call
    acquireInfo_->storedSeqs.insert(ledgerSeq);

    if (!storeSQLite(ledger, lock))
        return false;

    if (boost::icl::length(acquireInfo_->storedSeqs) >= maxLedgers_)
    {
        if (!initSQLite(lock))
            return false;

        state_ = complete;
    }

    JLOG(j_.debug()) << "shard " << index_ << " stored ledger sequence "
                     << ledgerSeq;

    setFileStats(lock);
    return true;
}

bool
Shard::containsLedger(std::uint32_t ledgerSeq) const
{
    if (ledgerSeq < firstSeq_ || ledgerSeq > lastSeq_)
        return false;
    if (state_ != acquire)
        return true;

    std::lock_guard lock(mutex_);
    if (!acquireInfo_)
    {
        JLOG(j_.error()) << "shard " << index_
                         << " missing acquire SQLite database";
        return false;
    }
    return boost::icl::contains(acquireInfo_->storedSeqs, ledgerSeq);
}

void
Shard::sweep()
{
    boost::optional<Shard::Count> scopedCount;
    {
        std::lock_guard lock(mutex_);
        if (!backend_ || !backend_->isOpen())
        {
            JLOG(j_.error()) << "shard " << index_ << " not initialized";
            return;
        }

        scopedCount.emplace(&backendCount_);
    }

    pCache_->sweep();
    nCache_->sweep();
}

int
Shard::getDesiredAsyncReadCount()
{
    auto const scopedCount{makeBackendCount()};
    if (!scopedCount)
        return 0;
    return pCache_->getTargetSize() / asyncDivider;
}

float
Shard::getCacheHitRate()
{
    auto const scopedCount{makeBackendCount()};
    if (!scopedCount)
        return 0;
    return pCache_->getHitRate();
}

std::chrono::steady_clock::time_point
Shard::getLastUse() const
{
    std::lock_guard lock(mutex_);
    return lastAccess_;
}

std::pair<std::uint64_t, std::uint32_t>
Shard::getFileInfo() const
{
    std::lock_guard lock(mutex_);
    return {fileSz_, fdRequired_};
}

std::int32_t
Shard::getWriteLoad()
{
    auto const scopedCount{makeBackendCount()};
    if (!scopedCount)
        return 0;
    return backend_->getWriteLoad();
}

bool
Shard::isLegacy() const
{
    std::lock_guard lock(mutex_);
    return legacy_;
}

bool
Shard::finalize(
    bool const writeSQLite,
    boost::optional<uint256> const& expectedHash)
{
    uint256 hash{0};
    std::uint32_t ledgerSeq{0};
    auto fail =
        [j = j_, index = index_, &hash, &ledgerSeq](std::string const& msg) {
            JLOG(j.fatal())
                << "shard " << index << ". " << msg
                << (hash.isZero() ? "" : ". Ledger hash " + to_string(hash))
                << (ledgerSeq == 0
                        ? ""
                        : ". Ledger sequence " + std::to_string(ledgerSeq));
            return false;
        };

    auto const scopedCount{makeBackendCount()};
    if (!scopedCount)
        return false;

    try
    {
        state_ = finalizing;

        /*
        TODO MP
        A lock is required when calling the NuDB verify function. Because
        this can be a time consuming process, the server may desync.
        Until this function is modified to work on an open database, we
        are unable to use it from rippled.

        // Verify backend integrity
        backend_->verify();
        */

        // Check if a final key has been stored
        if (std::shared_ptr<NodeObject> nodeObject;
            backend_->fetch(finalKey.data(), &nodeObject) == Status::ok)
        {
            // Check final key's value
            SerialIter sIt(
                nodeObject->getData().data(), nodeObject->getData().size());
            if (sIt.get32() != version)
                return fail("invalid version");

            if (sIt.get32() != firstSeq_ || sIt.get32() != lastSeq_)
                return fail("out of range ledger sequences");

            if (hash = sIt.get256(); hash.isZero())
                return fail("invalid last ledger hash");
        }
        else
        {
            // In the absence of a final key, an acquire SQLite database
            // must be present in order to validate the shard
            if (!acquireInfo_)
                return fail("missing acquire SQLite database");

            auto& session{acquireInfo_->SQLiteDB->getSession()};
            boost::optional<std::uint32_t> index;
            boost::optional<std::string> sHash;
            soci::blob sociBlob(session);
            soci::indicator blobPresent;
            session << "SELECT ShardIndex, LastLedgerHash, StoredLedgerSeqs "
                       "FROM Shard "
                       "WHERE ShardIndex = :index;",
                soci::into(index), soci::into(sHash),
                soci::into(sociBlob, blobPresent), soci::use(index_);

            if (!index || index != index_)
                return fail("missing or invalid ShardIndex");

            if (!sHash)
                return fail("missing LastLedgerHash");

            if (!hash.parseHex(*sHash) || hash.isZero())
                return fail("invalid LastLedgerHash");

            if (blobPresent != soci::i_ok)
                return fail("missing StoredLedgerSeqs");

            std::string s;
            convert(sociBlob, s);

            auto& storedSeqs{acquireInfo_->storedSeqs};
            if (!from_string(storedSeqs, s) ||
                boost::icl::first(storedSeqs) != firstSeq_ ||
                boost::icl::last(storedSeqs) != lastSeq_ ||
                storedSeqs.size() != maxLedgers_)
            {
                return fail("invalid StoredLedgerSeqs");
            }
        }
    }
    catch (std::exception const& e)
    {
        return fail(
            std::string(". Exception caught in function ") + __func__ +
            ". Error: " + e.what());
    }

    // Validate the last ledger hash of a downloaded shard
    // using a ledger hash obtained from the peer network
    if (expectedHash && *expectedHash != hash)
        return fail("invalid last ledger hash");

    // Validate every ledger stored in the backend
    Config const& config{app_.config()};
    std::shared_ptr<Ledger> ledger;
    std::shared_ptr<Ledger const> next;
    auto const lastLedgerHash{hash};
    auto& shardFamily{*app_.getShardFamily()};
    auto const fullBelowCache{shardFamily.getFullBelowCache(lastSeq_)};
    auto const treeNodeCache{shardFamily.getTreeNodeCache(lastSeq_)};

    // Reset caches to reduce memory usage
    pCache_->reset();
    nCache_->reset();
    fullBelowCache->reset();
    treeNodeCache->reset();

    // Start with the last ledger in the shard and walk backwards from
    // child to parent until we reach the first ledger
    ledgerSeq = lastSeq_;
    while (ledgerSeq >= firstSeq_)
    {
        if (stop_)
            return false;

        auto nodeObject{verifyFetch(hash)};
        if (!nodeObject)
            return fail("invalid ledger");

        ledger = std::make_shared<Ledger>(
            deserializePrefixedHeader(makeSlice(nodeObject->getData())),
            config,
            shardFamily);
        if (ledger->info().seq != ledgerSeq)
            return fail("invalid ledger sequence");
        if (ledger->info().hash != hash)
            return fail("invalid ledger hash");

        ledger->stateMap().setLedgerSeq(ledgerSeq);
        ledger->txMap().setLedgerSeq(ledgerSeq);
        ledger->setImmutable(config);
        if (!ledger->stateMap().fetchRoot(
                SHAMapHash{ledger->info().accountHash}, nullptr))
        {
            return fail("missing root STATE node");
        }
        if (ledger->info().txHash.isNonZero() &&
            !ledger->txMap().fetchRoot(
                SHAMapHash{ledger->info().txHash}, nullptr))
        {
            return fail("missing root TXN node");
        }

        if (!verifyLedger(ledger, next))
            return fail("failed to validate ledger");

        if (writeSQLite)
        {
            std::lock_guard lock(mutex_);
            if (!storeSQLite(ledger, lock))
                return fail("failed storing to SQLite databases");
        }

        hash = ledger->info().parentHash;
        next = std::move(ledger);
        --ledgerSeq;

        pCache_->reset();
        nCache_->reset();
        fullBelowCache->reset();
        treeNodeCache->reset();
    }

    JLOG(j_.debug()) << "shard " << index_ << " is valid";

    /*
    TODO MP
    SQLite VACUUM blocks all database access while processing.
    Depending on the file size, that can take a while. Until we find
    a non-blocking way of doing this, we cannot enable vacuum as
    it can desync a server.

    try
    {
        // VACUUM the SQLite databases
        auto const tmpDir {dir_ / "tmp_vacuum"};
        create_directory(tmpDir);

        auto vacuum = [&tmpDir](std::unique_ptr<DatabaseCon>& sqliteDB)
        {
            auto& session {sqliteDB->getSession()};
            session << "PRAGMA synchronous=OFF;";
            session << "PRAGMA journal_mode=OFF;";
            session << "PRAGMA temp_store_directory='" <<
                tmpDir.string() << "';";
            session << "VACUUM;";
        };
        vacuum(lgrSQLiteDB_);
        vacuum(txSQLiteDB_);
        remove_all(tmpDir);
    }
    catch (std::exception const& e)
    {
        return fail(
            std::string(". Exception caught in function ") + __func__ +
            ". Error: " + e.what());
    }
    */

    // Store final key's value, may already be stored
    Serializer s;
    s.add32(version);
    s.add32(firstSeq_);
    s.add32(lastSeq_);
    s.addBitString(lastLedgerHash);
    auto nodeObject{
        NodeObject::createObject(hotUNKNOWN, std::move(s.modData()), finalKey)};
    try
    {
        backend_->store(nodeObject);

        std::lock_guard lock(mutex_);

        // Remove the acquire SQLite database
        if (acquireInfo_)
        {
            acquireInfo_.reset();
            remove_all(dir_ / AcquireShardDBName);
        }

        if (!initSQLite(lock))
            return fail("failed to initialize SQLite databases");

        setFileStats(lock);
        lastAccess_ = std::chrono::steady_clock::now();
    }
    catch (std::exception const& e)
    {
        return fail(
            std::string(". Exception caught in function ") + __func__ +
            ". Error: " + e.what());
    }

    state_ = final;
    return true;
}

bool
Shard::open(std::lock_guard<std::mutex> const& lock)
{
    using namespace boost::filesystem;
    Config const& config{app_.config()};
    auto preexist{false};
    auto fail = [this, &preexist](std::string const& msg) {
        backend_->close();
        lgrSQLiteDB_.reset();
        txSQLiteDB_.reset();
        acquireInfo_.reset();

        pCache_.reset();
        nCache_.reset();

        state_ = acquire;

        if (!preexist)
            remove_all(dir_);

        if (!msg.empty())
        {
            JLOG(j_.fatal()) << "shard " << index_ << " " << msg;
        }
        return false;
    };
    auto createAcquireInfo = [this, &config]() {
        acquireInfo_ = std::make_unique<AcquireInfo>();

        DatabaseCon::Setup setup;
        setup.startUp = config.standalone() ? config.LOAD : config.START_UP;
        setup.standAlone = config.standalone();
        setup.dataDir = dir_;
        setup.useGlobalPragma = true;

        acquireInfo_->SQLiteDB = std::make_unique<DatabaseCon>(
            setup,
            AcquireShardDBName,
            AcquireShardDBPragma,
            AcquireShardDBInit,
            DatabaseCon::CheckpointerSetup{&app_.getJobQueue(), &app_.logs()});
        state_ = acquire;
    };

    try
    {
        // Open or create the NuDB key/value store
        preexist = exists(dir_);
        backend_->open(!preexist);

        if (!preexist)
        {
            // A new shard
            createAcquireInfo();
            acquireInfo_->SQLiteDB->getSession()
                << "INSERT INTO Shard (ShardIndex) "
                   "VALUES (:shardIndex);",
                soci::use(index_);
        }
        else if (exists(dir_ / AcquireShardDBName))
        {
            // A shard being acquired, backend is likely incomplete
            createAcquireInfo();

            auto& session{acquireInfo_->SQLiteDB->getSession()};
            boost::optional<std::uint32_t> index;
            soci::blob sociBlob(session);
            soci::indicator blobPresent;

            session << "SELECT ShardIndex, StoredLedgerSeqs "
                       "FROM Shard "
                       "WHERE ShardIndex = :index;",
                soci::into(index), soci::into(sociBlob, blobPresent),
                soci::use(index_);

            if (!index || index != index_)
                return fail("invalid acquire SQLite database");

            if (blobPresent == soci::i_ok)
            {
                std::string s;
                auto& storedSeqs{acquireInfo_->storedSeqs};
                if (convert(sociBlob, s); !from_string(storedSeqs, s))
                    return fail("invalid StoredLedgerSeqs");

                if (boost::icl::first(storedSeqs) < firstSeq_ ||
                    boost::icl::last(storedSeqs) > lastSeq_)
                {
                    return fail("invalid StoredLedgerSeqs");
                }

                // Check if backend is complete
                if (boost::icl::length(storedSeqs) == maxLedgers_)
                    state_ = complete;
            }
        }
        else
        {
            // A shard that is final or its backend is complete
            // and ready to be finalized
            std::shared_ptr<NodeObject> nodeObject;
            if (backend_->fetch(finalKey.data(), &nodeObject) != Status::ok)
            {
                legacy_ = true;
                return fail("incompatible, missing backend final key");
            }

            // Check final key's value
            SerialIter sIt(
                nodeObject->getData().data(), nodeObject->getData().size());
            if (sIt.get32() != version)
                return fail("invalid version");

            if (sIt.get32() != firstSeq_ || sIt.get32() != lastSeq_)
                return fail("out of range ledger sequences");

            if (sIt.get256().isZero())
                return fail("invalid last ledger hash");

            if (exists(dir_ / LgrDBName) && exists(dir_ / TxDBName))
            {
                lastAccess_ = std::chrono::steady_clock::now();
                state_ = final;
            }
            else
                state_ = complete;
        }
    }
    catch (std::exception const& e)
    {
        return fail(
            std::string(". Exception caught in function ") + __func__ +
            ". Error: " + e.what());
    }

    // Set backend caches
    auto const size{config.getValueFor(SizedItem::nodeCacheSize, 0)};
    auto const age{
        std::chrono::seconds{config.getValueFor(SizedItem::nodeCacheAge, 0)}};
    auto const name{"shard " + std::to_string(index_)};
    pCache_ = std::make_unique<PCache>(name, size, age, stopwatch(), j_);
    nCache_ = std::make_unique<NCache>(name, stopwatch(), size, age);

    if (!initSQLite(lock))
        return fail({});

    setFileStats(lock);
    return true;
}

bool
Shard::initSQLite(std::lock_guard<std::mutex> const&)
{
    Config const& config{app_.config()};
    DatabaseCon::Setup const setup = [&]() {
        DatabaseCon::Setup setup;
        setup.startUp = config.standalone() ? config.LOAD : config.START_UP;
        setup.standAlone = config.standalone();
        setup.dataDir = dir_;
        setup.useGlobalPragma = (state_ != complete);
        return setup;
    }();

    try
    {
        if (lgrSQLiteDB_)
            lgrSQLiteDB_.reset();

        if (txSQLiteDB_)
            txSQLiteDB_.reset();

        if (state_ != acquire)
        {
            lgrSQLiteDB_ = std::make_unique<DatabaseCon>(
                setup, LgrDBName, CompleteShardDBPragma, LgrDBInit);
            lgrSQLiteDB_->getSession() << boost::str(
                boost::format("PRAGMA cache_size=-%d;") %
                kilobytes(
                    config.getValueFor(SizedItem::lgrDBCache, boost::none)));

            txSQLiteDB_ = std::make_unique<DatabaseCon>(
                setup, TxDBName, CompleteShardDBPragma, TxDBInit);
            txSQLiteDB_->getSession() << boost::str(
                boost::format("PRAGMA cache_size=-%d;") %
                kilobytes(
                    config.getValueFor(SizedItem::txnDBCache, boost::none)));
        }
        else
        {
            // The incomplete shard uses a Write Ahead Log for performance
            lgrSQLiteDB_ = std::make_unique<DatabaseCon>(
                setup,
                LgrDBName,
                LgrDBPragma,
                LgrDBInit,
                DatabaseCon::CheckpointerSetup{
                    &app_.getJobQueue(), &app_.logs()});
            lgrSQLiteDB_->getSession() << boost::str(
                boost::format("PRAGMA cache_size=-%d;") %
                kilobytes(config.getValueFor(SizedItem::lgrDBCache)));

            txSQLiteDB_ = std::make_unique<DatabaseCon>(
                setup,
                TxDBName,
                TxDBPragma,
                TxDBInit,
                DatabaseCon::CheckpointerSetup{
                    &app_.getJobQueue(), &app_.logs()});
            txSQLiteDB_->getSession() << boost::str(
                boost::format("PRAGMA cache_size=-%d;") %
                kilobytes(config.getValueFor(SizedItem::txnDBCache)));
        }
    }
    catch (std::exception const& e)
    {
        JLOG(j_.fatal()) << "shard " << index_
                         << ". Exception caught in function " << __func__
                         << ". Error: " << e.what();
        return false;
    }

    return true;
}

bool
Shard::storeSQLite(
    std::shared_ptr<Ledger const> const& ledger,
    std::lock_guard<std::mutex> const&)
{
    if (stop_)
        return false;

    auto const ledgerSeq{ledger->info().seq};

    try
    {
        // Update the transactions database
        {
            auto& session{txSQLiteDB_->getSession()};
            soci::transaction tr(session);

            session << "DELETE FROM Transactions "
                       "WHERE LedgerSeq = :seq;",
                soci::use(ledgerSeq);
            session << "DELETE FROM AccountTransactions "
                       "WHERE LedgerSeq = :seq;",
                soci::use(ledgerSeq);

            if (ledger->info().txHash.isNonZero())
            {
                auto const sSeq{std::to_string(ledgerSeq)};
                if (!ledger->txMap().isValid())
                {
                    JLOG(j_.error()) << "shard " << index_
                                     << " has an invalid transaction map"
                                     << " on sequence " << sSeq;
                    return false;
                }

                for (auto const& item : ledger->txs)
                {
                    if (stop_)
                        return false;

                    auto const txID{item.first->getTransactionID()};
                    auto const sTxID{to_string(txID)};
                    auto const txMeta{std::make_shared<TxMeta>(
                        txID, ledger->seq(), *item.second)};

                    session << "DELETE FROM AccountTransactions "
                               "WHERE TransID = :txID;",
                        soci::use(sTxID);

                    auto const& accounts = txMeta->getAffectedAccounts(j_);
                    if (!accounts.empty())
                    {
                        auto const sTxnSeq{std::to_string(txMeta->getIndex())};
                        auto const s{boost::str(
                            boost::format("('%s','%s',%s,%s)") % sTxID % "%s" %
                            sSeq % sTxnSeq)};
                        std::string sql;
                        sql.reserve((accounts.size() + 1) * 128);
                        sql =
                            "INSERT INTO AccountTransactions "
                            "(TransID, Account, LedgerSeq, TxnSeq) VALUES ";
                        sql += boost::algorithm::join(
                            accounts |
                                boost::adaptors::transformed(
                                    [&](AccountID const& accountID) {
                                        return boost::str(
                                            boost::format(s) %
                                            ripple::toBase58(accountID));
                                    }),
                            ",");
                        sql += ';';
                        session << sql;

                        JLOG(j_.trace()) << "shard " << index_
                                         << " account transaction: " << sql;
                    }
                    else
                    {
                        JLOG(j_.warn())
                            << "shard " << index_ << " transaction in ledger "
                            << sSeq << " affects no accounts";
                    }

                    Serializer s;
                    item.second->add(s);
                    session
                        << (STTx::getMetaSQLInsertReplaceHeader() +
                            item.first->getMetaSQL(
                                ledgerSeq, sqlBlobLiteral(s.modData())) +
                            ';');
                }
            }

            tr.commit();
        }

        auto const sHash{to_string(ledger->info().hash)};

        // Update the ledger database
        {
            auto& session{lgrSQLiteDB_->getSession()};
            soci::transaction tr(session);

            auto const sParentHash{to_string(ledger->info().parentHash)};
            auto const sDrops{to_string(ledger->info().drops)};
            auto const sAccountHash{to_string(ledger->info().accountHash)};
            auto const sTxHash{to_string(ledger->info().txHash)};

            session << "DELETE FROM Ledgers "
                       "WHERE LedgerSeq = :seq;",
                soci::use(ledgerSeq);
            session
                << "INSERT OR REPLACE INTO Ledgers ("
                   "LedgerHash, LedgerSeq, PrevHash, TotalCoins, ClosingTime,"
                   "PrevClosingTime, CloseTimeRes, CloseFlags, AccountSetHash,"
                   "TransSetHash)"
                   "VALUES ("
                   ":ledgerHash, :ledgerSeq, :prevHash, :totalCoins,"
                   ":closingTime, :prevClosingTime, :closeTimeRes,"
                   ":closeFlags, :accountSetHash, :transSetHash);",
                soci::use(sHash), soci::use(ledgerSeq), soci::use(sParentHash),
                soci::use(sDrops),
                soci::use(ledger->info().closeTime.time_since_epoch().count()),
                soci::use(
                    ledger->info().parentCloseTime.time_since_epoch().count()),
                soci::use(ledger->info().closeTimeResolution.count()),
                soci::use(ledger->info().closeFlags), soci::use(sAccountHash),
                soci::use(sTxHash);

            tr.commit();
        }

        // Update the acquire database if present
        if (acquireInfo_)
        {
            auto& session{acquireInfo_->SQLiteDB->getSession()};
            soci::blob sociBlob(session);

            if (!acquireInfo_->storedSeqs.empty())
                convert(to_string(acquireInfo_->storedSeqs), sociBlob);

            if (ledger->info().seq == lastSeq_)
            {
                // Store shard's last ledger hash
                session << "UPDATE Shard "
                           "SET LastLedgerHash = :lastLedgerHash,"
                           "StoredLedgerSeqs = :storedLedgerSeqs "
                           "WHERE ShardIndex = :shardIndex;",
                    soci::use(sHash), soci::use(sociBlob), soci::use(index_);
            }
            else
            {
                session << "UPDATE Shard "
                           "SET StoredLedgerSeqs = :storedLedgerSeqs "
                           "WHERE ShardIndex = :shardIndex;",
                    soci::use(sociBlob), soci::use(index_);
            }
        }
    }
    catch (std::exception const& e)
    {
        JLOG(j_.fatal()) << "shard " << index_
                         << ". Exception caught in function " << __func__
                         << ". Error: " << e.what();
        return false;
    }

    return true;
}

void
Shard::setFileStats(std::lock_guard<std::mutex> const&)
{
    fileSz_ = 0;
    fdRequired_ = 0;
    try
    {
        using namespace boost::filesystem;
        for (auto const& d : directory_iterator(dir_))
        {
            if (is_regular_file(d))
            {
                fileSz_ += file_size(d);
                ++fdRequired_;
            }
        }
    }
    catch (std::exception const& e)
    {
        JLOG(j_.fatal()) << "shard " << index_
                         << ". Exception caught in function " << __func__
                         << ". Error: " << e.what();
    }
}

bool
Shard::verifyLedger(
    std::shared_ptr<Ledger const> const& ledger,
    std::shared_ptr<Ledger const> const& next) const
{
    auto fail = [j = j_, index = index_, &ledger](std::string const& msg) {
        JLOG(j.error()) << "shard " << index << ". " << msg
                        << (ledger->info().hash.isZero() ? ""
                                                         : ". Ledger hash " +
                                    to_string(ledger->info().hash))
                        << (ledger->info().seq == 0 ? ""
                                                    : ". Ledger sequence " +
                                    std::to_string(ledger->info().seq));
        return false;
    };

    if (ledger->info().hash.isZero())
        return fail("Invalid ledger hash");
    if (ledger->info().accountHash.isZero())
        return fail("Invalid ledger account hash");

    bool error{false};
    auto visit = [this, &error](SHAMapTreeNode const& node) {
        if (stop_)
            return false;
        if (!verifyFetch(node.getHash().as_uint256()))
            error = true;
        return !error;
    };

    // Validate the state map
    if (ledger->stateMap().getHash().isNonZero())
    {
        if (!ledger->stateMap().isValid())
            return fail("Invalid state map");

        try
        {
            if (next && next->info().parentHash == ledger->info().hash)
                ledger->stateMap().visitDifferences(&next->stateMap(), visit);
            else
                ledger->stateMap().visitNodes(visit);
        }
        catch (std::exception const& e)
        {
            return fail(
                std::string(". Exception caught in function ") + __func__ +
                ". Error: " + e.what());
        }

        if (stop_)
            return false;
        if (error)
            return fail("Invalid state map");
    }

    // Validate the transaction map
    if (ledger->info().txHash.isNonZero())
    {
        if (!ledger->txMap().isValid())
            return fail("Invalid transaction map");

        try
        {
            ledger->txMap().visitNodes(visit);
        }
        catch (std::exception const& e)
        {
            return fail(
                std::string(". Exception caught in function ") + __func__ +
                ". Error: " + e.what());
        }
        if (stop_)
            return false;
        if (error)
            return fail("Invalid transaction map");
    }

    return true;
}

std::shared_ptr<NodeObject>
Shard::verifyFetch(uint256 const& hash) const
{
    std::shared_ptr<NodeObject> nodeObject;
    auto fail =
        [j = j_, index = index_, &hash, &nodeObject](std::string const& msg) {
            JLOG(j.error()) << "shard " << index << ". " << msg
                            << ". Node object hash " << to_string(hash);
            nodeObject.reset();
            return nodeObject;
        };

    try
    {
        switch (backend_->fetch(hash.data(), &nodeObject))
        {
            case ok:
                // Verify that the hash of node object matches the payload
                if (nodeObject->getHash() !=
                    sha512Half(makeSlice(nodeObject->getData())))
                    return fail("Node object hash does not match payload");
                return nodeObject;
            case notFound:
                return fail("Missing node object");
            case dataCorrupt:
                return fail("Corrupt node object");
            default:
                return fail("Unknown error");
        }
    }
    catch (std::exception const& e)
    {
        return fail(
            std::string(". Exception caught in function ") + __func__ +
            ". Error: " + e.what());
    }
}

Shard::Count
Shard::makeBackendCount()
{
    if (stop_)
        return {nullptr};

    std::lock_guard lock(mutex_);
    if (!backend_)
    {
        JLOG(j_.error()) << "shard " << index_ << " not initialized";
        return {nullptr};
    }
    if (!backend_->isOpen())
    {
        if (!open(lock))
            return {nullptr};
    }
    else if (state_ == final)
        lastAccess_ = std::chrono::steady_clock::now();

    return Shard::Count(&backendCount_);
}

}  // namespace NodeStore
}  // namespace ripple
