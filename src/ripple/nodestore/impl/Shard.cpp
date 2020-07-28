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
#include <ripple/nodestore/impl/DatabaseShardImp.h>
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
    : app_(app)
    , db_(db)
    , index_(index)
    , firstSeq_(db.firstLedgerSeq(index))
    , lastSeq_(std::max(firstSeq_, db.lastLedgerSeq(index)))
    , maxLedgers_(
          index == db.earliestShardIndex() ? lastSeq_ - firstSeq_ + 1
                                           : db.ledgersPerShard())
    , dir_(db.getRootDir() / std::to_string(index_))
    , j_(j)
{
    if (index_ < db.earliestShardIndex())
        Throw<std::runtime_error>("Shard: Invalid index");
}

Shard::~Shard()
{
    if (removeOnDestroy_)
    {
        backend_.reset();
        lgrSQLiteDB_.reset();
        txSQLiteDB_.reset();
        acquireInfo_.reset();

        try
        {
            boost::filesystem::remove_all(dir_);
        }
        catch (std::exception const& e)
        {
            JLOG(j_.error()) << "shard " << index_ << " exception " << e.what()
                             << " in function " << __func__;
        }
    }
}

bool
Shard::open(Scheduler& scheduler, nudb::context& ctx)
{
    std::lock_guard lock{mutex_};
    assert(!backend_);

    Config const& config{app_.config()};
    {
        Section section{config.section(ConfigSection::shardDatabase())};
        std::string const type{get<std::string>(section, "type", "nudb")};
        auto factory{Manager::instance().find(type)};
        if (!factory)
        {
            JLOG(j_.error()) << "shard " << index_
                             << " failed to create backend type " << type;
            return false;
        }

        section.set("path", dir_.string());
        backend_ = factory->createInstance(
            NodeObject::keyBytes, section, scheduler, ctx, j_);
    }

    using namespace boost::filesystem;
    auto preexist{false};
    auto fail = [this, &preexist](std::string const& msg) {
        pCache_.reset();
        nCache_.reset();
        backend_.reset();
        lgrSQLiteDB_.reset();
        txSQLiteDB_.reset();
        acquireInfo_.reset();

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
        setup.startUp = config.START_UP;
        setup.standAlone = config.standalone();
        setup.dataDir = dir_;
        setup.useGlobalPragma = true;

        acquireInfo_->SQLiteDB = std::make_unique<DatabaseCon>(
            setup,
            AcquireShardDBName,
            AcquireShardDBPragma,
            AcquireShardDBInit,
            DatabaseCon::CheckpointerSetup{&app_.getJobQueue(), &app_.logs()});
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
            // An incomplete shard, being acquired
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

                if (boost::icl::length(storedSeqs) == maxLedgers_)
                    // All ledgers have been acquired, shard backend is complete
                    backendComplete_ = true;
            }
        }
        else
        {
            // A finalized shard or has all ledgers stored in the backend
            std::shared_ptr<NodeObject> nObj;
            if (backend_->fetch(finalKey.data(), &nObj) != Status::ok)
            {
                legacy_ = true;
                return fail("incompatible, missing backend final key");
            }

            // Check final key's value
            SerialIter sIt(nObj->getData().data(), nObj->getData().size());
            if (sIt.get32() != version)
                return fail("invalid version");

            if (sIt.get32() != firstSeq_ || sIt.get32() != lastSeq_)
                return fail("out of range ledger sequences");

            if (sIt.get256().isZero())
                return fail("invalid last ledger hash");

            if (exists(dir_ / LgrDBName) && exists(dir_ / TxDBName))
                final_ = true;

            backendComplete_ = true;
        }
    }
    catch (std::exception const& e)
    {
        return fail(
            std::string("exception ") + e.what() + " in function " + __func__);
    }

    setBackendCache(lock);
    if (!initSQLite(lock))
        return fail({});

    setFileStats(lock);
    return true;
}

boost::optional<std::uint32_t>
Shard::prepare()
{
    std::lock_guard lock(mutex_);
    assert(backend_);

    if (backendComplete_)
    {
        JLOG(j_.warn()) << "shard " << index_
                        << " prepare called when shard backend is complete";
        return {};
    }

    assert(acquireInfo_);
    auto const& storedSeqs{acquireInfo_->storedSeqs};
    if (storedSeqs.empty())
        return lastSeq_;
    return prevMissing(storedSeqs, 1 + lastSeq_, firstSeq_);
}

