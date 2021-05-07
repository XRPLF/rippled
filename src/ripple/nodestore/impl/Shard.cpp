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
#include <ripple/app/rdb/RelationalDBInterface_global.h>
#include <ripple/app/rdb/RelationalDBInterface_shards.h>
#include <ripple/basics/StringUtilities.h>
#include <ripple/core/ConfigSections.h>
#include <ripple/nodestore/Manager.h>
#include <ripple/nodestore/impl/DeterministicShard.h>
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
    , maxLedgers_(db.maxLedgers(index))
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
    std::string const type{get(section, "type", "nudb")};
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
        megabytes(
            app_.config().getValueFor(SizedItem::burstSize, std::nullopt)),
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
    if (state_ != ShardState::finalized)
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
    app_.getShardFamily()->getFullBelowCache(lastSeq_)->reset();
    app_.getShardFamily()->getTreeNodeCache(lastSeq_)->reset();

    return true;
}

std::optional<std::uint32_t>
Shard::prepare()
{
    if (state_ != ShardState::acquire)
    {
        JLOG(j_.warn()) << "shard " << index_
                        << " prepare called when not acquiring";
        return std::nullopt;
    }

    std::lock_guard lock(mutex_);
    if (!acquireInfo_)
    {
        JLOG(j_.error()) << "shard " << index_
                         << " missing acquire SQLite database";
        return std::nullopt;
    }

    if (acquireInfo_->storedSeqs.empty())
        return lastSeq_;
    return prevMissing(acquireInfo_->storedSeqs, 1 + lastSeq_, firstSeq_);
}

bool
Shard::storeNodeObject(std::shared_ptr<NodeObject> const& nodeObject)
{
    if (state_ != ShardState::acquire)
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

    return true;
}

