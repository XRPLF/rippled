//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2020 Ripple Labs Inc.

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

#include <ripple/app/ledger/AcceptedLedger.h>
#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/ledger/LedgerToJson.h>
#include <ripple/app/ledger/TransactionMaster.h>
#include <ripple/app/misc/Manifest.h>
#include <ripple/basics/BasicConfig.h>
#include <ripple/basics/StringUtilities.h>
#include <ripple/core/SQLInterface.h>
#include <ripple/core/sql_backend/DatabaseCon.h>
#include <ripple/core/sql_backend/SociDB.h>
#include <ripple/json/to_string.h>
#include <boost/algorithm/string.hpp>
#include <boost/range/adaptor/transformed.hpp>
#include <soci/sqlite3/soci-sqlite3.h>

namespace ripple {

class SQLDatabase_sqlite : public SQLDatabase_
{
public:
    SQLDatabase_sqlite(SQLInterface* iface)
        : SQLDatabase_(iface), db_(std::in_place_index<1>)
    {
    }

    /* for fake databases */
    SQLDatabase_sqlite(SQLInterface* iface, bool on)
        : SQLDatabase_(iface), db_(std::in_place_index<0>, on)
    {
    }

    template <std::size_t N, std::size_t M>
    SQLDatabase_sqlite(
        SQLInterface* iface,
        DatabaseCon::Setup const& setup,
        std::string const& DBName,
        std::array<char const*, N> const& pragma,
        std::array<char const*, M> const& initSQL)
        : SQLDatabase_(iface)
        , db_(std::in_place_index<2>, setup, DBName, pragma, initSQL)
    {
    }

    template <std::size_t N, std::size_t M>
    SQLDatabase_sqlite(
        SQLInterface* iface,
        boost::filesystem::path const& dataDir,
        std::string const& DBName,
        std::array<char const*, N> const& pragma,
        std::array<char const*, M> const& initSQL)
        : SQLDatabase_(iface)
        , db_(std::in_place_index<2>, dataDir, DBName, pragma, initSQL)
    {
    }

    std::variant<bool, soci::session, DatabaseCon> db_;
};

class SQLInterface_sqlite : public SQLInterface
{
public:
    virtual std::string
    getDBName(DatabaseType type) override;

    virtual std::tuple<bool, SQLDatabase, SQLDatabase>
    makeLedgerDBs(
        Application& app,
        Config const& config,
        beast::Journal const& j,
        bool setupFromConfig,
        LedgerIndex shardIndex,
        bool backendComplete,
        boost::filesystem::path const& dir) override;

    virtual SQLDatabase
    makeAcquireDB(
        Application& app,
        Config const& config,
        boost::filesystem::path const& dir) override;

    virtual SQLDatabase
    makeWalletDB(
        bool setupFromConfig,
        Config const& config,
        beast::Journal const& j,
        std::string const& dbname,
        boost::filesystem::path const& dir) override;

    virtual SQLDatabase
    makeArchiveDB(boost::filesystem::path const& dir, std::string const& dbName)
        override;

    virtual void
    initStateDB(
        SQLDatabase& db,
        BasicConfig const& config,
        std::string const& dbName) override;

    virtual std::pair<SQLDatabase, boost::optional<std::uint64_t>>
    openDatabaseBodyDb(
        Config const& config,
        boost::filesystem::path const& path) override;

    virtual bool
    makeVacuumDB(Config const& config) override;

    virtual void
    initPeerFinderDB(
        SQLDatabase& db,
        BasicConfig const& config,
        beast::Journal const j) override;

    virtual void
    updatePeerFinderDB(
        SQLDatabase& db,
        int currentSchemaVersion,
        beast::Journal const j) override;

    virtual boost::optional<LedgerIndex>
    getMinLedgerSeq(SQLDatabase& db, TableType type) override;

    virtual boost::optional<LedgerIndex>
    getMinLedgerSeq(SQLDatabase_* db, TableType type) override;

    virtual boost::optional<LedgerIndex>
    getMaxLedgerSeq(SQLDatabase& db, TableType type) override;

    virtual boost::optional<LedgerIndex>
    getMaxLedgerSeq(SQLDatabase_* db, TableType type) override;

    virtual void
    deleteByLedgerSeq(SQLDatabase& db, TableType type, LedgerIndex ledgerSeq)
        override;

    virtual void
    deleteByLedgerSeq(SQLDatabase_* db, TableType type, LedgerIndex ledgerSeq)
        override;

    virtual void
    deleteBeforeLedgerSeq(
        SQLDatabase& db,
        TableType type,
        LedgerIndex ledgerSeq) override;

    virtual void
    deleteBeforeLedgerSeq(
        SQLDatabase_* db,
        TableType type,
        LedgerIndex ledgerSeq) override;

    virtual int
    getRows(SQLDatabase& db, TableType type) override;

    virtual int
    getRows(SQLDatabase_* db, TableType type) override;

    virtual std::tuple<int, int, int>
    getRowsMinMax(SQLDatabase& db, TableType type) override;

    virtual std::tuple<int, int, int>
    getRowsMinMax(SQLDatabase_* db, TableType type) override;

    virtual void
    insertAcquireDBIndex(SQLDatabase& db, std::uint32_t index_) override;

    virtual std::pair<bool, boost::optional<std::string>>
    selectAcquireDBLedgerSeqs(SQLDatabase& db, std::uint32_t index) override;

    virtual std::
        tuple<bool, boost::optional<std::string>, boost::optional<std::string>>
        selectAcquireDBLedgerSeqsHash(SQLDatabase& db, std::uint32_t index)
            override;

    virtual bool
    updateLedgerDBs(
        SQLDatabase& txdb,
        SQLDatabase& lgrdb,
        std::shared_ptr<Ledger const> const& ledger,
        std::uint32_t const index,
        beast::Journal const j,
        std::atomic<bool>& stop) override;

    virtual void
    updateAcquireDB(
        SQLDatabase& db,
        std::shared_ptr<Ledger const> const& ledger,
        std::uint32_t const index,
        std::uint32_t const lastSeq,
        boost::optional<std::string> seqs) override;

    virtual bool
    saveValidatedLedger(
        SQLDatabase& ldgDB,
        SQLDatabase& txnDB,
        Application& app,
        std::shared_ptr<Ledger const> const& ledger,
        bool current) override;

    virtual bool
    saveValidatedLedger(
        SQLDatabase_* ldgDB,
        SQLDatabase_* txnDB,
        Application& app,
        std::shared_ptr<Ledger const> const& ledger,
        bool current) override;

    virtual bool
    loadLedgerInfoByIndex(
        SQLDatabase& db,
        SQLInterface::SQLLedgerInfo& info,
        beast::Journal const& j,
        LedgerIndex ledgerSeq) override;

    virtual bool
    loadLedgerInfoByIndex(
        SQLDatabase_* db,
        SQLInterface::SQLLedgerInfo& info,
        beast::Journal const& j,
        LedgerIndex ledgerSeq) override;

    virtual bool
    loadLedgerInfoByIndexSorted(
        SQLDatabase& db,
        SQLInterface::SQLLedgerInfo& info,
        beast::Journal const& j,
        bool ascendSort) override;

    virtual bool
    loadLedgerInfoByIndexSorted(
        SQLDatabase_* db,
        SQLInterface::SQLLedgerInfo& info,
        beast::Journal const& j,
        bool ascendSort) override;

    virtual bool
    loadLedgerInfoByIndexLimitedSorted(
        SQLDatabase& db,
        SQLInterface::SQLLedgerInfo& info,
        beast::Journal const& j,
        LedgerIndex ledgerFirstIndex,
        bool ascendSort) override;

    virtual bool
    loadLedgerInfoByIndexLimitedSorted(
        SQLDatabase_* db,
        SQLInterface::SQLLedgerInfo& info,
        beast::Journal const& j,
        LedgerIndex ledgerFirstIndex,
        bool ascendSort) override;

    virtual bool
    loadLedgerInfoByHash(
        SQLDatabase& db,
        SQLInterface::SQLLedgerInfo& info,
        beast::Journal const& j,
        uint256 const& ledgerHash) override;

    virtual bool
    loadLedgerInfoByHash(
        SQLDatabase_* db,
        SQLInterface::SQLLedgerInfo& info,
        beast::Journal const& j,
        uint256 const& ledgerHash) override;

    virtual uint256
    getHashByIndex(SQLDatabase& db, LedgerIndex ledgerIndex) override;

    virtual uint256
    getHashByIndex(SQLDatabase_* db, LedgerIndex ledgerIndex) override;

    virtual bool
    getHashesByIndex(
        SQLDatabase& db,
        beast::Journal const& j,
        LedgerIndex ledgerIndex,
        uint256& ledgerHash,
        uint256& parentHash) override;

    virtual bool
    getHashesByIndex(
        SQLDatabase_* db,
        beast::Journal const& j,
        LedgerIndex ledgerIndex,
        uint256& ledgerHash,
        uint256& parentHash) override;

    virtual std::map<LedgerIndex, std::pair<uint256, uint256>>
    getHashesByIndex(
        SQLDatabase& db,
        beast::Journal const& j,
        LedgerIndex minSeq,
        LedgerIndex maxSeq) override;

    virtual void
    getHashesByIndex(
        SQLDatabase_* db,
        beast::Journal const& j,
        LedgerIndex minSeq,
        LedgerIndex maxSeq,
        std::map<LedgerIndex, std::pair<uint256, uint256>>& map) override;

    virtual Json::Value
    loadTxHistory(SQLDatabase& db, Application& app, LedgerIndex startIndex)
        override;

    virtual int
    loadTxHistory(
        SQLDatabase_* db,
        Application& app,
        Json::Value& txs,
        LedgerIndex startIndex,
        int quantity,
        bool count) override;

    virtual NetworkOPs::AccountTxs
    getAccountTxs(
        SQLDatabase& db,
        Application& app,
        LedgerMaster& ledgerMaster,
        beast::Journal& j,
        AccountID const& account,
        std::int32_t minLedger,
        std::int32_t maxLedger,
        bool descending,
        std::uint32_t offset,
        int limit,
        bool bUnlimited) override;

    virtual int
    getAccountTxs(
        SQLDatabase_* db,
        Application& app,
        LedgerMaster& ledgerMaster,
        beast::Journal& j,
        AccountID const& account,
        NetworkOPs::AccountTxs& ret,
        std::int32_t minLedger,
        std::int32_t maxLedger,
        bool descending,
        std::uint32_t offset,
        int limit,
        int limit_used,
        bool bUnlimited) override;

    virtual std::vector<NetworkOPs::txnMetaLedgerType>
    getAccountTxsB(
        SQLDatabase& db,
        Application& app,
        beast::Journal& j,
        AccountID const& account,
        std::int32_t minLedger,
        std::int32_t maxLedger,
        bool descending,
        std::uint32_t offset,
        int limit,
        bool bUnlimited) override;

    virtual int
    getAccountTxsB(
        SQLDatabase_* db,
        Application& app,
        beast::Journal& j,
        AccountID const& account,
        std::vector<NetworkOPs::txnMetaLedgerType>& ret,
        std::int32_t minLedger,
        std::int32_t maxLedger,
        bool descending,
        std::uint32_t offset,
        int limit,
        int limit_used,
        bool bUnlimited) override;

    virtual LedgerIndex
    getCanDelete(SQLDatabase& db) override;

    virtual LedgerIndex
    setCanDelete(SQLDatabase& db, LedgerIndex canDelete) override;

    virtual SavedState
    getSavedState(SQLDatabase& db) override;

    virtual void
    setSavedState(SQLDatabase& db, SQLInterface::SavedState const& state)
        override;

    virtual void
    setLastRotated(SQLDatabase& db, LedgerIndex seq) override;

    virtual void
    accountTxPage(
        SQLDatabase& db,
        AccountIDCache const& idCache,
        std::function<void(std::uint32_t)> const& onUnsavedLedger,
        std::function<
            void(std::uint32_t, std::string const&, Blob&&, Blob&&)> const&
            onTransaction,
        AccountID const& account,
        std::int32_t minLedger,
        std::int32_t maxLedger,
        bool forward,
        std::optional<NetworkOPs::AccountTxMarker>& marker,
        int limit,
        bool bAdmin,
        std::uint32_t page_length) override;

    virtual int
    accountTxPage(
        SQLDatabase_* db,
        AccountIDCache const& idCache,
        std::function<void(std::uint32_t)> const& onUnsavedLedger,
        std::function<
            void(std::uint32_t, std::string const&, Blob&&, Blob&&)> const&
            onTransaction,
        AccountID const& account,
        std::int32_t minLedger,
        std::int32_t maxLedger,
        bool forward,
        std::optional<NetworkOPs::AccountTxMarker>& marker,
        int limit,
        int limit_used,
        bool bAdmin,
        std::uint32_t page_length) override;

    virtual void
    loadManifest(
        SQLDatabase& dbCon,
        std::string const& dbTable,
        beast::Journal& j,
        ManifestCache& mCache) override;

    virtual void
    saveManifest(
        SQLDatabase& dbCon,
        std::string const& dbTable,
        std::function<bool(PublicKey const&)> isTrusted,
        beast::Journal& j,
        hash_map<PublicKey, Manifest>& map) override;