bool
Shard::store(std::shared_ptr<Ledger const> const& ledger)
{
    auto const seq{ledger->info().seq};
    if (seq < firstSeq_ || seq > lastSeq_)
    {
        JLOG(j_.error()) << "shard " << index_ << " invalid ledger sequence "
                         << seq;
        return false;
    }

    std::lock_guard lock(mutex_);
    assert(backend_);

    if (backendComplete_)
    {
        JLOG(j_.debug()) << "shard " << index_ << " ledger sequence " << seq
                         << " already stored";
        return true;
    }

    assert(acquireInfo_);
    auto& storedSeqs{acquireInfo_->storedSeqs};
    if (boost::icl::contains(storedSeqs, seq))
    {
        JLOG(j_.debug()) << "shard " << index_ << " ledger sequence " << seq
                         << " already stored";
        return true;
    }
    // storeSQLite looks at storedSeqs so insert before the call
    storedSeqs.insert(seq);

    if (!storeSQLite(ledger, lock))
        return false;

    if (boost::icl::length(storedSeqs) >= maxLedgers_)
    {
        if (!initSQLite(lock))
            return false;

        backendComplete_ = true;
        setBackendCache(lock);
    }

    JLOG(j_.debug()) << "shard " << index_ << " stored ledger sequence " << seq
                     << (backendComplete_ ? " . All ledgers stored" : "");

    setFileStats(lock);
    return true;
}

bool
Shard::containsLedger(std::uint32_t seq) const
{
    if (seq < firstSeq_ || seq > lastSeq_)
        return false;

    std::lock_guard lock(mutex_);
    if (backendComplete_)
        return true;

    assert(acquireInfo_);
    return boost::icl::contains(acquireInfo_->storedSeqs, seq);
}

void
Shard::sweep()
{
    std::lock_guard lock(mutex_);
    assert(pCache_ && nCache_);

    pCache_->sweep();
    nCache_->sweep();
}

std::tuple<
    std::shared_ptr<Backend>,
    std::shared_ptr<PCache>,
    std::shared_ptr<NCache>>
Shard::getBackendAll() const
{
    std::lock_guard lock(mutex_);
    assert(backend_);

    return {backend_, pCache_, nCache_};
}

std::shared_ptr<Backend>
Shard::getBackend() const
{
    std::lock_guard lock(mutex_);
    assert(backend_);

    return backend_;
}

bool
Shard::isBackendComplete() const
{
    std::lock_guard lock(mutex_);
    return backendComplete_;
}

std::shared_ptr<PCache>
Shard::pCache() const
{
    std::lock_guard lock(mutex_);
    assert(pCache_);

    return pCache_;
}

std::shared_ptr<NCache>
Shard::nCache() const
{
    std::lock_guard lock(mutex_);
    assert(nCache_);

    return nCache_;
}

std::pair<std::uint64_t, std::uint32_t>
Shard::fileInfo() const
{
    std::lock_guard lock(mutex_);
    return {fileSz_, fdRequired_};
}

