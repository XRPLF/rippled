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

#ifndef RIPPLE_CORE_SQLINTERFACE_H_INCLUDED
#define RIPPLE_CORE_SQLINTERFACE_H_INCLUDED

#include <ripple/app/ledger/Ledger.h>
#include <ripple/app/main/Application.h>
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/app/misc/Transaction.h>
#include <ripple/core/Config.h>
#include <ripple/peerfinder/impl/Store.h>
#include <boost/filesystem.hpp>
#include <boost/optional.hpp>
#include <boost/variant.hpp>

namespace ripple {

class SQLDatabase_;

class SQLDatabase : public std::unique_ptr<SQLDatabase_>
{
};

class SQLInterface
{
public:
    enum DatabaseType : uint8_t {
        LEDGER,
        TRANSACTION,
        WALLET,
        LEDGER_SHARD,
        TRANSACTION_SHARD,
        ACQUIRE_SHARD,
        ARCHIVE,
        STATE,
        DOWNLOAD,
        PEER_FINDER,
        VACUUM
    };

    enum TableType { LEDGERS, TRANSACTIONS, ACCOUNT_TRANSACTIONS };

    struct SQLLedgerInfo
    {
        boost::optional<std::string> sLedgerHash, sPrevHash, sAccountHash,
            sTransHash;
        boost::optional<std::uint64_t> totDrops, closingTime, prevClosingTime,
            closeResolution, closeFlags, ledgerSeq64;
    };

    struct SavedState
    {
        std::string writableDb;
        std::string archiveDb;
        LedgerIndex lastRotated;
    };

    typedef std::pair<LedgerIndex, uint8_t> DatabaseIndex;

    struct hash_pair
    {
        template <class T1, class T2>
        size_t
        operator()(const std::pair<T1, T2>& p) const
        {
            auto hash1 = std::hash<T1>{}(p.first);
            auto hash2 = std::hash<T2>{}(p.second);
            return hash1 ^ hash2;
        }
    };

    static SQLInterface*
    getInterface(DatabaseType type);

    static std::string
    tableName(TableType type)
    {
        switch (type)
        {
            case LEDGERS:
                return "Ledgers";
            case TRANSACTIONS:
                return "Transactions";
            case ACCOUNT_TRANSACTIONS:
                return "AccountTransactions";
        }
        return "Unknown";
    }

    static bool
    init(Config const& config);

    static void
    addDatabase(
        SQLDatabase_* db,
        DatabaseType type,
        LedgerIndex shardIndex = 0);

    static void
    removeDatabase(SQLDatabase_* db);

    static SQLDatabase_*
    findShardDatabase(SQLDatabase_* db, LedgerIndex ledgerIndex);

    static bool
    iterate_forward(
        SQLDatabase_* db,
        LedgerIndex firstIndex,
        std::function<bool(SQLDatabase_* db, LedgerIndex index)> const&
            onShardDB);

    static bool
    iterate_back(
        SQLDatabase_* db,
        LedgerIndex lastIndex,
        std::function<bool(SQLDatabase_* db, LedgerIndex index)> const&
            onShardDB);

    virtual ~SQLInterface()
    {
    }

    virtual std::string
    getDBName(DatabaseType type) = 0;

    virtual std::tuple<bool, SQLDatabase, SQLDatabase>
    makeLedgerDBs(
        Application& app,
        Config const& config,
        beast::Journal const& j,
        bool setupFromConfig,
        LedgerIndex shardIndex,
        bool backendComplete,
        boost::filesystem::path const& dir) = 0;

    virtual SQLDatabase
    makeAcquireDB(
        Application& app,
        Config const& config,
        boost::filesystem::path const& dir) = 0;

    virtual SQLDatabase
    makeWalletDB(
        bool setupFromConfig,
        Config const& config,
        beast::Journal const& j,
        std::string const& dbname,
        boost::filesystem::path const& dir) = 0;

    virtual SQLDatabase
    makeArchiveDB(
        boost::filesystem::path const& dir,
        std::string const& dbName) = 0;

    virtual void
    initStateDB(
        SQLDatabase& db,
        BasicConfig const& config,
        std::string const& dbName) = 0;