std::shared_ptr<NodeObject>
Shard::fetchNodeObject(uint256 const& hash, FetchReport& fetchReport)
{
    auto const scopedCount{makeBackendCount()};
    if (!scopedCount)
        return nullptr;

    std::shared_ptr<NodeObject> nodeObject;

    // Try the backend
    Status status;
    try
    {
        status = backend_->fetch(hash.data(), &nodeObject);
    }
    catch (std::exception const& e)
    {
        JLOG(j_.fatal()) << "shard " << index_
                         << ". Exception caught in function " << __func__
                         << ". Error: " << e.what();
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

    if (nodeObject)
        fetchReport.wasFound = true;

    return nodeObject;
}

Shard::StoreLedgerResult
Shard::storeLedger(
    std::shared_ptr<Ledger const> const& srcLedger,
    std::shared_ptr<Ledger const> const& next)
{
    StoreLedgerResult result;
    if (state_ != ShardState::acquire)
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
            sz += nodeObject->getData().size();

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
    if (state_ != ShardState::acquire)
    {
        // Ignore residual calls from InboundLedgers
        JLOG(j_.trace()) << "shard " << index_ << " not acquiring";
        return false;
    }

    auto fail = [&](std::string const& msg) {
        JLOG(j_.error()) << "shard " << index_ << ". " << msg;
        return false;
    };

    auto const ledgerSeq{ledger->info().seq};
    if (ledgerSeq < firstSeq_ || ledgerSeq > lastSeq_)
        return fail("Invalid ledger sequence " + std::to_string(ledgerSeq));

    auto const scopedCount{makeBackendCount()};
    if (!scopedCount)
        return false;

    // This lock is used as an optimization to prevent unneeded
    // calls to storeSQLite before acquireInfo_ is updated
    std::lock_guard storedLock(storedMutex_);

    {
        std::lock_guard lock(mutex_);
        if (!acquireInfo_)
            return fail("Missing acquire SQLite database");

        if (boost::icl::contains(acquireInfo_->storedSeqs, ledgerSeq))
        {
            // Ignore redundant calls
            JLOG(j_.debug()) << "shard " << index_ << " ledger sequence "
                             << ledgerSeq << " already stored";
            return true;
        }
    }

    if (!storeSQLite(ledger))
        return fail("Failed to store ledger");

    std::lock_guard lock(mutex_);

    // Update the acquire database
    acquireInfo_->storedSeqs.insert(ledgerSeq);

    try
    {
        auto session{acquireInfo_->SQLiteDB->checkoutDb()};
        soci::blob sociBlob(*session);
        convert(to_string(acquireInfo_->storedSeqs), sociBlob);
        if (ledgerSeq == lastSeq_)
        {
            // Store shard's last ledger hash
            auto const sHash{to_string(ledger->info().hash)};
            *session << "UPDATE Shard "
                        "SET LastLedgerHash = :lastLedgerHash,"
                        "StoredLedgerSeqs = :storedLedgerSeqs "
                        "WHERE ShardIndex = :shardIndex;",
                soci::use(sHash), soci::use(sociBlob), soci::use(index_);
        }
        else
        {
            *session << "UPDATE Shard "
                        "SET StoredLedgerSeqs = :storedLedgerSeqs "
                        "WHERE ShardIndex = :shardIndex;",
                soci::use(sociBlob), soci::use(index_);
        }
    }
    catch (std::exception const& e)
    {
        acquireInfo_->storedSeqs.erase(ledgerSeq);
        return fail(
            std::string(". Exception caught in function ") + __func__ +
            ". Error: " + e.what());
    }

    // Update progress
    progress_ = boost::icl::length(acquireInfo_->storedSeqs);
    if (progress_ == maxLedgers_)
        state_ = ShardState::complete;

    setFileStats(lock);
    JLOG(j_.trace()) << "shard " << index_ << " stored ledger sequence "
                     << ledgerSeq;
    return true;
}

bool
Shard::containsLedger(std::uint32_t ledgerSeq) const
{
    if (ledgerSeq < firstSeq_ || ledgerSeq > lastSeq_)
        return false;
    if (state_ != ShardState::acquire)
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
Shard::finalize(bool writeSQLite, std::optional<uint256> const& referenceHash)
{
    auto const scopedCount{makeBackendCount()};
    if (!scopedCount)
        return false;

    uint256 hash{0};
    std::uint32_t ledgerSeq{0};
    auto fail = [&](std::string const& msg) {
        JLOG(j_.fatal()) << "shard " << index_ << ". " << msg
                         << (hash.isZero() ? ""
                                           : ". Ledger hash " + to_string(hash))
                         << (ledgerSeq == 0 ? ""
                                            : ". Ledger sequence " +
                                     std::to_string(ledgerSeq));
        state_ = ShardState::finalizing;
        progress_ = 0;
        busy_ = false;
        return false;
    };

    try
    {
        state_ = ShardState::finalizing;
        progress_ = 0;

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
            // must be present in order to verify the shard
            if (!acquireInfo_)
                return fail("missing acquire SQLite database");

            auto [res, seqshash] = selectAcquireDBLedgerSeqsHash(
                *acquireInfo_->SQLiteDB->checkoutDb(), index_);

            if (!res)
                return fail("missing or invalid ShardIndex");

            if (!seqshash.hash)
                return fail("missing LastLedgerHash");

            if (!hash.parseHex(*seqshash.hash) || hash.isZero())
                return fail("invalid LastLedgerHash");

            if (!seqshash.sequences)
                return fail("missing StoredLedgerSeqs");

            auto& storedSeqs{acquireInfo_->storedSeqs};
            if (!from_string(storedSeqs, *seqshash.sequences) ||
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

    // Verify the last ledger hash of a downloaded shard
    // using a ledger hash obtained from the peer network
    if (referenceHash && *referenceHash != hash)
        return fail("invalid last ledger hash");

    // Verify every ledger stored in the backend
    Config const& config{app_.config()};
    std::shared_ptr<Ledger> ledger;
    std::shared_ptr<Ledger const> next;
    auto const lastLedgerHash{hash};
    auto& shardFamily{*app_.getShardFamily()};
    auto const fullBelowCache{shardFamily.getFullBelowCache(lastSeq_)};
    auto const treeNodeCache{shardFamily.getTreeNodeCache(lastSeq_)};

    // Reset caches to reduce memory usage
    fullBelowCache->reset();
    treeNodeCache->reset();

    Serializer s;
    s.add32(version);
    s.add32(firstSeq_);
    s.add32(lastSeq_);
    s.addBitString(lastLedgerHash);

    std::shared_ptr<DeterministicShard> dShard{
        make_DeterministicShard(app_, dir_, index_, s, j_)};
    if (!dShard)
        return fail("Failed to create deterministic shard");

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

        if (!verifyLedger(ledger, next, dShard))
            return fail("failed to verify ledger");

        if (!dShard->store(nodeObject))
            return fail("failed to store node object");

        if (writeSQLite && !storeSQLite(ledger))
            return fail("failed storing to SQLite databases");

        hash = ledger->info().parentHash;
        next = std::move(ledger);

        // Update progress
        progress_ = maxLedgers_ - (ledgerSeq - firstSeq_);

        --ledgerSeq;

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
            auto session {sqliteDB->checkoutDb()};
            *session << "PRAGMA synchronous=OFF;";
            *session << "PRAGMA journal_mode=OFF;";
            *session << "PRAGMA temp_store_directory='" <<
                tmpDir.string() << "';";
            *session << "VACUUM;";
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

    auto const nodeObject{
        NodeObject::createObject(hotUNKNOWN, std::move(s.modData()), finalKey)};
    if (!dShard->store(nodeObject))
        return fail("failed to store node object");

    try
    {
        // Store final key's value, may already be stored
        backend_->store(nodeObject);

        // Do not allow all other threads work with the shard
        busy_ = true;

        // Wait until all other threads leave the shard
        while (backendCount_ > 1)
            std::this_thread::yield();

        std::lock_guard lock(mutex_);

        // Close original backend
        backend_->close();

        // Close SQL databases
        lgrSQLiteDB_.reset();
        txSQLiteDB_.reset();

        // Remove the acquire SQLite database
        if (acquireInfo_)
        {
            acquireInfo_.reset();
            remove_all(dir_ / AcquireShardDBName);
        }

        // Close deterministic backend
        dShard->close();

        // Replace original backend with deterministic backend
        remove(dir_ / "nudb.key");
        remove(dir_ / "nudb.dat");
        rename(dShard->getDir() / "nudb.key", dir_ / "nudb.key");
        rename(dShard->getDir() / "nudb.dat", dir_ / "nudb.dat");

        // Re-open deterministic shard
        if (!open(lock))
            return fail("failed to open");

        assert(state_ == ShardState::finalized);

        // Allow all other threads work with the shard
        busy_ = false;
    }
    catch (std::exception const& e)
    {
        return fail(
            std::string(". Exception caught in function ") + __func__ +
            ". Error: " + e.what());
    }

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

        state_ = ShardState::acquire;
        progress_ = 0;

        if (!preexist)
            remove_all(dir_);

        if (!msg.empty())
        {
            JLOG(j_.fatal()) << "shard " << index_ << " " << msg;
        }
        return false;
    };
    auto createAcquireInfo = [this, &config]() {
        DatabaseCon::Setup setup;
        setup.startUp = config.standalone() ? config.LOAD : config.START_UP;
        setup.standAlone = config.standalone();
        setup.dataDir = dir_;
        setup.useGlobalPragma = true;

        acquireInfo_ = std::make_unique<AcquireInfo>();
        acquireInfo_->SQLiteDB = makeAcquireDB(
            setup,
            DatabaseCon::CheckpointerSetup{&app_.getJobQueue(), &app_.logs()});

        state_ = ShardState::acquire;
        progress_ = 0;
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
            insertAcquireDBIndex(acquireInfo_->SQLiteDB->getSession(), index_);
        }
        else if (exists(dir_ / AcquireShardDBName))
        {
            // A shard being acquired, backend is likely incomplete
            createAcquireInfo();
            auto [res, s] = selectAcquireDBLedgerSeqs(
                acquireInfo_->SQLiteDB->getSession(), index_);

            if (!res)
                return fail("invalid acquire SQLite database");

            if (s)
            {
                auto& storedSeqs{acquireInfo_->storedSeqs};
                if (!from_string(storedSeqs, *s))
                    return fail("invalid StoredLedgerSeqs");

                if (boost::icl::first(storedSeqs) < firstSeq_ ||
                    boost::icl::last(storedSeqs) > lastSeq_)
                {
                    return fail("invalid StoredLedgerSeqs");
                }

                // Check if backend is complete
                progress_ = boost::icl::length(storedSeqs);
                if (progress_ == maxLedgers_)
                    state_ = ShardState::complete;
            }
        }
        else
        {
            // A shard with a finalized or complete state
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
                state_ = ShardState::finalized;
            }
            else
                state_ = ShardState::complete;

            progress_ = maxLedgers_;
        }
    }
    catch (std::exception const& e)
    {
        return fail(
            std::string(". Exception caught in function ") + __func__ +
            ". Error: " + e.what());
    }

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
        setup.useGlobalPragma = (state_ != ShardState::complete);
        return setup;
    }();

    try
    {
        if (lgrSQLiteDB_)
            lgrSQLiteDB_.reset();

        if (txSQLiteDB_)
            txSQLiteDB_.reset();

        switch (state_)
        {
            case ShardState::complete:
            case ShardState::finalizing:
            case ShardState::finalized: {
                auto [lgr, tx] = makeShardCompleteLedgerDBs(config, setup);

                lgrSQLiteDB_ = std::move(lgr);
                lgrSQLiteDB_->getSession() << boost::str(
                    boost::format("PRAGMA cache_size=-%d;") %
                    kilobytes(config.getValueFor(
                        SizedItem::lgrDBCache, std::nullopt)));

                txSQLiteDB_ = std::move(tx);
                txSQLiteDB_->getSession() << boost::str(
                    boost::format("PRAGMA cache_size=-%d;") %
                    kilobytes(config.getValueFor(
                        SizedItem::txnDBCache, std::nullopt)));
                break;
            }

            // case ShardState::acquire:
            // case ShardState::queued:
            default: {
                // Incomplete shards use a Write Ahead Log for performance
                auto [lgr, tx] = makeShardIncompleteLedgerDBs(
                    config,
                    setup,
                    DatabaseCon::CheckpointerSetup{
                        &app_.getJobQueue(), &app_.logs()});

                lgrSQLiteDB_ = std::move(lgr);
                lgrSQLiteDB_->getSession() << boost::str(
                    boost::format("PRAGMA cache_size=-%d;") %
                    kilobytes(config.getValueFor(SizedItem::lgrDBCache)));

                txSQLiteDB_ = std::move(tx);
                txSQLiteDB_->getSession() << boost::str(
                    boost::format("PRAGMA cache_size=-%d;") %
                    kilobytes(config.getValueFor(SizedItem::txnDBCache)));
                break;
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

bool
Shard::storeSQLite(std::shared_ptr<Ledger const> const& ledger)
{
    if (stop_)
        return false;

    try
    {
        auto res = updateLedgerDBs(
            *txSQLiteDB_->checkoutDb(),
            *lgrSQLiteDB_->checkoutDb(),
            ledger,
            index_,
            stop_,
            j_);

        if (!res)
            return false;

        // Update the acquire database if present
        if (acquireInfo_)
        {
            std::optional<std::string> s;
            if (!acquireInfo_->storedSeqs.empty())
                s = to_string(acquireInfo_->storedSeqs);

            updateAcquireDB(
                acquireInfo_->SQLiteDB->getSession(),
                ledger,
                index_,
                lastSeq_,
                s);
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
    std::shared_ptr<Ledger const> const& next,
    std::shared_ptr<DeterministicShard> const& dShard) const
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
    auto visit = [this, &error, &dShard](SHAMapTreeNode const& node) {
        if (stop_)
            return false;

        auto nodeObject{verifyFetch(node.getHash().as_uint256())};
        if (!nodeObject || !dShard->store(nodeObject))
            error = true;

        return !error;
    };

    // Verify the state map
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

    // Verify the transaction map
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
    if (stop_ || busy_)
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
    else if (state_ == ShardState::finalized)
        lastAccess_ = std::chrono::steady_clock::now();

    return Shard::Count(&backendCount_);
}

bool
Shard::doCallForSQL(
    std::function<bool(soci::session& session)> const& callback,
    LockedSociSession&& db)
{
    return callback(*db);
}

bool
Shard::doCallForSQL(
    std::function<bool(soci::session& session, std::uint32_t shardIndex)> const&
        callback,
    LockedSociSession&& db)
{
    return callback(*db, index_);
}

}  // namespace NodeStore
}  // namespace ripple