    virtual boost::variant<Transaction::pointer, bool>
    loadTransaction(
        SQLDatabase& db,
        Application& app,
        uint256 const& id,
        boost::optional<ClosedInterval<uint32_t>> const& range,
        error_code_i& ec) override;

    virtual boost::variant<Transaction::pointer, bool>
    loadTransaction(
        SQLDatabase_* db,
        Application& app,
        uint256 const& id,
        boost::optional<ClosedInterval<uint32_t>> const& range,
        error_code_i& ec) override;

    virtual bool
    checkDBSpace(SQLDatabase& txDb, Config const& config, beast::Journal& j)
        override;

    virtual std::pair<PublicKey, SecretKey>
    loadNodeIdentity(SQLDatabase& db) override;

    virtual void
    databaseBodyDoPut(
        SQLDatabase& conn,
        std::string& data,
        std::string& path,
        std::uint64_t fileSize,
        std::uint64_t& part,
        std::uint16_t const maxRowSizePad) override;

    virtual void
    databaseBodyFinish(SQLDatabase& conn, std::ofstream& fout) override;

    virtual void
    addValidatorManifest(SQLDatabase& db, std::string const& serialized)
        override;

    virtual void
    loadPeerReservationTable(
        SQLDatabase& conn,
        beast::Journal& j,
        std::unordered_set<PeerReservation, beast::uhash<>, KeyEqual>& table)
        override;

    virtual void
    insertPeerReservation(
        SQLDatabase& conn,
        PublicKey const& nodeId,
        std::string const& description) override;

    virtual void
    deletePeerReservation(SQLDatabase& conn, PublicKey const& nodeId) override;

    virtual void
    readArchiveDB(
        SQLDatabase& db,
        std::function<void(std::string const&, int)> const& func) override;

    virtual void
    insertArchiveDB(
        SQLDatabase& db,
        LedgerIndex shardIndex,
        std::string const& url) override;

    virtual void
    deleteFromArchiveDB(SQLDatabase& db, LedgerIndex shardIndex) override;

    virtual void
    dropArchiveDB(SQLDatabase& db) override;

    virtual int
    getKBUsedAll(SQLDatabase& db) override;

    virtual int
    getKBUsedDB(SQLDatabase& db) override;

    virtual void
    readPeerFinderDB(
        SQLDatabase& db,
        std::function<void(std::string const&, int)> const& func) override;

    virtual void
    savePeerFinderDB(
        SQLDatabase& db,
        std::vector<PeerFinder::Store::Entry> const& v) override;

private:
    bool
    loadLedgerInfo(
        SQLDatabase_* db,
        SQLInterface::SQLLedgerInfo& info,
        beast::Journal const& j,
        std::string const& sqlSuffix);