    virtual std::pair<SQLDatabase, boost::optional<std::uint64_t>>
    openDatabaseBodyDb(
        Config const& config,
        boost::filesystem::path const& path) = 0;

    virtual bool
    makeVacuumDB(Config const& config) = 0;

    virtual void
    initPeerFinderDB(
        SQLDatabase& db,
        BasicConfig const& config,
        beast::Journal const j) = 0;

    virtual void
    updatePeerFinderDB(
        SQLDatabase& db,
        int currentSchemaVersion,
        beast::Journal const j) = 0;

    virtual boost::optional<LedgerIndex>
    getMinLedgerSeq(SQLDatabase& db, TableType type) = 0;

    virtual boost::optional<LedgerIndex>
    getMinLedgerSeq(SQLDatabase_* db, TableType type) = 0;

    virtual boost::optional<LedgerIndex>
    getMaxLedgerSeq(SQLDatabase& db, TableType type) = 0;

    virtual boost::optional<LedgerIndex>
    getMaxLedgerSeq(SQLDatabase_* db, TableType type) = 0;

    virtual void
    deleteByLedgerSeq(
        SQLDatabase& db,
        TableType type,
        LedgerIndex ledgerSeq) = 0;

    virtual void
    deleteByLedgerSeq(
        SQLDatabase_* db,
        TableType type,
        LedgerIndex ledgerSeq) = 0;

    virtual void
    deleteBeforeLedgerSeq(
        SQLDatabase& db,
        TableType type,
        LedgerIndex ledgerSeq) = 0;

    virtual void
    deleteBeforeLedgerSeq(
        SQLDatabase_* db,
        TableType type,
        LedgerIndex ledgerSeq) = 0;

    virtual int
    getRows(SQLDatabase& db, TableType type) = 0;

    virtual int
    getRows(SQLDatabase_* db, TableType type) = 0;

    virtual std::tuple<int, int, int>
    getRowsMinMax(SQLDatabase& db, TableType type) = 0;

    virtual std::tuple<int, int, int>
    getRowsMinMax(SQLDatabase_* db, TableType type) = 0;

    virtual void
    insertAcquireDBIndex(SQLDatabase& db, std::uint32_t index_) = 0;

    virtual std::pair<bool, boost::optional<std::string>>
    selectAcquireDBLedgerSeqs(SQLDatabase& db, std::uint32_t index) = 0;

    virtual std::
        tuple<bool, boost::optional<std::string>, boost::optional<std::string>>
        selectAcquireDBLedgerSeqsHash(SQLDatabase& db, std::uint32_t index) = 0;

    virtual bool
    updateLedgerDBs(
        SQLDatabase& txdb,
        SQLDatabase& lgrdb,
        std::shared_ptr<Ledger const> const& ledger,
        std::uint32_t const index,
        beast::Journal const j,
        std::atomic<bool>& stop) = 0;

    virtual void
    updateAcquireDB(
        SQLDatabase& db,
        std::shared_ptr<Ledger const> const& ledger,
        std::uint32_t const index,
        std::uint32_t const lastSeq,
        boost::optional<std::string> seqs) = 0;

    virtual bool
    saveValidatedLedger(
        SQLDatabase& ldgDB,
        SQLDatabase& txnDB,
        Application& app,
        std::shared_ptr<Ledger const> const& ledger,
        bool current) = 0;

    virtual bool
    saveValidatedLedger(
        SQLDatabase_* ldgDB,
        SQLDatabase_* txnDB,
        Application& app,
        std::shared_ptr<Ledger const> const& ledger,
        bool current) = 0;

    virtual bool
    loadLedgerInfoByIndex(
        SQLDatabase& db,
        SQLInterface::SQLLedgerInfo& info,
        beast::Journal const& j,
        LedgerIndex ledgerSeq) = 0;

    virtual bool
    loadLedgerInfoByIndex(
        SQLDatabase_* db,
        SQLInterface::SQLLedgerInfo& info,
        beast::Journal const& j,
        LedgerIndex ledgerSeq) = 0;

    virtual bool
    loadLedgerInfoByIndexSorted(
        SQLDatabase& db,
        SQLInterface::SQLLedgerInfo& info,
        beast::Journal const& j,
        bool ascendSort) = 0;