bool
Shard::isFinal() const
{
    std::lock_guard lock(mutex_);
    return final_;
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
    boost::optional<uint256> const& expectedHash,
    const bool writeDeterministicShard)
{
    assert(backend_);

    if (stop_)
        return false;

    uint256 hash{0};
    std::uint32_t seq{0};
    auto fail =
        [j = j_, index = index_, &hash, &seq](std::string const& msg) {
            JLOG(j.fatal())
                << "shard " << index << ". " << msg
                << (hash.isZero() ? "" : ". Ledger hash " + to_string(hash))
                << (seq == 0 ? "" : ". Ledger sequence " + std::to_string(seq));
            return false;
        };

    try
    {
        std::unique_lock lock(mutex_);
        if (!backendComplete_)
            return fail("backend incomplete");

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
        lock.unlock();
        if (std::shared_ptr<NodeObject> nObj;
            backend_->fetch(finalKey.data(), &nObj) == Status::ok)
        {
            // Check final key's value
            SerialIter sIt(nObj->getData().data(), nObj->getData().size());
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
            lock.lock();
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

            lock.unlock();
            if (!index || index != index_)
                return fail("missing or invalid ShardIndex");

            if (!sHash)
                return fail("missing LastLedgerHash");

            if (hash.SetHexExact(*sHash); hash.isZero())
                return fail("invalid LastLedgerHash");

            if (blobPresent != soci::i_ok)
                return fail("missing StoredLedgerSeqs");

            std::string s;
            convert(sociBlob, s);

            lock.lock();

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
            std::string("exception ") + e.what() + " in function " + __func__);
    }

    // Validate the last ledger hash of a downloaded shard
    // using a ledger hash obtained from the peer network
    if (expectedHash && *expectedHash != hash)
        return fail("invalid last ledger hash");

    // Validate every ledger stored in the backend
    std::shared_ptr<Ledger> ledger;
    std::shared_ptr<Ledger const> next;
    auto const lastLedgerHash{hash};

    std::shared_ptr<DeterministicShard> dsh;
    if (writeDeterministicShard)
    {
        dsh = std::make_shared<DeterministicShard>(
            app_, db_, index_, lastLedgerHash, j_);
        if (!dsh->init())
        {
            return fail("can't create deterministic shard");
        }
    }

    // Start with the last ledger in the shard and walk backwards from
    // child to parent until we reach the first ledger
    seq = lastSeq_;
    while (seq >= firstSeq_)
    {
        if (stop_)
            return false;

        auto nObj = valFetch(hash);
        if (!nObj)
            return fail("invalid ledger");

        ledger = std::make_shared<Ledger>(
            deserializePrefixedHeader(makeSlice(nObj->getData())),
            app_.config(),
            *app_.getShardFamily());
        if (ledger->info().seq != seq)
            return fail("invalid ledger sequence");
        if (ledger->info().hash != hash)
            return fail("invalid ledger hash");

        ledger->stateMap().setLedgerSeq(seq);
        ledger->txMap().setLedgerSeq(seq);
        ledger->setImmutable(app_.config());
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

        if (dsh)
            dsh->store(nObj);

        if (!verifyLedger(ledger, next, dsh))
            return fail("verification check failed");

        if (writeSQLite)
        {
            std::lock_guard lock(mutex_);
            if (!storeSQLite(ledger, lock))
                return fail("failed storing to SQLite databases");
        }

        hash = ledger->info().parentHash;
        next = std::move(ledger);
        --seq;
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
        return fail(std::string("exception ") +
            e.what() + " in function " + __func__);
    }
    */

    // Store final key's value, may already be stored
    Serializer s;
    s.add32(version);
    s.add32(firstSeq_);
    s.add32(lastSeq_);
    s.addBitString(lastLedgerHash);
    auto nObj{
        NodeObject::createObject(hotUNKNOWN, std::move(s.modData()), finalKey)};
    try
    {
        backend_->store(nObj);

        if (dsh)
        {
            dsh->store(nObj);
            dsh->flush();
        }

        std::lock_guard lock(mutex_);
        final_ = true;

        // Remove the acquire SQLite database if present
        if (acquireInfo_)
            acquireInfo_.reset();
        remove_all(dir_ / AcquireShardDBName);

        if (!initSQLite(lock))
            return fail("failed to initialize SQLite databases");

        setFileStats(lock);
    }
    catch (std::exception const& e)
    {
        return fail(
            std::string("exception ") + e.what() + " in function " + __func__);
    }

    if (dsh)
    {
        /* Close non-deterministic shard database. */
        backend_->close();
        /* Replace non-deterministic shard by deterministic one. */
        dsh->close();
        /* Re-open deterministic shard database. */
        backend_->open(false);
        /** The finalize() function verifies the shard and, if third parameter
         *  is true, then replaces the shard by deterministic copy of the shard.
         *  After deterministic shard is created it verifies again,
         *  the finalize() function called here to verify deterministic shard,
         *  third parameter is false.
         */
        return finalize(false, expectedHash, false);
    }

    return true;
}

void
Shard::setBackendCache(std::lock_guard<std::recursive_mutex> const&)
{
    // Complete shards use the smallest cache and
    // fastest expiration to reduce memory consumption.
    // An incomplete shard is set according to configuration.

    Config const& config{app_.config()};
    if (!pCache_)
    {
        auto const name{"shard " + std::to_string(index_)};
        auto const sz{config.getValueFor(
            SizedItem::nodeCacheSize,
            backendComplete_ ? boost::optional<std::size_t>(0) : boost::none)};
        auto const age{std::chrono::seconds{config.getValueFor(
            SizedItem::nodeCacheAge,
            backendComplete_ ? boost::optional<std::size_t>(0) : boost::none)}};

        pCache_ = std::make_shared<PCache>(name, sz, age, stopwatch(), j_);
        nCache_ = std::make_shared<NCache>(name, stopwatch(), sz, age);
    }
    else
    {
        auto const sz{config.getValueFor(SizedItem::nodeCacheSize, 0)};
        pCache_->setTargetSize(sz);
        nCache_->setTargetSize(sz);

        auto const age{std::chrono::seconds{
            config.getValueFor(SizedItem::nodeCacheAge, 0)}};
        pCache_->setTargetAge(age);
        nCache_->setTargetAge(age);
    }
}