    std::string
    transactionsSQL(
        Application& app,
        beast::Journal& j,
        AccountID const& account,
        std::string selection,
        std::int32_t minLedger,
        std::int32_t maxLedger,
        bool descending,
        std::uint32_t offset,
        int limit,
        int limit_used,
        bool binary,
        bool count,
        bool bUnlimited);
};

static bool
exists(SQLDatabase& db)
{
    if (db)
    {
        SQLDatabase_sqlite* sdb = dynamic_cast<SQLDatabase_sqlite*>(&*db);
        if (sdb)
            switch (sdb->db_.index())
            {
                case 1:
                    return true;
                case 2:
                    return true;
            }
    }
    return false;
}

static soci::session&
getSession(SQLDatabase& db)
{
    if (db)
    {
        SQLDatabase_sqlite* sdb = dynamic_cast<SQLDatabase_sqlite*>(&*db);
        if (sdb)
            switch (sdb->db_.index())
            {
                case 1:
                    return std::get<1>(sdb->db_);
                case 2:
                    return std::get<2>(sdb->db_).getSession();
            }
    }
    Throw<std::runtime_error>("SQL Database is not initialized.");
}

static LockedSociSession
checkoutDb(SQLDatabase& db)
{
    if (db)
    {
        SQLDatabase_sqlite* sdb = dynamic_cast<SQLDatabase_sqlite*>(&*db);
        if (sdb)
            switch (sdb->db_.index())
            {
                case 2:
                    return std::get<2>(sdb->db_).checkoutDb();
            }
    }
    Throw<std::runtime_error>("SQL Database is not initialized.");
}

static LockedSociSession
checkoutDb(SQLDatabase_* db)
{
    if (db)
    {
        SQLDatabase_sqlite* sdb = dynamic_cast<SQLDatabase_sqlite*>(db);
        if (sdb)
            switch (sdb->db_.index())
            {
                case 2:
                    return std::get<2>(sdb->db_).checkoutDb();
            }
    }
    Throw<std::runtime_error>("SQL Database is not initialized.");
}

static void
setupCheckpointing(SQLDatabase& db, JobQueue* q, Logs& l)
{
    if (db)
    {
        SQLDatabase_sqlite* sdb = dynamic_cast<SQLDatabase_sqlite*>(&*db);
        if (sdb)
            switch (sdb->db_.index())
            {
                case 2:
                    std::get<2>(sdb->db_).setupCheckpointing(q, l);
                    return;
            }
    }
    Throw<std::runtime_error>("SQL Database is not initialized.");
}

std::string
SQLInterface_sqlite::getDBName(SQLInterface::DatabaseType type)
{
    switch (type)
    {
        case LEDGER:
        case LEDGER_SHARD:
            return LgrDBName;
        case TRANSACTION:
        case TRANSACTION_SHARD:
            return TxDBName;
        case WALLET:
            return WalletDBName;
        case ACQUIRE_SHARD:
            return AcquireShardDBName;
        case ARCHIVE:
        case STATE:
        case DOWNLOAD:
        case VACUUM:
            return stateDBName;
        case PEER_FINDER:
            return "peerfinder";
    }
    return "Unknown";
}

std::tuple<bool, SQLDatabase, SQLDatabase>
SQLInterface_sqlite::makeLedgerDBs(
    Application& app,
    Config const& config,
    beast::Journal const& j,
    bool setupFromConfig,
    LedgerIndex shardIndex,
    bool backendComplete,
    boost::filesystem::path const& dir)
{
    DatabaseCon::Setup setup;

    if (setupFromConfig)
    {
        setup = setup_DatabaseCon(config, j);
    }
    else
    {
        setup.startUp = config.START_UP;
        setup.standAlone = config.standalone();
        setup.dataDir = dir;
        setup.useGlobalPragma = !backendComplete;
    }

    if (shardIndex != -1u && backendComplete)
    {
        SQLDatabase tx{std::make_unique<SQLDatabase_sqlite>(
            this, setup, TxDBName, CompleteShardDBPragma, TxDBInit)};
        getSession(tx) << boost::str(
            boost::format("PRAGMA cache_size=-%d;") %
            kilobytes(config.getValueFor(SizedItem::txnDBCache, boost::none)));

        SQLDatabase lgr{std::make_unique<SQLDatabase_sqlite>(
            this, setup, LgrDBName, CompleteShardDBPragma, LgrDBInit)};
        getSession(lgr) << boost::str(
            boost::format("PRAGMA cache_size=-%d;") %
            kilobytes(config.getValueFor(SizedItem::lgrDBCache, boost::none)));

        addDatabase(&*tx, SQLInterface::TRANSACTION, shardIndex);
        addDatabase(&*lgr, SQLInterface::LEDGER, shardIndex);
        return {true, std::move(tx), std::move(lgr)};
    }
    else
    {
        // transaction database
        SQLDatabase tx{std::make_unique<SQLDatabase_sqlite>(
            this, setup, TxDBName, TxDBPragma, TxDBInit)};
        getSession(tx) << boost::str(
            boost::format("PRAGMA cache_size=-%d;") %
            kilobytes(config.getValueFor(SizedItem::txnDBCache)));
        setupCheckpointing(tx, &app.getJobQueue(), app.logs());

        // ledger database
        SQLDatabase lgr{std::make_unique<SQLDatabase_sqlite>(
            this, setup, LgrDBName, LgrDBPragma, LgrDBInit)};
        getSession(lgr) << boost::str(
            boost::format("PRAGMA cache_size=-%d;") %
            kilobytes(config.getValueFor(SizedItem::lgrDBCache)));
        setupCheckpointing(lgr, &app.getJobQueue(), app.logs());

        if (setupFromConfig &&
            (!setup.standAlone || setup.startUp == Config::LOAD ||
             setup.startUp == Config::LOAD_FILE ||
             setup.startUp == Config::REPLAY))
        {
            // Check if AccountTransactions has primary key
            std::string cid, name, type;
            std::size_t notnull, dflt_value, pk;
            soci::indicator ind;
            soci::statement st =
                (getSession(tx).prepare
                     << ("PRAGMA table_info(AccountTransactions);"),
                 soci::into(cid),
                 soci::into(name),
                 soci::into(type),
                 soci::into(notnull),
                 soci::into(dflt_value, ind),
                 soci::into(pk));

            st.execute();
            while (st.fetch())
            {
                if (pk == 1)
                {
                    return {false, std::move(tx), std::move(lgr)};
                }
            }
        }

        addDatabase(&*tx, SQLInterface::TRANSACTION, shardIndex);
        addDatabase(&*lgr, SQLInterface::LEDGER, shardIndex);
        return {true, std::move(tx), std::move(lgr)};
    }
}

SQLDatabase
SQLInterface_sqlite::makeAcquireDB(
    Application& app,
    Config const& config,
    boost::filesystem::path const& dir)
{
    DatabaseCon::Setup setup;
    setup.startUp = config.START_UP;
    setup.standAlone = config.standalone();
    setup.dataDir = dir;
    setup.useGlobalPragma = true;

    SQLDatabase res{std::make_unique<SQLDatabase_sqlite>(
        this,
        setup,
        AcquireShardDBName,
        AcquireShardDBPragma,
        AcquireShardDBInit)};
    setupCheckpointing(res, &app.getJobQueue(), app.logs());

    return res;
}

SQLDatabase
SQLInterface_sqlite::makeWalletDB(
    bool setupFromConfig,
    Config const& config,
    beast::Journal const& j,
    std::string const& dbname,
    boost::filesystem::path const& dir)
{
    DatabaseCon::Setup setup;

    if (setupFromConfig)
    {
        setup = setup_DatabaseCon(config, j);
        setup.useGlobalPragma = false;
    }
    else
    {
        setup.dataDir = dir;
        assert(!setup.useGlobalPragma);
    }

    // wallet database
    SQLDatabase res{std::make_unique<SQLDatabase_sqlite>(
        this,
        setup,
        (setupFromConfig ? WalletDBName : dbname.data()),
        std::array<char const*, 0>(),
        WalletDBInit)};
    return res;
}

SQLDatabase
SQLInterface_sqlite::makeArchiveDB(
    boost::filesystem::path const& dir,
    std::string const& dbName)
{
    SQLDatabase res{std::make_unique<SQLDatabase_sqlite>(
        this, dir, dbName, DownloaderDBPragma, ShardArchiveHandlerDBInit)};
    return res;
}

void
SQLInterface_sqlite::initStateDB(
    SQLDatabase& db,
    BasicConfig const& config,
    std::string const& dbName)
{
    {
        SQLDatabase dbnew{std::make_unique<SQLDatabase_sqlite>(this)};
        db = std::move(dbnew);
    }
    soci::session& session = getSession(db);

    open(session, config, dbName);

    session << "PRAGMA synchronous=FULL;";

    session << "CREATE TABLE IF NOT EXISTS DbState ("
               "  Key                    INTEGER PRIMARY KEY,"
               "  WritableDb             TEXT,"
               "  ArchiveDb              TEXT,"
               "  LastRotatedLedger      INTEGER"
               ");";

    session << "CREATE TABLE IF NOT EXISTS CanDelete ("
               "  Key                    INTEGER PRIMARY KEY,"
               "  CanDeleteSeq           INTEGER"
               ");";

    std::int64_t count = 0;
    {
        boost::optional<std::int64_t> countO;
        session << "SELECT COUNT(Key) FROM DbState WHERE Key = 1;",
            soci::into(countO);
        if (!countO)
            Throw<std::runtime_error>(
                "Failed to fetch Key Count from DbState.");
        count = *countO;
    }

    if (!count)
    {
        session << "INSERT INTO DbState VALUES (1, '', '', 0);";
    }

    {
        boost::optional<std::int64_t> countO;
        session << "SELECT COUNT(Key) FROM CanDelete WHERE Key = 1;",
            soci::into(countO);
        if (!countO)
            Throw<std::runtime_error>(
                "Failed to fetch Key Count from CanDelete.");
        count = *countO;
    }

    if (!count)
    {
        session << "INSERT INTO CanDelete VALUES (1, 0);";
    }
}

std::pair<SQLDatabase, boost::optional<std::uint64_t>>
SQLInterface_sqlite::openDatabaseBodyDb(
    Config const& config,
    boost::filesystem::path const& path)
{
    boost::optional<std::string> pathFromDb;
    boost::optional<std::uint64_t> size;
    auto setup = setup_DatabaseCon(config);
    setup.dataDir = path.parent_path();
    setup.useGlobalPragma = false;

    SQLDatabase conn{std::make_unique<SQLDatabase_sqlite>(
        this, setup, "Download", DownloaderDBPragma, DatabaseBodyDBInit)};

    auto db = checkoutDb(conn);

    *db << "SELECT Path FROM Download WHERE Part=0;", soci::into(pathFromDb);

    // Try to reuse preexisting
    // database.
    if (pathFromDb)
    {
        // Can't resuse - database was
        // from a different file download.
        if (pathFromDb != path.string())
        {
            *db << "DROP TABLE Download;";
        }

        // Continuing a file download.
        else
        {
            *db << "SELECT SUM(LENGTH(Data)) FROM Download;", soci::into(size);
        }
    }

    return {std::move(conn), size};
}

bool
SQLInterface_sqlite::makeVacuumDB(Config const& config)
{
    using namespace boost::filesystem;
    DatabaseCon::Setup const dbSetup = setup_DatabaseCon(config);
    path dbPath = dbSetup.dataDir / TxDBName;

    uintmax_t const dbSize = file_size(dbPath);
    assert(dbSize != static_cast<uintmax_t>(-1));

    if (auto available = space(dbPath.parent_path()).available;
        available < dbSize)
    {
        std::cerr << "The database filesystem must have at least as "
                     "much free space as the size of "
                  << dbPath.string() << ", which is " << dbSize
                  << " bytes. Only " << available << " bytes are available.\n";
        return false;
    }

    SQLDatabase txnDB{std::make_unique<SQLDatabase_sqlite>(
        this, dbSetup, TxDBName, TxDBPragma, TxDBInit)};
    auto& session = getSession(txnDB);
    std::uint32_t pageSize;

    // Only the most trivial databases will fit in memory on typical
    // (recommended) software. Force temp files to be written to disk
    // regardless of the config settings.
    session << boost::format(CommonDBPragmaTemp) % "file";
    session << "PRAGMA page_size;", soci::into(pageSize);

    std::cout << "VACUUM beginning. page_size: " << pageSize << std::endl;

    session << "VACUUM;";
    assert(dbSetup.globalPragma);
    for (auto const& p : *dbSetup.globalPragma)
        session << p;
    session << "PRAGMA page_size;", soci::into(pageSize);

    std::cout << "VACUUM finished. page_size: " << pageSize << std::endl;

    return true;
}

void
SQLInterface_sqlite::initPeerFinderDB(
    SQLDatabase& db,
    BasicConfig const& config,
    beast::Journal const j)
{
    {
        SQLDatabase dbnew{std::make_unique<SQLDatabase_sqlite>(this)};
        db = std::move(dbnew);
    }
    soci::session& m_session = getSession(db);
    SociConfig m_sociConfig(config, "peerfinder");
    m_sociConfig.open(m_session);

    JLOG(j.info()) << "Opening database at '" << m_sociConfig.connectionString()
                   << "'";

    soci::transaction tr(m_session);
    m_session << "PRAGMA encoding=\"UTF-8\";";

    m_session << "CREATE TABLE IF NOT EXISTS SchemaVersion ( "
                 "  name             TEXT PRIMARY KEY, "
                 "  version          INTEGER"
                 ");";

    m_session << "CREATE TABLE IF NOT EXISTS PeerFinder_BootstrapCache ( "
                 "  id       INTEGER PRIMARY KEY AUTOINCREMENT, "
                 "  address  TEXT UNIQUE NOT NULL, "
                 "  valence  INTEGER"
                 ");";

    m_session << "CREATE INDEX IF NOT EXISTS "
                 "  PeerFinder_BootstrapCache_Index ON "
                 "PeerFinder_BootstrapCache "
                 "  (  "
                 "    address "
                 "  ); ";

    tr.commit();
}

void
SQLInterface_sqlite::updatePeerFinderDB(
    SQLDatabase& db,
    int currentSchemaVersion,
    beast::Journal const j)
{
    soci::session& m_session = getSession(db);
    soci::transaction tr(m_session);
    // get version
    int version(0);
    {
        boost::optional<int> vO;
        m_session << "SELECT "
                     "  version "
                     "FROM SchemaVersion WHERE "
                     "  name = 'PeerFinder';",
            soci::into(vO);

        version = vO.value_or(0);

        JLOG(j.info()) << "Opened version " << version << " database";
    }

    {
        if (version < currentSchemaVersion)
        {
            JLOG(j.info()) << "Updating database to version "
                           << currentSchemaVersion;
        }
        else if (version > currentSchemaVersion)
        {
            Throw<std::runtime_error>(
                "The PeerFinder database version is higher than expected");
        }
    }

    if (version < 4)
    {
        //
        // Remove the "uptime" column from the bootstrap table
        //

        m_session << "CREATE TABLE IF NOT EXISTS "
                     "PeerFinder_BootstrapCache_Next ( "
                     "  id       INTEGER PRIMARY KEY AUTOINCREMENT, "
                     "  address  TEXT UNIQUE NOT NULL, "
                     "  valence  INTEGER"
                     ");";

        m_session << "CREATE INDEX IF NOT EXISTS "
                     "  PeerFinder_BootstrapCache_Next_Index ON "
                     "    PeerFinder_BootstrapCache_Next "
                     "  ( address ); ";

        std::size_t count;
        m_session << "SELECT COUNT(*) FROM PeerFinder_BootstrapCache;",
            soci::into(count);

        std::vector<PeerFinder::Store::Entry> list;

        {
            list.reserve(count);
            std::string s;
            int valence;
            soci::statement st =
                (m_session.prepare << "SELECT "
                                      " address, "
                                      " valence "
                                      "FROM PeerFinder_BootstrapCache;",
                 soci::into(s),
                 soci::into(valence));

            st.execute();
            while (st.fetch())
            {
                PeerFinder::Store::Entry entry;
                entry.endpoint = beast::IP::Endpoint::from_string(s);
                if (!is_unspecified(entry.endpoint))
                {
                    entry.valence = valence;
                    list.push_back(entry);
                }
                else
                {
                    JLOG(j.error()) << "Bad address string '" << s
                                    << "' in Bootcache table";
                }
            }
        }

        if (!list.empty())
        {
            std::vector<std::string> s;
            std::vector<int> valence;
            s.reserve(list.size());
            valence.reserve(list.size());

            for (auto iter(list.cbegin()); iter != list.cend(); ++iter)
            {
                s.emplace_back(to_string(iter->endpoint));
                valence.emplace_back(iter->valence);
            }

            m_session << "INSERT INTO PeerFinder_BootstrapCache_Next ( "
                         "  address, "
                         "  valence "
                         ") VALUES ( "
                         "  :s, :valence"
                         ");",
                soci::use(s), soci::use(valence);
        }

        m_session << "DROP TABLE IF EXISTS PeerFinder_BootstrapCache;";

        m_session << "DROP INDEX IF EXISTS PeerFinder_BootstrapCache_Index;";

        m_session << "ALTER TABLE PeerFinder_BootstrapCache_Next "
                     "  RENAME TO PeerFinder_BootstrapCache;";

        m_session << "CREATE INDEX IF NOT EXISTS "
                     "  PeerFinder_BootstrapCache_Index ON "
                     "PeerFinder_BootstrapCache "
                     "  (  "
                     "    address "
                     "  ); ";
    }

    if (version < 3)
    {
        //
        // Remove legacy endpoints from the schema
        //

        m_session << "DROP TABLE IF EXISTS LegacyEndpoints;";

        m_session << "DROP TABLE IF EXISTS PeerFinderLegacyEndpoints;";

        m_session << "DROP TABLE IF EXISTS PeerFinder_LegacyEndpoints;";

        m_session << "DROP TABLE IF EXISTS PeerFinder_LegacyEndpoints_Index;";
    }

    {
        int const v(currentSchemaVersion);
        m_session << "INSERT OR REPLACE INTO SchemaVersion ("
                     "   name "
                     "  ,version "
                     ") VALUES ( "
                     "  'PeerFinder', :version "
                     ");",
            soci::use(v);
    }

    tr.commit();
}

boost::optional<LedgerIndex>
SQLInterface_sqlite::getMinLedgerSeq(SQLDatabase_* db, TableType type)
{
    std::string query = "SELECT MIN(LedgerSeq) FROM " + tableName(type) + ";";
    auto cdb = checkoutDb(db);
    boost::optional<LedgerIndex> m;
    *cdb << query, soci::into(m);
    return m;
}

boost::optional<LedgerIndex>
SQLInterface_sqlite::getMinLedgerSeq(SQLDatabase& db, TableType type)
{
    /* if databases exists, use it */
    if (exists(db))
    {
        return getMinLedgerSeq(&*db, type);
    }

    /* else use shard databases */
    boost::optional<LedgerIndex> res;
    iterate_forward(&*db, -1u, [&](SQLDatabase_* sdb, LedgerIndex index) {
        res = sdb->getInterface()->getMinLedgerSeq(sdb, type);
        return res ? false : true;
    });
    return res;
}

boost::optional<LedgerIndex>
SQLInterface_sqlite::getMaxLedgerSeq(SQLDatabase_* db, TableType type)
{
    std::string query = "SELECT MAX(LedgerSeq) FROM " + tableName(type) + ";";
    auto cdb = checkoutDb(db);
    boost::optional<LedgerIndex> m;
    *cdb << query, soci::into(m);
    return m;
}

boost::optional<LedgerIndex>
SQLInterface_sqlite::getMaxLedgerSeq(SQLDatabase& db, TableType type)
{
    /* if databases exists, use it */
    if (exists(db))
    {
        return getMaxLedgerSeq(&*db, type);
    }

    /* else use shard databases */
    boost::optional<LedgerIndex> res;
    iterate_back(&*db, -1u, [&](SQLDatabase_* sdb, LedgerIndex index) {
        res = sdb->getInterface()->getMaxLedgerSeq(sdb, type);
        return res ? false : true;
    });
    return res;
}

void
SQLInterface_sqlite::deleteByLedgerSeq(
    SQLDatabase_* db,
    TableType type,
    LedgerIndex ledgerSeq)
{
    auto cdb = checkoutDb(db);
    *cdb << "DELETE FROM " << tableName(type)
         << " WHERE LedgerSeq == " << ledgerSeq << ";";
}

void
SQLInterface_sqlite::deleteByLedgerSeq(
    SQLDatabase& db,
    TableType type,
    LedgerIndex ledgerSeq)
{
    /* if databases exists, use it */
    if (exists(db))
        deleteByLedgerSeq(&*db, type, ledgerSeq);

    /* else use shard database */
    SQLDatabase_* sdb = findShardDatabase(&*db, ledgerSeq);
    if (sdb)
        sdb->getInterface()->deleteByLedgerSeq(sdb, type, ledgerSeq);
}

void
SQLInterface_sqlite::deleteBeforeLedgerSeq(
    SQLDatabase_* db,
    TableType type,
    LedgerIndex ledgerSeq)
{
    std::string query =
        "DELETE FROM " + tableName(type) + " WHERE LedgerSeq < %u;";
    boost::format formattedQuery(query);

    auto cdb = checkoutDb(db);
    *cdb << boost::str(formattedQuery % ledgerSeq);
}

void
SQLInterface_sqlite::deleteBeforeLedgerSeq(
    SQLDatabase& db,
    TableType type,
    LedgerIndex ledgerSeq)
{
    /* if databases exists, use it */
    if (exists(db))
        deleteBeforeLedgerSeq(&*db, type, ledgerSeq);

    /* else use shard databases */
    iterate_back(
        &*db, ledgerSeq - 1, [&](SQLDatabase_* sdb, LedgerIndex index) {
            sdb->getInterface()->deleteBeforeLedgerSeq(sdb, type, ledgerSeq);
            return true;
        });
}

int
SQLInterface_sqlite::getRows(SQLDatabase_* db, TableType type)
{
    auto cdb = checkoutDb(db);

    int rows;
    *cdb << "SELECT count(*) AS rows "
            "FROM "
         << tableName(type) << ";",
        soci::into(rows);

    return rows;
}

int
SQLInterface_sqlite::getRows(SQLDatabase& db, TableType type)
{
    /* if databases exists, use it */
    if (exists(db))
        return getRows(&*db, type);

    /* else use shard databases */
    int rows = 0;
    iterate_forward(&*db, -1u, [&](SQLDatabase_* sdb, LedgerIndex index) {
        rows += sdb->getInterface()->getRows(sdb, type);
        return true;
    });
    return rows;
}

std::tuple<int, int, int>
SQLInterface_sqlite::getRowsMinMax(SQLDatabase_* db, TableType type)
{
    auto cdb = checkoutDb(db);

    int rows, first, last;
    *cdb << "SELECT count(*) AS rows, "
            "min(LedgerSeq) as first, "
            "max(LedgerSeq) as last "
            "FROM "
         << tableName(type) << ";",
        soci::into(rows), soci::into(first), soci::into(last);

    return {rows, first, last};
}

std::tuple<int, int, int>
SQLInterface_sqlite::getRowsMinMax(SQLDatabase& db, TableType type)
{
    /* if databases exists, use it */
    if (exists(db))
        return getRowsMinMax(&*db, type);

    /* else use shard databases */
    int rows = 0, first = -1u, last = -1u;
    iterate_forward(&*db, -1u, [&](SQLDatabase_* sdb, LedgerIndex index) {
        auto [r, f, l] = sdb->getInterface()->getRowsMinMax(sdb, type);
        if (r)
        {
            rows += r;
            if (first == -1u)
                first = f;
            last = l;
        }
        return true;
    });
    return {rows, first, last};
}

void
SQLInterface_sqlite::insertAcquireDBIndex(SQLDatabase& db, std::uint32_t index_)
{
    getSession(db) << "INSERT INTO Shard (ShardIndex) "
                      "VALUES (:shardIndex);",
        soci::use(index_);
}

std::pair<bool, boost::optional<std::string>>
SQLInterface_sqlite::selectAcquireDBLedgerSeqs(
    SQLDatabase& db,
    std::uint32_t index)
{
    auto& session{getSession(db)};
    boost::optional<std::uint32_t> resIndex;
    soci::blob sociBlob(session);
    soci::indicator blobPresent;

    session << "SELECT ShardIndex, StoredLedgerSeqs "
               "FROM Shard "
               "WHERE ShardIndex = :index;",
        soci::into(resIndex), soci::into(sociBlob, blobPresent),
        soci::use(index);

    if (!resIndex || index != resIndex)
        return {false, {}};

    if (blobPresent != soci::i_ok)
        return {true, {}};

    std::string s;
    convert(sociBlob, s);

    return {true, s};
}

std::tuple<bool, boost::optional<std::string>, boost::optional<std::string>>
SQLInterface_sqlite::selectAcquireDBLedgerSeqsHash(
    SQLDatabase& db,
    std::uint32_t index)
{
    auto& session{getSession(db)};
    boost::optional<std::uint32_t> resIndex;
    boost::optional<std::string> sHash;
    soci::blob sociBlob(session);
    soci::indicator blobPresent;

    session << "SELECT ShardIndex, LastLedgerHash, StoredLedgerSeqs "
               "FROM Shard "
               "WHERE ShardIndex = :index;",
        soci::into(resIndex), soci::into(sHash),
        soci::into(sociBlob, blobPresent), soci::use(index);

    if (!resIndex || index != resIndex)
        return {false, {}, {}};

    if (blobPresent != soci::i_ok)
        return {true, {}, sHash};

    std::string s;
    convert(sociBlob, s);

    return {true, s, sHash};
}

bool
SQLInterface_sqlite::updateLedgerDBs(
    SQLDatabase& txdb,
    SQLDatabase& lgrdb,
    std::shared_ptr<Ledger const> const& ledger,
    std::uint32_t const index,
    beast::Journal const j,
    std::atomic<bool>& stop)
{
    auto const seq{ledger->info().seq};

    // Update the transactions database
    {
        auto& session{getSession(txdb)};
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
                JLOG(j.error())
                    << "shard " << index << " has an invalid transaction map"
                    << " on sequence " << sSeq;
                return false;
            }

            for (auto const& item : ledger->txs)
            {
                if (stop)
                    return false;

                auto const txID{item.first->getTransactionID()};
                auto const sTxID{to_string(txID)};
                auto const txMeta{std::make_shared<TxMeta>(
                    txID, ledger->seq(), *item.second)};

                session << "DELETE FROM AccountTransactions "
                           "WHERE TransID = :txID;",
                    soci::use(sTxID);

                auto const& accounts = txMeta->getAffectedAccounts(j);
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

                    JLOG(j.trace())
                        << "shard " << index << " account transaction: " << sql;
                }
                else
                {
                    JLOG(j.warn())
                        << "shard " << index << " transaction in ledger "
                        << sSeq << " affects no accounts";
                }

                Serializer s;
                item.second->add(s);
                session
                    << (STTx::getMetaSQLInsertReplaceHeader() +
                        item.first->getMetaSQL(seq, sqlEscape(s.modData())) +
                        ';');
            }
        }

        tr.commit();
    }

    auto const sHash{to_string(ledger->info().hash)};

    // Update the ledger database
    {
        auto& session{getSession(lgrdb)};
        soci::transaction tr(session);

        auto const sParentHash{to_string(ledger->info().parentHash)};
        auto const sDrops{to_string(ledger->info().drops)};
        auto const sAccountHash{to_string(ledger->info().accountHash)};
        auto const sTxHash{to_string(ledger->info().txHash)};

        session << "DELETE FROM Ledgers "
                   "WHERE LedgerSeq = :seq;",
            soci::use(seq);
        session << "INSERT OR REPLACE INTO Ledgers ("
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

    return true;
}