    virtual bool
    loadLedgerInfoByIndexSorted(
        SQLDatabase_* db,
        SQLInterface::SQLLedgerInfo& info,
        beast::Journal const& j,
        bool ascendSort) = 0;

    virtual bool
    loadLedgerInfoByIndexLimitedSorted(
        SQLDatabase& db,
        SQLInterface::SQLLedgerInfo& info,
        beast::Journal const& j,
        LedgerIndex ledgerFirstIndex,
        bool ascendSort) = 0;

    virtual bool
    loadLedgerInfoByIndexLimitedSorted(
        SQLDatabase_* db,
        SQLInterface::SQLLedgerInfo& info,
        beast::Journal const& j,
        LedgerIndex ledgerFirstIndex,
        bool ascendSort) = 0;

    virtual bool
    loadLedgerInfoByHash(
        SQLDatabase& db,
        SQLInterface::SQLLedgerInfo& info,
        beast::Journal const& j,
        uint256 const& ledgerHash) = 0;

    virtual bool
    loadLedgerInfoByHash(
        SQLDatabase_* db,
        SQLInterface::SQLLedgerInfo& info,
        beast::Journal const& j,
        uint256 const& ledgerHash) = 0;

    virtual uint256
    getHashByIndex(SQLDatabase& db, LedgerIndex ledgerIndex) = 0;

    virtual uint256
    getHashByIndex(SQLDatabase_* db, LedgerIndex ledgerIndex) = 0;

    virtual bool
    getHashesByIndex(
        SQLDatabase& db,
        beast::Journal const& j,
        LedgerIndex ledgerIndex,
        uint256& ledgerHash,
        uint256& parentHash) = 0;

    virtual bool
    getHashesByIndex(
        SQLDatabase_* db,
        beast::Journal const& j,
        LedgerIndex ledgerIndex,
        uint256& ledgerHash,
        uint256& parentHash) = 0;

    virtual std::map<LedgerIndex, std::pair<uint256, uint256>>
    getHashesByIndex(
        SQLDatabase& db,
        beast::Journal const& j,
        LedgerIndex minSeq,
        LedgerIndex maxSeq) = 0;

    virtual void
    getHashesByIndex(
        SQLDatabase_* db,
        beast::Journal const& j,
        LedgerIndex minSeq,
        LedgerIndex maxSeq,
        std::map<LedgerIndex, std::pair<uint256, uint256>>& map) = 0;

    virtual Json::Value
    loadTxHistory(
        SQLDatabase& db,
        Application& app,
        LedgerIndex startIndex) = 0;