bool
Shard::initSQLite(std::lock_guard<std::recursive_mutex> const&)
{
    Config const& config{app_.config()};
    DatabaseCon::Setup const setup = [&]() {
        DatabaseCon::Setup result;
        result.startUp = config.START_UP;
        result.standAlone = config.standalone();
        result.dataDir = dir_;
        result.useGlobalPragma = !backendComplete_;
        return result;
    }();

    try
    {
        if (lgrSQLiteDB_)
            lgrSQLiteDB_.reset();

        if (txSQLiteDB_)
            txSQLiteDB_.reset();

        if (backendComplete_)
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
        JLOG(j_.fatal()) << "shard " << index_ << " exception " << e.what()
                         << " in function " << __func__;
        return false;
    }
    return true;
}

bool
Shard::storeSQLite(
    std::shared_ptr<Ledger const> const& ledger,
    std::lock_guard<std::recursive_mutex> const&)
{
    if (stop_)
        return false;

    auto const seq{ledger->info().seq};

    try
    {
        // Update the transactions database
        {
            auto& session{txSQLiteDB_->getSession()};
            soci::transaction tr(session);

            session << "DELETE FROM Transactions "
                       "WHERE LedgerSeq = :seq;",
                soci::use(seq);
            session << "DELETE FROM AccountTransactions "
                       "WHERE LedgerSeq = :seq;",
                soci::use(seq);

            if (ledger->info().txHash.isNonZero())
            {
                auto const sSeq{std::to_string(seq)};
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
                                seq, sqlEscape(s.modData())) +
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
                soci::use(seq);
            session
                << "INSERT OR REPLACE INTO Ledgers ("
                   "LedgerHash, LedgerSeq, PrevHash, TotalCoins, ClosingTime,"
                   "PrevClosingTime, CloseTimeRes, CloseFlags, AccountSetHash,"
                   "TransSetHash)"
                   "VALUES ("
                   ":ledgerHash, :ledgerSeq, :prevHash, :totalCoins,"
                   ":closingTime, :prevClosingTime, :closeTimeRes,"
                   ":closeFlags, :accountSetHash, :transSetHash);",
                soci::use(sHash), soci::use(seq), soci::use(sParentHash),
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
        JLOG(j_.fatal()) << "shard " << index_ << " exception " << e.what()
                         << " in function " << __func__;
        return false;
    }
    return true;
}

void
Shard::setFileStats(std::lock_guard<std::recursive_mutex> const&)
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
        JLOG(j_.error()) << "shard " << index_ << " exception " << e.what()
                         << " in function " << __func__;
    }
}

bool
Shard::verifyLedger(
    std::shared_ptr<Ledger const> const& ledger,
    std::shared_ptr<Ledger const> const& next,
    std::shared_ptr<DeterministicShard> dsh) const
{
    auto fail = [j = j_, index = index_, &ledger](std::string const& msg) {
        JLOG(j.fatal()) << "shard " << index << ". " << msg
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
    auto visit = [this, &error, dsh](SHAMapAbstractNode& node) {
        if (stop_)
            return false;
        auto nObj = valFetch(node.getNodeHash().as_uint256());
        if (!nObj)
            error = true;
        else if (dsh)
            dsh->store(nObj);
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
                std::string("exception ") + e.what() + " in function " +
                __func__);
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
                std::string("exception ") + e.what() + " in function " +
                __func__);
        }
        if (stop_)
            return false;
        if (error)
            return fail("Invalid transaction map");
    }

    return true;
}

std::shared_ptr<NodeObject>
Shard::valFetch(uint256 const& hash) const
{
    std::shared_ptr<NodeObject> nObj;
    auto fail = [j = j_, index = index_, &hash, &nObj](std::string const& msg) {
        JLOG(j.fatal()) << "shard " << index << ". " << msg
                        << ". Node object hash " << to_string(hash);
        nObj.reset();
        return nObj;
    };

    try
    {
        switch (backend_->fetch(hash.data(), &nObj))
        {
            case ok:
                // This verifies that the hash of node object matches the
                // payload
                if (nObj->getHash() != sha512Half(makeSlice(nObj->getData())))
                    return fail("Node object hash does not match payload");
                return nObj;
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
            std::string("exception ") + e.what() + " in function " + __func__);
    }
}

}  // namespace NodeStore
}  // namespace ripple