void
SQLInterface_sqlite::updateAcquireDB(
    SQLDatabase& db,
    std::shared_ptr<Ledger const> const& ledger,
    std::uint32_t const index,
    std::uint32_t const lastSeq,
    boost::optional<std::string> seqs)
{
    auto& session{getSession(db)};
    soci::blob sociBlob(session);
    auto const sHash{to_string(ledger->info().hash)};

    if (seqs)
        convert(*seqs, sociBlob);

    if (ledger->info().seq == lastSeq)
    {
        // Store shard's last ledger hash
        session << "UPDATE Shard "
                   "SET LastLedgerHash = :lastLedgerHash,"
                   "StoredLedgerSeqs = :storedLedgerSeqs "
                   "WHERE ShardIndex = :shardIndex;",
            soci::use(sHash), soci::use(sociBlob), soci::use(index);
    }
    else
    {
        session << "UPDATE Shard "
                   "SET StoredLedgerSeqs = :storedLedgerSeqs "
                   "WHERE ShardIndex = :shardIndex;",
            soci::use(sociBlob), soci::use(index);
    }
}

bool
SQLInterface_sqlite::saveValidatedLedger(
    SQLDatabase_* ldgDB,
    SQLDatabase_* txnDB,
    Application& app,
    std::shared_ptr<Ledger const> const& ledger,
    bool current)
{
    auto j = app.journal("Ledger");
    auto seq = ledger->info().seq;

    // TODO(tom): Fix this hard-coded SQL!
    JLOG(j.trace()) << "saveValidatedLedger " << (current ? "" : "fromAcquire ")
                    << seq;
    static boost::format deleteLedger(
        "DELETE FROM Ledgers WHERE LedgerSeq = %u;");
    static boost::format deleteTrans1(
        "DELETE FROM Transactions WHERE LedgerSeq = %u;");
    static boost::format deleteTrans2(
        "DELETE FROM AccountTransactions WHERE LedgerSeq = %u;");
    static boost::format deleteAcctTrans(
        "DELETE FROM AccountTransactions WHERE TransID = '%s';");

    if (!ledger->info().accountHash.isNonZero())
    {
        JLOG(j.fatal()) << "AH is zero: " << getJson(*ledger);
        assert(false);
    }

    if (ledger->info().accountHash != ledger->stateMap().getHash().as_uint256())
    {
        JLOG(j.fatal()) << "sAL: " << ledger->info().accountHash
                        << " != " << ledger->stateMap().getHash();
        JLOG(j.fatal()) << "saveAcceptedLedger: seq=" << seq
                        << ", current=" << current;
        assert(false);
    }

    assert(ledger->info().txHash == ledger->txMap().getHash().as_uint256());

    // Save the ledger header in the hashed object store
    {
        Serializer s(128);
        s.add32(HashPrefix::ledgerMaster);
        addRaw(ledger->info(), s);
        app.getNodeStore().store(
            hotLEDGER, std::move(s.modData()), ledger->info().hash, seq);
    }

    AcceptedLedger::pointer aLedger;
    try
    {
        aLedger = app.getAcceptedLedgerCache().fetch(ledger->info().hash);
        if (!aLedger)
        {
            aLedger = std::make_shared<AcceptedLedger>(
                ledger, app.accountIDCache(), app.logs());
            app.getAcceptedLedgerCache().canonicalize_replace_client(
                ledger->info().hash, aLedger);
        }
    }
    catch (std::exception const&)
    {
        JLOG(j.warn()) << "An accepted ledger was missing nodes";
        app.getLedgerMaster().failedSave(seq, ledger->info().hash);
        return false;
    }

    {
        auto db = checkoutDb(ldgDB);
        *db << boost::str(deleteLedger % seq);
    }

    {
        auto db = checkoutDb(txnDB);

        soci::transaction tr(*db);

        *db << boost::str(deleteTrans1 % seq);
        *db << boost::str(deleteTrans2 % seq);

        std::string const ledgerSeq(std::to_string(seq));

        for (auto const& [_, acceptedLedgerTx] : aLedger->getMap())
        {
            (void)_;
            uint256 transactionID = acceptedLedgerTx->getTransactionID();

            app.getMasterTransaction().inLedger(transactionID, seq);

            std::string const txnId(to_string(transactionID));
            std::string const txnSeq(
                std::to_string(acceptedLedgerTx->getTxnSeq()));

            *db << boost::str(deleteAcctTrans % transactionID);

            auto const& accts = acceptedLedgerTx->getAffected();

            if (!accts.empty())
            {
                std::string sql(
                    "INSERT INTO AccountTransactions "
                    "(TransID, Account, LedgerSeq, TxnSeq) VALUES ");

                // Try to make an educated guess on how much space we'll need
                // for our arguments. In argument order we have:
                // 64 + 34 + 10 + 10 = 118 + 10 extra = 128 bytes
                sql.reserve(sql.length() + (accts.size() * 128));

                bool first = true;
                for (auto const& account : accts)
                {
                    if (!first)
                        sql += ", ('";
                    else
                    {
                        sql += "('";
                        first = false;
                    }

                    sql += txnId;
                    sql += "','";
                    sql += app.accountIDCache().toBase58(account);
                    sql += "',";
                    sql += ledgerSeq;
                    sql += ",";
                    sql += txnSeq;
                    sql += ")";
                }
                sql += ";";
                JLOG(j.trace()) << "ActTx: " << sql;
                *db << sql;
            }
            else
            {
                JLOG(j.warn()) << "Transaction in ledger " << seq
                               << " affects no accounts";
                JLOG(j.warn())
                    << acceptedLedgerTx->getTxn()->getJson(JsonOptions::none);
            }

            *db
                << (STTx::getMetaSQLInsertReplaceHeader() +
                    acceptedLedgerTx->getTxn()->getMetaSQL(
                        seq, acceptedLedgerTx->getEscMeta()) +
                    ";");
        }

        tr.commit();
    }

    {
        static std::string addLedger(
            R"sql(INSERT OR REPLACE INTO Ledgers
                (LedgerHash,LedgerSeq,PrevHash,TotalCoins,ClosingTime,PrevClosingTime,
                CloseTimeRes,CloseFlags,AccountSetHash,TransSetHash)
            VALUES
                (:ledgerHash,:ledgerSeq,:prevHash,:totalCoins,:closingTime,:prevClosingTime,
                :closeTimeRes,:closeFlags,:accountSetHash,:transSetHash);)sql");

        auto db(checkoutDb(ldgDB));

        soci::transaction tr(*db);

        auto const hash = to_string(ledger->info().hash);
        auto const parentHash = to_string(ledger->info().parentHash);
        auto const drops = to_string(ledger->info().drops);
        auto const closeTime =
            ledger->info().closeTime.time_since_epoch().count();
        auto const parentCloseTime =
            ledger->info().parentCloseTime.time_since_epoch().count();
        auto const closeTimeResolution =
            ledger->info().closeTimeResolution.count();
        auto const closeFlags = ledger->info().closeFlags;
        auto const accountHash = to_string(ledger->info().accountHash);
        auto const txHash = to_string(ledger->info().txHash);

        *db << addLedger, soci::use(hash), soci::use(seq),
            soci::use(parentHash), soci::use(drops), soci::use(closeTime),
            soci::use(parentCloseTime), soci::use(closeTimeResolution),
            soci::use(closeFlags), soci::use(accountHash), soci::use(txHash);

        tr.commit();
    }

    return true;
}

bool
SQLInterface_sqlite::saveValidatedLedger(
    SQLDatabase& ldgDB,
    SQLDatabase& txnDB,
    Application& app,
    std::shared_ptr<Ledger const> const& ledger,
    bool current)
{
    /* if databases exists, use it */
    if (exists(ldgDB) && exists(txnDB))
        return saveValidatedLedger(&*ldgDB, &*txnDB, app, ledger, current);

    /* else use shard databases */
    auto seq = ledger->info().seq;
    SQLDatabase_* ldg = findShardDatabase(&*ldgDB, seq);
    SQLDatabase_* txn = findShardDatabase(&*txnDB, seq);
    if (ldg && txn)
        return ldg->getInterface()->saveValidatedLedger(
            ldg, txn, app, ledger, current);

    return false;
}

bool
SQLInterface_sqlite::loadLedgerInfo(
    SQLDatabase_* db,
    SQLInterface::SQLLedgerInfo& info,
    beast::Journal const& j,
    std::string const& sqlSuffix)
{
    auto cdb = checkoutDb(db);

    std::string const sql =
        "SELECT "
        "LedgerHash, PrevHash, AccountSetHash, TransSetHash, "
        "TotalCoins,"
        "ClosingTime, PrevClosingTime, CloseTimeRes, CloseFlags,"
        "LedgerSeq from Ledgers " +
        sqlSuffix + ";";

    *cdb << sql, soci::into(info.sLedgerHash), soci::into(info.sPrevHash),
        soci::into(info.sAccountHash), soci::into(info.sTransHash),
        soci::into(info.totDrops), soci::into(info.closingTime),
        soci::into(info.prevClosingTime), soci::into(info.closeResolution),
        soci::into(info.closeFlags), soci::into(info.ledgerSeq64);

    if (!cdb->got_data())
    {
        auto stream = j.debug();
        JLOG(stream) << "Ledger not found: " << sqlSuffix;
        return false;
    }

    return true;
}