    virtual int
    loadTxHistory(
        SQLDatabase_* db,
        Application& app,
        Json::Value& txs,
        LedgerIndex startIndex,
        int quantity,
        bool count) = 0;

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
        bool bUnlimited) = 0;

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
        bool bUnlimited) = 0;

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
        bool bUnlimited) = 0;

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
        bool bUnlimited) = 0;

    virtual LedgerIndex
    getCanDelete(SQLDatabase& db) = 0;

    virtual LedgerIndex
    setCanDelete(SQLDatabase& db, LedgerIndex canDelete) = 0;

    virtual SavedState
    getSavedState(SQLDatabase& db) = 0;

    virtual void
    setSavedState(SQLDatabase& db, SQLInterface::SavedState const& state) = 0;

    virtual void
    setLastRotated(SQLDatabase& db, LedgerIndex seq) = 0;

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
        std::uint32_t page_length) = 0;

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
        int used_limit,
        bool bAdmin,
        std::uint32_t page_length) = 0;

    virtual void
    loadManifest(
        SQLDatabase& dbCon,
        std::string const& dbTable,
        beast::Journal& j,
        ManifestCache& mCache) = 0;

    virtual void
    saveManifest(
        SQLDatabase& dbCon,
        std::string const& dbTable,
        std::function<bool(PublicKey const&)> isTrusted,
        beast::Journal& j,
        hash_map<PublicKey, Manifest>& map) = 0;

    virtual boost::variant<Transaction::pointer, bool>
    loadTransaction(
        SQLDatabase& db,
        Application& app,
        uint256 const& id,
        boost::optional<ClosedInterval<uint32_t>> const& range,
        error_code_i& ec) = 0;

    virtual boost::variant<Transaction::pointer, bool>
    loadTransaction(
        SQLDatabase_* db,
        Application& app,
        uint256 const& id,
        boost::optional<ClosedInterval<uint32_t>> const& range,
        error_code_i& ec) = 0;

    virtual bool
    checkDBSpace(
        SQLDatabase& txDb,
        Config const& config,
        beast::Journal& j) = 0;

    virtual std::pair<PublicKey, SecretKey>
    loadNodeIdentity(SQLDatabase& db) = 0;

    virtual void
    databaseBodyDoPut(
        SQLDatabase& conn,
        std::string& data,
        std::string& path,
        std::uint64_t fileSize,
        std::uint64_t& part,
        std::uint16_t const maxRowSizePad) = 0;

    virtual void
    databaseBodyFinish(SQLDatabase& conn, std::ofstream& fout) = 0;

    virtual void
    addValidatorManifest(SQLDatabase& db, std::string const& serialized) = 0;

    virtual void
    loadPeerReservationTable(
        SQLDatabase& conn,
        beast::Journal& j,
        std::unordered_set<PeerReservation, beast::uhash<>, KeyEqual>&
            table) = 0;

    virtual void
    insertPeerReservation(
        SQLDatabase& conn,
        PublicKey const& nodeId,
        std::string const& description) = 0;

    virtual void
    deletePeerReservation(SQLDatabase& conn, PublicKey const& nodeId) = 0;

    virtual void
    readArchiveDB(
        SQLDatabase& db,
        std::function<void(std::string const&, int)> const& func) = 0;

    virtual void
    insertArchiveDB(
        SQLDatabase& db,
        LedgerIndex shardIndex,
        std::string const& url) = 0;

    virtual void
    deleteFromArchiveDB(SQLDatabase& db, LedgerIndex shardIndex) = 0;

    virtual void
    dropArchiveDB(SQLDatabase& db) = 0;

    virtual int
    getKBUsedAll(SQLDatabase& db) = 0;

    virtual int
    getKBUsedDB(SQLDatabase& db) = 0;

    virtual void
    readPeerFinderDB(
        SQLDatabase& db,
        std::function<void(std::string const&, int)> const& func) = 0;

    virtual void
    savePeerFinderDB(
        SQLDatabase& db,
        std::vector<PeerFinder::Store::Entry> const& v) = 0;

protected:
    static LedgerIndex
    seqToShardIndex(LedgerIndex seq);

    static LedgerIndex
    firstLedgerSeq(LedgerIndex shardIndex);

    static LedgerIndex
    lastLedgerSeq(LedgerIndex shardIndex);

    static LedgerIndex ledgersPerShard_;

private:
    static std::unordered_map<SQLInterface::DatabaseType, SQLInterface*>
        type2iface_;
    static std::unordered_map<SQLDatabase_*, DatabaseIndex> db2ind_;
    static std::map<LedgerIndex, SQLDatabase_*> txInd2db_;
    static std::map<LedgerIndex, SQLDatabase_*> lgrInd2db_;
    static std::mutex maps_mutex_;
};

class SQLDatabase_
{
public:
    SQLDatabase_(SQLInterface* iface) : iface_(iface)
    {
    }

    virtual ~SQLDatabase_()
    {
        SQLInterface::removeDatabase(this);
    }

    SQLInterface*
    getInterface()
    {
        return iface_;
    }

    SQLInterface* iface_;
};

template <class T, class C>
T
rangeCheckedCast(C c)
{
    if ((c > std::numeric_limits<T>::max()) ||
        (!std::numeric_limits<T>::is_signed && c < 0) ||
        (std::numeric_limits<T>::is_signed &&
         std::numeric_limits<C>::is_signed &&
         c < std::numeric_limits<T>::lowest()))
    {
        JLOG(debugLog().error())
            << "rangeCheckedCast domain error:"
            << " value = " << c << " min = " << std::numeric_limits<T>::lowest()
            << " max: " << std::numeric_limits<T>::max();
    }

    return static_cast<T>(c);
}

}  // namespace ripple

#endif