bool
SQLInterface_sqlite::loadLedgerInfoByIndex(
    SQLDatabase_* db,
    SQLInterface::SQLLedgerInfo& info,
    beast::Journal const& j,
    LedgerIndex ledgerSeq)
{
    std::ostringstream s;
    s << "WHERE LedgerSeq = " << ledgerSeq;
    return loadLedgerInfo(db, info, j, s.str());
}

bool
SQLInterface_sqlite::loadLedgerInfoByIndex(
    SQLDatabase& db,
    SQLInterface::SQLLedgerInfo& info,
    beast::Journal const& j,
    LedgerIndex ledgerSeq)
{
    /* if databases exists, use it */
    if (exists(db))
        return loadLedgerInfoByIndex(&*db, info, j, ledgerSeq);

    /* else use shard databases */
    SQLDatabase_* sdb = findShardDatabase(&*db, ledgerSeq);
    if (sdb)
        return sdb->getInterface()->loadLedgerInfoByIndex(
            sdb, info, j, ledgerSeq);

    return false;
}

bool
SQLInterface_sqlite::loadLedgerInfoByIndexSorted(
    SQLDatabase_* db,
    SQLInterface::SQLLedgerInfo& info,
    beast::Journal const& j,
    bool ascendSort)
{
    std::ostringstream s;
    s << "order by LedgerSeq " << (ascendSort ? "asc" : "desc") << " limit 1";
    return loadLedgerInfo(db, info, j, s.str());
}

bool
SQLInterface_sqlite::loadLedgerInfoByIndexSorted(
    SQLDatabase& db,
    SQLInterface::SQLLedgerInfo& info,
    beast::Journal const& j,
    bool ascendSort)
{
    /* if databases exists, use it */
    if (exists(db))
        return loadLedgerInfoByIndexSorted(&*db, info, j, ascendSort);

    /* else use shard databases */
    bool res = false;
    (ascendSort ? iterate_forward : iterate_back)(
        &*db, -1u, [&](SQLDatabase_* sdb, LedgerIndex index) {
            if (sdb->getInterface()->loadLedgerInfoByIndexSorted(
                    sdb, info, j, ascendSort))
            {
                res = true;
                return false;
            }
            return true;
        });

    return res;
}

bool
SQLInterface_sqlite::loadLedgerInfoByIndexLimitedSorted(
    SQLDatabase_* db,
    SQLInterface::SQLLedgerInfo& info,
    beast::Journal const& j,
    LedgerIndex ledgerFirstIndex,
    bool ascendSort)
{
    std::ostringstream s;
    s << "WHERE LedgerSeq >= " + std::to_string(ledgerFirstIndex) +
            " order by LedgerSeq " + (ascendSort ? "asc" : "desc") + " limit 1";
    return loadLedgerInfo(db, info, j, s.str());
}

bool
SQLInterface_sqlite::loadLedgerInfoByIndexLimitedSorted(
    SQLDatabase& db,
    SQLInterface::SQLLedgerInfo& info,
    beast::Journal const& j,
    LedgerIndex ledgerFirstIndex,
    bool ascendSort)
{
    /* if databases exists, use it */
    if (exists(db))
        return loadLedgerInfoByIndexLimitedSorted(
            &*db, info, j, ledgerFirstIndex, ascendSort);

    /* else use shard databases */
    bool res = false;
    if (ascendSort)
    {
        iterate_forward(
            &*db,
            SQLInterface::seqToShardIndex(ledgerFirstIndex),
            [&](SQLDatabase_* sdb, LedgerIndex index) {
                if (sdb->getInterface()->loadLedgerInfoByIndexLimitedSorted(
                        sdb, info, j, ledgerFirstIndex, ascendSort))
                {
                    res = true;
                    return false;
                }
                return true;
            });
    }
    else
    {
        iterate_back(&*db, -1u, [&](SQLDatabase_* sdb, LedgerIndex index) {
            if (sdb->getInterface()->loadLedgerInfoByIndexLimitedSorted(
                    sdb, info, j, ledgerFirstIndex, ascendSort))
            {
                res = true;
                return false;
            }
            if (index < SQLInterface::seqToShardIndex(ledgerFirstIndex))
                return false;
            return true;
        });
    }

    return res;
}

bool
SQLInterface_sqlite::loadLedgerInfoByHash(
    SQLDatabase_* db,
    SQLInterface::SQLLedgerInfo& info,
    beast::Journal const& j,
    uint256 const& ledgerHash)
{
    std::ostringstream s;
    s << "WHERE LedgerHash = '" << ledgerHash << "'";
    return loadLedgerInfo(db, info, j, s.str());
}

bool
SQLInterface_sqlite::loadLedgerInfoByHash(
    SQLDatabase& db,
    SQLInterface::SQLLedgerInfo& info,
    beast::Journal const& j,
    uint256 const& ledgerHash)
{
    /* if databases exists, use it */
    if (exists(db))
        return loadLedgerInfoByHash(&*db, info, j, ledgerHash);

    /* else use shard databases */
    bool res = false;
    iterate_back(&*db, -1u, [&](SQLDatabase_* sdb, LedgerIndex index) {
        if (sdb->getInterface()->loadLedgerInfoByHash(sdb, info, j, ledgerHash))
        {
            res = true;
            return false;
        }
        return true;
    });

    return res;
}

uint256
SQLInterface_sqlite::getHashByIndex(SQLDatabase_* db, LedgerIndex ledgerIndex)
{
    uint256 ret;

    std::string sql =
        "SELECT LedgerHash FROM Ledgers INDEXED BY SeqLedger WHERE LedgerSeq='";
    sql.append(beast::lexicalCastThrow<std::string>(ledgerIndex));
    sql.append("';");

    std::string hash;
    {
        auto cdb = checkoutDb(db);

        boost::optional<std::string> lh;
        *cdb << sql, soci::into(lh);

        if (!cdb->got_data() || !lh)
            return ret;

        hash = *lh;
        if (hash.empty())
            return ret;
    }

    ret.SetHexExact(hash);
    return ret;
}

uint256
SQLInterface_sqlite::getHashByIndex(SQLDatabase& db, LedgerIndex ledgerIndex)
{
    /* if database exists, use it */
    if (exists(db))
        return getHashByIndex(&*db, ledgerIndex);

    /* else use shard database */
    SQLDatabase_* sdb = findShardDatabase(&*db, ledgerIndex);
    if (sdb)
        return sdb->getInterface()->getHashByIndex(sdb, ledgerIndex);

    return uint256();
}

bool
SQLInterface_sqlite::getHashesByIndex(
    SQLDatabase_* db,
    beast::Journal const& j,
    LedgerIndex ledgerIndex,
    uint256& ledgerHash,
    uint256& parentHash)
{
    auto cdb = checkoutDb(db);

    boost::optional<std::string> lhO, phO;

    *cdb << "SELECT LedgerHash,PrevHash FROM Ledgers "
            "INDEXED BY SeqLedger Where LedgerSeq = :ls;",
        soci::into(lhO), soci::into(phO), soci::use(ledgerIndex);

    if (!lhO || !phO)
    {
        auto stream = j.trace();
        JLOG(stream) << "Don't have ledger " << ledgerIndex;
        return false;
    }

    ledgerHash.SetHexExact(*lhO);
    parentHash.SetHexExact(*phO);

    return true;
}

bool
SQLInterface_sqlite::getHashesByIndex(
    SQLDatabase& db,
    beast::Journal const& j,
    LedgerIndex ledgerIndex,
    uint256& ledgerHash,
    uint256& parentHash)
{
    /* if database exists, use it */
    if (exists(db))
        return getHashesByIndex(&*db, j, ledgerIndex, ledgerHash, parentHash);

    /* else use shard database */
    SQLDatabase_* sdb = findShardDatabase(&*db, ledgerIndex);
    if (sdb)
        return sdb->getInterface()->getHashesByIndex(
            sdb, j, ledgerIndex, ledgerHash, parentHash);

    return false;
}

void
SQLInterface_sqlite::getHashesByIndex(
    SQLDatabase_* db,
    beast::Journal const& j,
    LedgerIndex minSeq,
    LedgerIndex maxSeq,
    std::map<LedgerIndex, std::pair<uint256, uint256>>& ret)
{
    std::string sql =
        "SELECT LedgerSeq,LedgerHash,PrevHash FROM Ledgers WHERE LedgerSeq >= ";
    sql.append(beast::lexicalCastThrow<std::string>(minSeq));
    sql.append(" AND LedgerSeq <= ");
    sql.append(beast::lexicalCastThrow<std::string>(maxSeq));
    sql.append(";");

    auto cdb = checkoutDb(db);

    std::uint64_t ls;
    std::string lh;
    boost::optional<std::string> ph;
    soci::statement st =
        (cdb->prepare << sql, soci::into(ls), soci::into(lh), soci::into(ph));

    st.execute();
    while (st.fetch())
    {
        std::pair<uint256, uint256>& hashes =
            ret[rangeCheckedCast<LedgerIndex>(ls)];
        hashes.first.SetHexExact(lh);
        if (ph)
            hashes.second.SetHexExact(*ph);
        else
            hashes.second.zero();
        if (!ph)
        {
            auto stream = j.warn();
            JLOG(stream) << "Null prev hash for ledger seq: " << ls;
        }
    }
}

std::map<LedgerIndex, std::pair<uint256, uint256>>
SQLInterface_sqlite::getHashesByIndex(
    SQLDatabase& db,
    beast::Journal const& j,
    LedgerIndex minSeq,
    LedgerIndex maxSeq)
{
    std::map<LedgerIndex, std::pair<uint256, uint256>> ret;

    /* if database exists, use it */
    if (exists(db))
    {
        getHashesByIndex(&*db, j, minSeq, maxSeq, ret);
        return ret;
    }

    /* else use shard databases */
    while (minSeq <= maxSeq)
    {
        SQLDatabase_* sdb = findShardDatabase(&*db, minSeq);
        LedgerIndex shardMaxSeq = lastLedgerSeq(seqToShardIndex(minSeq));
        if (shardMaxSeq > maxSeq)
            shardMaxSeq = maxSeq;
        if (sdb)
            sdb->getInterface()->getHashesByIndex(
                sdb, j, minSeq, shardMaxSeq, ret);
        minSeq = shardMaxSeq + 1;
    }

    return ret;
}

int
SQLInterface_sqlite::loadTxHistory(
    SQLDatabase_* db,
    Application& app,
    Json::Value& txs,
    LedgerIndex startIndex,
    int quantity,
    bool count)
{
    std::string sql = boost::str(
        boost::format(
            "SELECT LedgerSeq, Status, RawTxn "
            "FROM Transactions ORDER BY LedgerSeq desc LIMIT %u,%u;") %
        startIndex % quantity);

    int total = 0;

    {
        auto cdb = checkoutDb(db);

        boost::optional<std::uint64_t> ledgerSeq;
        boost::optional<std::string> status;
        soci::blob sociRawTxnBlob(*cdb);
        soci::indicator rti;
        Blob rawTxn;

        soci::statement st =
            (cdb->prepare << sql,
             soci::into(ledgerSeq),
             soci::into(status),
             soci::into(sociRawTxnBlob, rti));

        st.execute();
        while (st.fetch())
        {
            if (soci::i_ok == rti)
                convert(sociRawTxnBlob, rawTxn);
            else
                rawTxn.clear();

            if (auto trans = Transaction::transactionFromSQL(
                    ledgerSeq, status, rawTxn, app))
            {
                total++;
                txs.append(trans->getJson(JsonOptions::none));
            }
        }

        if (!total && count)
        {
            *cdb << "SELECT count(*) FROM Transactions;", soci::into(total);

            total = -total;
        }
    }

    return total;
}

Json::Value
SQLInterface_sqlite::loadTxHistory(
    SQLDatabase& db,
    Application& app,
    LedgerIndex startIndex)
{
    Json::Value txs;

    /* if databases exists, use it */
    if (exists(db))
    {
        loadTxHistory(&*db, app, txs, startIndex, 20, false);
        return txs;
    }

    /* else use shard databases */
    int quantity = 20;
    iterate_back(&*db, -1u, [&](SQLDatabase_* sdb, LedgerIndex index) {
        int total = sdb->getInterface()->loadTxHistory(
            sdb, app, txs, startIndex, quantity, true);
        if (total > 0)
        {
            quantity -= total;
            if (quantity <= 0)
                return false;
            startIndex = 0;
        }
        else
        {
            startIndex += total;
        }
        return true;
    });

    return txs;
}

std::string
SQLInterface_sqlite::transactionsSQL(
    Application& app,
    beast::Journal& j,
    AccountID const& account,
    std::string selection,
    std::int32_t minLedger,
    std::int32_t maxLedger,
    bool descending,
    std::uint32_t offset,
    int limit,
    int limit_used,
    bool binary,
    bool count,
    bool bUnlimited)
{
    std::uint32_t NONBINARY_PAGE_LENGTH = 200;
    std::uint32_t BINARY_PAGE_LENGTH = 500;

    std::uint32_t numberOfResults;

    if (count)
    {
        numberOfResults = 1000000000;
    }
    else if (limit < 0)
    {
        numberOfResults = binary ? BINARY_PAGE_LENGTH : NONBINARY_PAGE_LENGTH;
    }
    else if (!bUnlimited)
    {
        numberOfResults = std::min(
            binary ? BINARY_PAGE_LENGTH : NONBINARY_PAGE_LENGTH,
            static_cast<std::uint32_t>(limit));
    }
    else
    {
        numberOfResults = limit;
    }

    if (limit_used >= 0)
    {
        if (numberOfResults <= limit_used)
            return "";
        else
            numberOfResults -= limit_used;
    }

    std::string maxClause = "";
    std::string minClause = "";

    if (maxLedger != -1)
    {
        maxClause = boost::str(
            boost::format("AND AccountTransactions.LedgerSeq <= '%u'") %
            maxLedger);
    }

    if (minLedger != -1)
    {
        minClause = boost::str(
            boost::format("AND AccountTransactions.LedgerSeq >= '%u'") %
            minLedger);
    }

    std::string sql;

    if (count)
        sql = boost::str(
            boost::format("SELECT %s FROM AccountTransactions "
                          "WHERE Account = '%s' %s %s LIMIT %u, %u;") %
            selection % app.accountIDCache().toBase58(account) % maxClause %
            minClause % beast::lexicalCastThrow<std::string>(offset) %
            beast::lexicalCastThrow<std::string>(numberOfResults));
    else
        sql = boost::str(
            boost::format(
                "SELECT %s FROM "
                "AccountTransactions INNER JOIN Transactions "
                "ON Transactions.TransID = AccountTransactions.TransID "
                "WHERE Account = '%s' %s %s "
                "ORDER BY AccountTransactions.LedgerSeq %s, "
                "AccountTransactions.TxnSeq %s, AccountTransactions.TransID %s "
                "LIMIT %u, %u;") %
            selection % app.accountIDCache().toBase58(account) % maxClause %
            minClause % (descending ? "DESC" : "ASC") %
            (descending ? "DESC" : "ASC") % (descending ? "DESC" : "ASC") %
            beast::lexicalCastThrow<std::string>(offset) %
            beast::lexicalCastThrow<std::string>(numberOfResults));
    JLOG(j.trace()) << "txSQL query: " << sql;
    return sql;
}

int
SQLInterface_sqlite::getAccountTxs(
    SQLDatabase_* db,
    Application& app,
    LedgerMaster& ledgerMaster,
    beast::Journal& j,
    AccountID const& account,
    NetworkOPs::AccountTxs& ret,
    std::int32_t minLedger,
    std::int32_t maxLedger,
    bool descending,
    std::uint32_t offset,
    int limit,
    int limit_used,
    bool bUnlimited)
{
    std::string sql = transactionsSQL(
        app,
        j,
        account,
        "AccountTransactions.LedgerSeq,Status,RawTxn,TxnMeta",
        minLedger,
        maxLedger,
        descending,
        offset,
        limit,
        limit_used,
        false,
        false,
        bUnlimited);
    if (sql == "")
        return 0;

    int total = 0;
    {
        auto cdb = checkoutDb(db);

        boost::optional<std::uint64_t> ledgerSeq;
        boost::optional<std::string> status;
        soci::blob sociTxnBlob(*cdb), sociTxnMetaBlob(*cdb);
        soci::indicator rti, tmi;
        Blob rawTxn, txnMeta;

        soci::statement st =
            (cdb->prepare << sql,
             soci::into(ledgerSeq),
             soci::into(status),
             soci::into(sociTxnBlob, rti),
             soci::into(sociTxnMetaBlob, tmi));

        st.execute();
        while (st.fetch())
        {
            if (soci::i_ok == rti)
                convert(sociTxnBlob, rawTxn);
            else
                rawTxn.clear();

            if (soci::i_ok == tmi)
                convert(sociTxnMetaBlob, txnMeta);
            else
                txnMeta.clear();

            auto txn =
                Transaction::transactionFromSQL(ledgerSeq, status, rawTxn, app);

            if (txnMeta.empty())
            {  // Work around a bug that could leave the metadata missing
                auto const seq =
                    rangeCheckedCast<std::uint32_t>(ledgerSeq.value_or(0));

                JLOG(j.warn())
                    << "Recovering ledger " << seq << ", txn " << txn->getID();

                if (auto l = ledgerMaster.getLedgerBySeq(seq))
                    pendSaveValidated(app, l, false, false);
            }

            if (txn)
            {
                ret.emplace_back(
                    txn,
                    std::make_shared<TxMeta>(
                        txn->getID(), txn->getLedger(), txnMeta));
                total++;
            }
        }

        if (!total && limit_used >= 0)
        {
            std::string sql1 = transactionsSQL(
                app,
                j,
                account,
                "count(*)",
                minLedger,
                maxLedger,
                descending,
                0,
                limit,
                limit_used,
                false,
                false,
                bUnlimited);

            *cdb << sql1, soci::into(total);

            total = ~total;
        }
    }

    return total;
}

NetworkOPs::AccountTxs
SQLInterface_sqlite::getAccountTxs(
    SQLDatabase& db,
    Application& app,
    LedgerMaster& ledgerMaster,
    beast::Journal& j,
    AccountID const& account,
    std::int32_t minLedger,
    std::int32_t maxLedger,
    bool descending,
    std::uint32_t offset,
    int limit,
    bool bUnlimited)
{
    NetworkOPs::AccountTxs ret;

    /* if databases exists, use it */
    if (exists(db))
    {
        getAccountTxs(
            &*db,
            app,
            ledgerMaster,
            j,
            account,
            ret,
            minLedger,
            maxLedger,
            descending,
            offset,
            limit,
            -1,
            bUnlimited);
        return ret;
    }

    /* else use shard databases */
    int limit_used = 0;
    if (descending)
    {
        iterate_back(
            &*db,
            maxLedger >= 0 ? SQLInterface::seqToShardIndex(maxLedger) : -1u,
            [&](SQLDatabase_* sdb, LedgerIndex index) {
                if (minLedger >= 0 &&
                    index < SQLInterface::seqToShardIndex(minLedger))
                    return false;
                int total = getAccountTxs(
                    &*db,
                    app,
                    ledgerMaster,
                    j,
                    account,
                    ret,
                    minLedger,
                    maxLedger,
                    descending,
                    offset,
                    limit,
                    limit_used,
                    bUnlimited);
                if (!total)
                    return false;
                if (total > 0)
                {
                    limit_used += total;
                    offset = 0;
                }
                else
                {
                    total = ~total;
                    if (offset <= total)
                        offset = 0;
                    else
                        offset -= total;
                }
                return true;
            });
    }
    else
    {
        iterate_forward(
            &*db,
            minLedger >= 0 ? SQLInterface::seqToShardIndex(minLedger) : -1u,
            [&](SQLDatabase_* sdb, LedgerIndex index) {
                if (maxLedger >= 0 &&
                    index > SQLInterface::seqToShardIndex(minLedger))
                    return false;
                int total = getAccountTxs(
                    &*db,
                    app,
                    ledgerMaster,
                    j,
                    account,
                    ret,
                    minLedger,
                    maxLedger,
                    descending,
                    offset,
                    limit,
                    limit_used,
                    bUnlimited);
                if (!total)
                    return false;
                if (total > 0)
                {
                    limit_used += total;
                    offset = 0;
                }
                else
                {
                    total = ~total;
                    if (offset <= total)
                        offset = 0;
                    else
                        offset -= total;
                }
                return true;
            });
    }

    return ret;
}

int
SQLInterface_sqlite::getAccountTxsB(
    SQLDatabase_* db,
    Application& app,
    beast::Journal& j,
    AccountID const& account,
    std::vector<NetworkOPs::txnMetaLedgerType>& ret,
    std::int32_t minLedger,
    std::int32_t maxLedger,
    bool descending,
    std::uint32_t offset,
    int limit,
    int limit_used,
    bool bUnlimited)
{
    std::string sql = transactionsSQL(
        app,
        j,
        account,
        "AccountTransactions.LedgerSeq,Status,RawTxn,TxnMeta",
        minLedger,
        maxLedger,
        descending,
        offset,
        limit,
        limit_used,
        true /*binary*/,
        false,
        bUnlimited);
    if (sql == "")
        return 0;

    int total = 0;

    {
        auto cdb = checkoutDb(db);

        boost::optional<std::uint64_t> ledgerSeq;
        boost::optional<std::string> status;
        soci::blob sociTxnBlob(*cdb), sociTxnMetaBlob(*cdb);
        soci::indicator rti, tmi;

        soci::statement st =
            (cdb->prepare << sql,
             soci::into(ledgerSeq),
             soci::into(status),
             soci::into(sociTxnBlob, rti),
             soci::into(sociTxnMetaBlob, tmi));

        st.execute();
        while (st.fetch())
        {
            Blob rawTxn;
            if (soci::i_ok == rti)
                convert(sociTxnBlob, rawTxn);
            Blob txnMeta;
            if (soci::i_ok == tmi)
                convert(sociTxnMetaBlob, txnMeta);

            auto const seq =
                rangeCheckedCast<std::uint32_t>(ledgerSeq.value_or(0));

            ret.emplace_back(std::move(rawTxn), std::move(txnMeta), seq);
            total++;
        }

        if (!total && limit_used >= 0)
        {
            std::string sql1 = transactionsSQL(
                app,
                j,
                account,
                "count(*)",
                minLedger,
                maxLedger,
                descending,
                0,
                limit,
                limit_used,
                true,
                false,
                bUnlimited);

            *cdb << sql1, soci::into(total);

            total = ~total;
        }
    }

    return total;
}

std::vector<NetworkOPs::txnMetaLedgerType>
SQLInterface_sqlite::getAccountTxsB(
    SQLDatabase& db,
    Application& app,
    beast::Journal& j,
    AccountID const& account,
    std::int32_t minLedger,
    std::int32_t maxLedger,
    bool descending,
    std::uint32_t offset,
    int limit,
    bool bUnlimited)
{
    std::vector<NetworkOPs::txnMetaLedgerType> ret;

    /* if databases exists, use it */
    if (exists(db))
    {
        getAccountTxsB(
            &*db,
            app,
            j,
            account,
            ret,
            minLedger,
            maxLedger,
            descending,
            offset,
            limit,
            -1,
            bUnlimited);
        return ret;
    }

    /* else use shard databases */
    int limit_used = 0;
    if (descending)
    {
        iterate_back(
            &*db,
            maxLedger >= 0 ? SQLInterface::seqToShardIndex(maxLedger) : -1u,
            [&](SQLDatabase_* sdb, LedgerIndex index) {
                if (minLedger >= 0 &&
                    index < SQLInterface::seqToShardIndex(minLedger))
                    return false;
                int total = getAccountTxsB(
                    &*db,
                    app,
                    j,
                    account,
                    ret,
                    minLedger,
                    maxLedger,
                    descending,
                    offset,
                    limit,
                    limit_used,
                    bUnlimited);
                if (!total)
                    return false;
                if (total > 0)
                {
                    limit_used += total;
                    offset = 0;
                }
                else
                {
                    total = ~total;
                    if (offset <= total)
                        offset = 0;
                    else
                        offset -= total;
                }
                return true;
            });
    }
    else
    {
        iterate_forward(
            &*db,
            minLedger >= 0 ? SQLInterface::seqToShardIndex(minLedger) : -1u,
            [&](SQLDatabase_* sdb, LedgerIndex index) {
                if (maxLedger >= 0 &&
                    index > SQLInterface::seqToShardIndex(minLedger))
                    return false;
                int total = getAccountTxsB(
                    &*db,
                    app,
                    j,
                    account,
                    ret,
                    minLedger,
                    maxLedger,
                    descending,
                    offset,
                    limit,
                    limit_used,
                    bUnlimited);
                if (!total)
                    return false;
                if (total > 0)
                {
                    limit_used += total;
                    offset = 0;
                }
                else
                {
                    total = ~total;
                    if (offset <= total)
                        offset = 0;
                    else
                        offset -= total;
                }
                return true;
            });
    }

    return ret;
}

LedgerIndex
SQLInterface_sqlite::getCanDelete(SQLDatabase& db)
{
    soci::session& session = getSession(db);
    LedgerIndex seq;
    session << "SELECT CanDeleteSeq FROM CanDelete WHERE Key = 1;",
        soci::into(seq);
    ;
    return seq;
}

LedgerIndex
SQLInterface_sqlite::setCanDelete(SQLDatabase& db, LedgerIndex canDelete)
{
    soci::session& session = getSession(db);
    session << "UPDATE CanDelete SET CanDeleteSeq = :canDelete WHERE Key = 1;",
        soci::use(canDelete);
    return canDelete;
}

SQLInterface::SavedState
SQLInterface_sqlite::getSavedState(SQLDatabase& db)
{
    soci::session& session = getSession(db);
    SavedState state;
    session << "SELECT WritableDb, ArchiveDb, LastRotatedLedger"
               " FROM DbState WHERE Key = 1;",
        soci::into(state.writableDb), soci::into(state.archiveDb),
        soci::into(state.lastRotated);

    return state;
}

void
SQLInterface_sqlite::setSavedState(
    SQLDatabase& db,
    SQLInterface::SavedState const& state)
{
    soci::session& session = getSession(db);
    session << "UPDATE DbState"
               " SET WritableDb = :writableDb,"
               " ArchiveDb = :archiveDb,"
               " LastRotatedLedger = :lastRotated"
               " WHERE Key = 1;",
        soci::use(state.writableDb), soci::use(state.archiveDb),
        soci::use(state.lastRotated);
}

void
SQLInterface_sqlite::setLastRotated(SQLDatabase& db, LedgerIndex seq)
{
    soci::session& session = getSession(db);
    session << "UPDATE DbState SET LastRotatedLedger = :seq"
               " WHERE Key = 1;",
        soci::use(seq);
}

int
SQLInterface_sqlite::accountTxPage(
    SQLDatabase_* db,
    AccountIDCache const& idCache,
    std::function<void(std::uint32_t)> const& onUnsavedLedger,
    std::function<
        void(std::uint32_t, std::string const&, Blob&&, Blob&&)> const&
        onTransaction,
    AccountID const& account,
    std::int32_t minLedger,
    std::int32_t maxLedger,
    bool forward,
    std::optional<NetworkOPs::AccountTxMarker>& marker,
    int limit,
    int limit_used,
    bool bAdmin,
    std::uint32_t page_length)
{
    int total = 0;

    bool lookingForMarker = marker.has_value();

    std::uint32_t numberOfResults;

    if (limit <= 0 || (limit > page_length && !bAdmin))
        numberOfResults = page_length;
    else
        numberOfResults = limit;

    if (numberOfResults < limit_used)
        return -1;
    numberOfResults -= limit_used;

    // As an account can have many thousands of transactions, there is a limit
    // placed on the amount of transactions returned. If the limit is reached
    // before the result set has been exhausted (we always query for one more
    // than the limit), then we return an opaque marker that can be supplied in
    // a subsequent query.
    std::uint32_t queryLimit = numberOfResults + 1;
    std::uint32_t findLedger = 0, findSeq = 0;

    if (lookingForMarker)
    {
        findLedger = marker->ledgerSeq;
        findSeq = marker->txnSeq;
    }

    // marker is also an output parameter, so need to reset
    if (limit_used <= 0)
        marker.reset();

    static std::string const prefix(
        R"(SELECT AccountTransactions.LedgerSeq,AccountTransactions.TxnSeq,
          Status,RawTxn,TxnMeta
          FROM AccountTransactions INNER JOIN Transactions
          ON Transactions.TransID = AccountTransactions.TransID
          AND AccountTransactions.Account = '%s' WHERE
          )");

    std::string sql;

    // SQL's BETWEEN uses a closed interval ([a,b])

    if (forward && (findLedger == 0))
    {
        sql = boost::str(
            boost::format(
                prefix + (R"(AccountTransactions.LedgerSeq BETWEEN '%u' AND '%u'
             ORDER BY AccountTransactions.LedgerSeq ASC,
             AccountTransactions.TxnSeq ASC
             LIMIT %u;)")) %
            idCache.toBase58(account) % minLedger % maxLedger % queryLimit);
    }
    else if (forward && (findLedger != 0))
    {
        auto b58acct = idCache.toBase58(account);
        sql = boost::str(
            boost::format((
                R"(SELECT AccountTransactions.LedgerSeq,AccountTransactions.TxnSeq,
            Status,RawTxn,TxnMeta
            FROM AccountTransactions, Transactions WHERE
            (AccountTransactions.TransID = Transactions.TransID AND
            AccountTransactions.Account = '%s' AND
            AccountTransactions.LedgerSeq BETWEEN '%u' AND '%u')
            OR
            (AccountTransactions.TransID = Transactions.TransID AND
            AccountTransactions.Account = '%s' AND
            AccountTransactions.LedgerSeq = '%u' AND
            AccountTransactions.TxnSeq >= '%u')
            ORDER BY AccountTransactions.LedgerSeq ASC,
            AccountTransactions.TxnSeq ASC
            LIMIT %u;
            )")) %
            b58acct % (findLedger + 1) % maxLedger % b58acct % findLedger %
            findSeq % queryLimit);
    }
    else if (!forward && (findLedger == 0))
    {
        sql = boost::str(
            boost::format(
                prefix + (R"(AccountTransactions.LedgerSeq BETWEEN '%u' AND '%u'
             ORDER BY AccountTransactions.LedgerSeq DESC,
             AccountTransactions.TxnSeq DESC
             LIMIT %u;)")) %
            idCache.toBase58(account) % minLedger % maxLedger % queryLimit);
    }
    else if (!forward && (findLedger != 0))
    {
        auto b58acct = idCache.toBase58(account);
        sql = boost::str(
            boost::format((
                R"(SELECT AccountTransactions.LedgerSeq,AccountTransactions.TxnSeq,
            Status,RawTxn,TxnMeta
            FROM AccountTransactions, Transactions WHERE
            (AccountTransactions.TransID = Transactions.TransID AND
            AccountTransactions.Account = '%s' AND
            AccountTransactions.LedgerSeq BETWEEN '%u' AND '%u')
            OR
            (AccountTransactions.TransID = Transactions.TransID AND
            AccountTransactions.Account = '%s' AND
            AccountTransactions.LedgerSeq = '%u' AND
            AccountTransactions.TxnSeq <= '%u')
            ORDER BY AccountTransactions.LedgerSeq DESC,
            AccountTransactions.TxnSeq DESC
            LIMIT %u;
            )")) %
            b58acct % minLedger % (findLedger - 1) % b58acct % findLedger %
            findSeq % queryLimit);
    }
    else
    {
        assert(false);
        // sql is empty
        return total;
    }

    {
        auto cdb(checkoutDb(db));

        Blob rawData;
        Blob rawMeta;

        boost::optional<std::uint64_t> ledgerSeq;
        boost::optional<std::uint32_t> txnSeq;
        boost::optional<std::string> status;
        soci::blob txnData(*cdb);
        soci::blob txnMeta(*cdb);
        soci::indicator dataPresent, metaPresent;

        soci::statement st =
            (cdb->prepare << sql,
             soci::into(ledgerSeq),
             soci::into(txnSeq),
             soci::into(status),
             soci::into(txnData, dataPresent),
             soci::into(txnMeta, metaPresent));

        st.execute();

        while (st.fetch())
        {
            if (lookingForMarker)
            {
                if (findLedger == ledgerSeq.value_or(0) &&
                    findSeq == txnSeq.value_or(0))
                {
                    lookingForMarker = false;
                }
            }
            else if (numberOfResults == 0)
            {
                marker = {
                    rangeCheckedCast<std::uint32_t>(ledgerSeq.value_or(0)),
                    txnSeq.value_or(0)};
                break;
            }

            if (!lookingForMarker)
            {
                if (dataPresent == soci::i_ok)
                    convert(txnData, rawData);
                else
                    rawData.clear();

                if (metaPresent == soci::i_ok)
                    convert(txnMeta, rawMeta);
                else
                    rawMeta.clear();

                // Work around a bug that could leave the metadata missing
                if (rawMeta.size() == 0)
                    onUnsavedLedger(ledgerSeq.value_or(0));

                // `rawData` and `rawMeta` will be used after they are moved.
                // That's OK.
                onTransaction(
                    rangeCheckedCast<std::uint32_t>(ledgerSeq.value_or(0)),
                    *status,
                    std::move(rawData),
                    std::move(rawMeta));
                // Note some callbacks will move the data, some will not. Clear
                // them so code doesn't depend on if the data was actually moved
                // or not. The code will be more efficient if `rawData` and
                // `rawMeta` don't have to allocate in `convert`, so don't
                // refactor my moving these variables into loop scope.
                rawData.clear();
                rawMeta.clear();

                --numberOfResults;
                total++;
            }
        }
    }

    return total;
}

void
SQLInterface_sqlite::accountTxPage(
    SQLDatabase& db,
    AccountIDCache const& idCache,
    std::function<void(std::uint32_t)> const& onUnsavedLedger,
    std::function<
        void(std::uint32_t, std::string const&, Blob&&, Blob&&)> const&
        onTransaction,
    AccountID const& account,
    std::int32_t minLedger,
    std::int32_t maxLedger,
    bool forward,
    std::optional<NetworkOPs::AccountTxMarker>& marker,
    int limit,
    bool bAdmin,
    std::uint32_t page_length)
{
    /* if databases exists, use it */
    if (exists(db))
    {
        accountTxPage(
            &*db,
            idCache,
            onUnsavedLedger,
            onTransaction,
            account,
            minLedger,
            maxLedger,
            forward,
            marker,
            limit,
            0,
            bAdmin,
            page_length);
        return;
    }

    /* else use shard databases */
    int limit_used = 0;
    if (!forward)
    {
        iterate_forward(
            &*db,
            minLedger >= 0 ? SQLInterface::seqToShardIndex(minLedger) : -1u,
            [&](SQLDatabase_* sdb, LedgerIndex index) {
                if (maxLedger >= 0 &&
                    index > SQLInterface::seqToShardIndex(minLedger))
                    return false;
                int total = accountTxPage(
                    &*db,
                    idCache,
                    onUnsavedLedger,
                    onTransaction,
                    account,
                    minLedger,
                    maxLedger,
                    forward,
                    marker,
                    limit,
                    limit_used,
                    bAdmin,
                    page_length);
                if (total < 0)
                    return false;
                limit_used += total;
                return true;
            });
    }
    else
    {
        iterate_back(
            &*db,
            maxLedger >= 0 ? SQLInterface::seqToShardIndex(maxLedger) : -1u,
            [&](SQLDatabase_* sdb, LedgerIndex index) {
                if (minLedger >= 0 &&
                    index < SQLInterface::seqToShardIndex(minLedger))
                    return false;
                int total = accountTxPage(
                    &*db,
                    idCache,
                    onUnsavedLedger,
                    onTransaction,
                    account,
                    minLedger,
                    maxLedger,
                    forward,
                    marker,
                    limit,
                    limit_used,
                    bAdmin,
                    page_length);
                if (total < 0)
                    return false;
                limit_used += total;
                return true;
            });
    }

    return;
}

void
SQLInterface_sqlite::loadManifest(
    SQLDatabase& dbCon,
    std::string const& dbTable,
    beast::Journal& j,
    ManifestCache& mCache)
{
    // Load manifests stored in database
    std::string const sql = "SELECT RawData FROM " + dbTable + ";";
    auto db = checkoutDb(dbCon);
    soci::blob sociRawData(*db);
    soci::statement st = (db->prepare << sql, soci::into(sociRawData));
    st.execute();
    while (st.fetch())
    {
        std::string serialized;
        convert(sociRawData, serialized);
        if (auto mo = deserializeManifest(serialized))
        {
            if (!mo->verify())
            {
                JLOG(j.warn()) << "Unverifiable manifest in db";
                continue;
            }

            mCache.applyManifest(std::move(*mo));
        }
        else
        {
            JLOG(j.warn()) << "Malformed manifest in database";
        }
    }
}

void
SQLInterface_sqlite::saveManifest(
    SQLDatabase& dbCon,
    std::string const& dbTable,
    std::function<bool(PublicKey const&)> isTrusted,
    beast::Journal& j,
    hash_map<PublicKey, Manifest>& map)
{
    auto db = checkoutDb(dbCon);

    soci::transaction tr(*db);
    *db << "DELETE FROM " << dbTable;
    std::string const sql =
        "INSERT INTO " + dbTable + " (RawData) VALUES (:rawData);";
    for (auto const& v : map)
    {
        // Save all revocation manifests,
        // but only save trusted non-revocation manifests.
        if (!v.second.revoked() && !isTrusted(v.second.masterKey))
        {
            JLOG(j.info()) << "Untrusted manifest in cache not saved to db";
            continue;
        }

        // soci does not support bulk insertion of blob data
        // Do not reuse blob because manifest ecdsa signatures vary in length
        // but blob write length is expected to be >= the last write
        soci::blob rawData(*db);
        convert(v.second.serialized, rawData);
        *db << sql, soci::use(rawData);
    }
    tr.commit();
}

boost::variant<Transaction::pointer, bool>
SQLInterface_sqlite::loadTransaction(
    SQLDatabase_* db,
    Application& app,
    uint256 const& id,
    boost::optional<ClosedInterval<uint32_t>> const& range,
    error_code_i& ec)
{
    std::string sql =
        "SELECT LedgerSeq,Status,RawTxn "
        "FROM Transactions WHERE TransID='";

    sql.append(to_string(id));
    sql.append("';");

    boost::optional<std::uint64_t> ledgerSeq;
    boost::optional<std::string> status;
    Blob rawTxn;
    {
        auto cdb = checkoutDb(db);
        soci::blob sociRawTxnBlob(*cdb);
        soci::indicator rti;

        *cdb << sql, soci::into(ledgerSeq), soci::into(status),
            soci::into(sociRawTxnBlob, rti);

        auto const got_data = cdb->got_data();

        if ((!got_data || rti != soci::i_ok) && !range)
            return nullptr;

        if (!got_data)
        {
            uint64_t count = 0;

            *cdb << "SELECT COUNT(DISTINCT LedgerSeq) FROM Transactions WHERE "
                    "LedgerSeq BETWEEN "
                 << range->first() << " AND " << range->last() << ";",
                soci::into(count, rti);

            if (!cdb->got_data() || rti != soci::i_ok)
                return false;

            return count == (range->last() - range->first() + 1);
        }

        convert(sociRawTxnBlob, rawTxn);
    }

    try
    {
        return Transaction::transactionFromSQL(ledgerSeq, status, rawTxn, app);
    }
    catch (std::exception& e)
    {
        JLOG(app.journal("Ledger").warn())
            << "Unable to deserialize transaction from raw SQL value. Error: "
            << e.what();

        ec = rpcDB_DESERIALIZATION;
    }

    return nullptr;
}

boost::variant<Transaction::pointer, bool>
SQLInterface_sqlite::loadTransaction(
    SQLDatabase& db,
    Application& app,
    uint256 const& id,
    boost::optional<ClosedInterval<uint32_t>> const& range,
    error_code_i& ec)
{
    /* if databases exists, use it */
    if (exists(db))
        return loadTransaction(&*db, app, id, range, ec);

    /* else use shard databases */
    boost::variant<Transaction::pointer, bool> res(false);
    iterate_back(&*db, -1u, [&](SQLDatabase_* sdb, LedgerIndex index) {
        boost::optional<ClosedInterval<uint32_t>> range1;
        if (range)
        {
            uint32_t low =
                std::max(range->lower(), SQLInterface::firstLedgerSeq(index));
            uint32_t high =
                std::min(range->upper(), SQLInterface::lastLedgerSeq(index));
            if (low <= high)
                range1 = ClosedInterval<uint32_t>(low, high);
        }
        res = sdb->getInterface()->loadTransaction(sdb, app, id, range1, ec);
        /* finish iterations if transaction found or error detected */
        return res.which() == 1 && boost::get<bool>(res);
    });

    return res;
}

bool
SQLInterface_sqlite::checkDBSpace(
    SQLDatabase& txDb,
    Config const& config,
    beast::Journal& j)
{
    boost::filesystem::space_info space =
        boost::filesystem::space(config.legacy("database_path"));

    if (space.available < megabytes(512))
    {
        JLOG(j.fatal()) << "Remaining free disk space is less than 512MB";
        return false;
    }

    DatabaseCon::Setup dbSetup = setup_DatabaseCon(config);
    boost::filesystem::path dbPath = dbSetup.dataDir / TxDBName;
    boost::system::error_code ec;
    boost::optional<std::uint64_t> dbSize =
        boost::filesystem::file_size(dbPath, ec);
    if (ec)
    {
        JLOG(j.error()) << "Error checking transaction db file size: "
                        << ec.message();
        dbSize.reset();
    }

    auto db = checkoutDb(txDb);
    static auto const pageSize = [&] {
        std::uint32_t ps;
        *db << "PRAGMA page_size;", soci::into(ps);
        return ps;
    }();
    static auto const maxPages = [&] {
        std::uint32_t mp;
        *db << "PRAGMA max_page_count;", soci::into(mp);
        return mp;
    }();
    std::uint32_t pageCount;
    *db << "PRAGMA page_count;", soci::into(pageCount);
    std::uint32_t freePages = maxPages - pageCount;
    std::uint64_t freeSpace = safe_cast<std::uint64_t>(freePages) * pageSize;
    JLOG(j.info())
        << "Transaction DB pathname: " << dbPath.string()
        << "; file size: " << dbSize.value_or(-1) << " bytes"
        << "; SQLite page size: " << pageSize << " bytes"
        << "; Free pages: " << freePages << "; Free space: " << freeSpace
        << " bytes; "
        << "Note that this does not take into account available disk "
           "space.";

    if (freeSpace < megabytes(512))
    {
        JLOG(j.fatal())
            << "Free SQLite space for transaction db is less than "
               "512MB. To fix this, rippled must be executed with the "
               "\"--vacuum\" parameter before restarting. "
               "Note that this activity can take multiple days, "
               "depending on database size.";
        return false;
    }

    return true;
}

std::pair<PublicKey, SecretKey>
SQLInterface_sqlite::loadNodeIdentity(SQLDatabase& db)
{
    // Try to load a node identity from the database:
    boost::optional<PublicKey> publicKey;
    boost::optional<SecretKey> secretKey;

    auto cdb = checkoutDb(db);

    {
        boost::optional<std::string> pubKO, priKO;
        soci::statement st =
            (cdb->prepare << "SELECT PublicKey, PrivateKey FROM NodeIdentity;",
             soci::into(pubKO),
             soci::into(priKO));
        st.execute();
        while (st.fetch())
        {
            auto const sk = parseBase58<SecretKey>(
                TokenType::NodePrivate, priKO.value_or(""));
            auto const pk = parseBase58<PublicKey>(
                TokenType::NodePublic, pubKO.value_or(""));

            // Only use if the public and secret keys are a pair
            if (sk && pk && (*pk == derivePublicKey(KeyType::secp256k1, *sk)))
            {
                secretKey = sk;
                publicKey = pk;
            }
        }
    }

    // If a valid identity wasn't found, we randomly generate a new one:
    if (!publicKey || !secretKey)
    {
        std::tie(publicKey, secretKey) = randomKeyPair(KeyType::secp256k1);

        *cdb << str(
            boost::format("INSERT INTO NodeIdentity (PublicKey,PrivateKey) "
                          "VALUES ('%s','%s');") %
            toBase58(TokenType::NodePublic, *publicKey) %
            toBase58(TokenType::NodePrivate, *secretKey));
    }

    return {*publicKey, *secretKey};
}

void
SQLInterface_sqlite::databaseBodyDoPut(
    SQLDatabase& conn,
    std::string& data,
    std::string& path,
    std::uint64_t fileSize,
    std::uint64_t& part,
    std::uint16_t const maxRowSizePad)
{
    std::uint64_t rowSize = 0;
    soci::indicator rti;

    std::uint64_t remainingInRow = 0;

    auto db = checkoutDb(conn);

    auto be = dynamic_cast<soci::sqlite3_session_backend*>(db->get_backend());
    BOOST_ASSERT(be);

    // This limits how large we can make the blob
    // in each row. Also subtract a pad value to
    // account for the other values in the row.
    auto const blobMaxSize =
        sqlite_api::sqlite3_limit(be->conn_, SQLITE_LIMIT_LENGTH, -1) -
        maxRowSizePad;

    auto rowInit = [&] {
        *db << "INSERT INTO Download VALUES (:path, zeroblob(0), 0, :part)",
            soci::use(path), soci::use(part);

        remainingInRow = blobMaxSize;
        rowSize = 0;
    };

    *db << "SELECT Path,Size,Part FROM Download ORDER BY Part DESC "
           "LIMIT 1",
        soci::into(path), soci::into(rowSize), soci::into(part, rti);

    if (!db->got_data())
        rowInit();
    else
        remainingInRow = blobMaxSize - rowSize;

    auto insert = [&db, &rowSize, &part, &fs = fileSize](auto const& data) {
        std::uint64_t updatedSize = rowSize + data.size();

        *db << "UPDATE Download SET Data = CAST(Data || :data AS blob), "
               "Size = :size WHERE Part = :part;",
            soci::use(data), soci::use(updatedSize), soci::use(part);

        fs += data.size();
    };

    while (remainingInRow < data.size())
    {
        if (remainingInRow)
        {
            insert(data.substr(0, remainingInRow));
            data.erase(0, remainingInRow);
        }

        ++part;
        rowInit();
    }

    insert(data);
}

void
SQLInterface_sqlite::databaseBodyFinish(SQLDatabase& conn, std::ofstream& fout)
{
    auto db = checkoutDb(conn);

    soci::rowset<std::string> rs =
        (db->prepare << "SELECT Data FROM Download ORDER BY PART ASC;");

    // iteration through the resultset:
    for (auto it = rs.begin(); it != rs.end(); ++it)
        fout.write(it->data(), it->size());
}

void
SQLInterface_sqlite::addValidatorManifest(
    SQLDatabase& db,
    std::string const& serialized)
{
    auto cdb = checkoutDb(db);

    soci::transaction tr(*cdb);
    static const char* const sql =
        "INSERT INTO ValidatorManifests (RawData) VALUES "
        "(:rawData);";
    soci::blob rawData(*cdb);
    convert(serialized, rawData);
    *cdb << sql, soci::use(rawData);
    tr.commit();
}

void
SQLInterface_sqlite::loadPeerReservationTable(
    SQLDatabase& conn,
    beast::Journal& j,
    std::unordered_set<PeerReservation, beast::uhash<>, KeyEqual>& table)
{
    auto db = checkoutDb(conn);

    boost::optional<std::string> valPubKey, valDesc;
    // We should really abstract the table and column names into constants,
    // but no one else does. Because it is too tedious? It would be easy if we
    // had a jOOQ for C++.
    soci::statement st =
        (db->prepare << "SELECT PublicKey, Description FROM PeerReservations;",
         soci::into(valPubKey),
         soci::into(valDesc));
    st.execute();
    while (st.fetch())
    {
        if (!valPubKey || !valDesc)
        {
            // This represents a `NULL` in a `NOT NULL` column. It should be
            // unreachable.
            continue;
        }
        auto const optNodeId =
            parseBase58<PublicKey>(TokenType::NodePublic, *valPubKey);
        if (!optNodeId)
        {
            JLOG(j.warn()) << "load: not a public key: " << valPubKey;
            continue;
        }
        table.insert(PeerReservation{*optNodeId, *valDesc});
    }
}

void
SQLInterface_sqlite::insertPeerReservation(
    SQLDatabase& conn,
    PublicKey const& nodeId,
    std::string const& description)
{
    auto db = checkoutDb(conn);
    *db << "INSERT INTO PeerReservations (PublicKey, Description) "
           "VALUES (:nodeId, :desc) "
           "ON CONFLICT (PublicKey) DO UPDATE SET "
           "Description=excluded.Description",
        soci::use(toBase58(TokenType::NodePublic, nodeId)),
        soci::use(description);
}

void
SQLInterface_sqlite::deletePeerReservation(
    SQLDatabase& conn,
    PublicKey const& nodeId)
{
    auto db = checkoutDb(conn);
    *db << "DELETE FROM PeerReservations WHERE PublicKey = :nodeId",
        soci::use(toBase58(TokenType::NodePublic, nodeId));
}

void
SQLInterface_sqlite::readArchiveDB(
    SQLDatabase& db,
    std::function<void(std::string const&, int)> const& func)
{
    auto& session{getSession(db)};

    soci::rowset<soci::row> rs = (session.prepare << "SELECT * FROM State;");

    for (auto it = rs.begin(); it != rs.end(); ++it)
    {
        func(it->get<std::string>(1), it->get<int>(0));
    }
}

void
SQLInterface_sqlite::insertArchiveDB(
    SQLDatabase& db,
    LedgerIndex shardIndex,
    std::string const& url)
{
    auto& session{getSession(db)};

    session << "INSERT INTO State VALUES (:index, :url);",
        soci::use(shardIndex), soci::use(url);
}

void
SQLInterface_sqlite::deleteFromArchiveDB(
    SQLDatabase& db,
    LedgerIndex shardIndex)
{
    auto& session{getSession(db)};

    session << "DELETE FROM State WHERE ShardIndex = :index;",
        soci::use(shardIndex);
}

void
SQLInterface_sqlite::dropArchiveDB(SQLDatabase& db)
{
    auto& session{getSession(db)};

    session << "DROP TABLE State;";
}

int
SQLInterface_sqlite::getKBUsedAll(SQLDatabase& db)
{
    return ripple::getKBUsedAll(getSession(db));
}

int
SQLInterface_sqlite::getKBUsedDB(SQLDatabase& db)
{
    return ripple::getKBUsedDB(getSession(db));
}

void
SQLInterface_sqlite::readPeerFinderDB(
    SQLDatabase& db,
    std::function<void(std::string const&, int)> const& func)
{
    soci::session& m_session = getSession(db);
    std::string s;
    int valence;
    soci::statement st =
        (m_session.prepare << "SELECT "
                              " address, "
                              " valence "
                              "FROM PeerFinder_BootstrapCache;",
         soci::into(s),
         soci::into(valence));

    st.execute();
    while (st.fetch())
    {
        func(s, valence);
    }
}

void
SQLInterface_sqlite::savePeerFinderDB(
    SQLDatabase& db,
    std::vector<PeerFinder::Store::Entry> const& v)
{
    soci::session& m_session = getSession(db);
    soci::transaction tr(m_session);
    m_session << "DELETE FROM PeerFinder_BootstrapCache;";

    if (!v.empty())
    {
        std::vector<std::string> s;
        std::vector<int> valence;
        s.reserve(v.size());
        valence.reserve(v.size());

        for (auto const& e : v)
        {
            s.emplace_back(to_string(e.endpoint));
            valence.emplace_back(e.valence);
        }

        m_session << "INSERT INTO PeerFinder_BootstrapCache ( "
                     "  address, "
                     "  valence "
                     ") VALUES ( "
                     "  :s, :valence "
                     ");",
            soci::use(s), soci::use(valence);
    }

    tr.commit();
}

SQLInterface_sqlite SQLInterface_sqlite_;

SQLInterface* SQLInterfaceSqlite =
    static_cast<SQLInterface_sqlite*>(&SQLInterface_sqlite_);

}  // namespace ripple
