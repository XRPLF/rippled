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
#include <ripple/app/misc/impl/AccountTxPaging.h>
#include <ripple/app/rdb/RelationalDBInterface_nodes.h>
#include <ripple/app/rdb/RelationalDBInterface_postgres.h>
#include <ripple/app/rdb/RelationalDBInterface_shards.h>
#include <ripple/app/rdb/backend/RelationalDBInterfaceSqlite.h>
#include <ripple/basics/BasicConfig.h>
#include <ripple/basics/StringUtilities.h>
#include <ripple/core/DatabaseCon.h>
#include <ripple/core/SociDB.h>
#include <ripple/json/to_string.h>
#include <ripple/nodestore/DatabaseShard.h>
#include <boost/algorithm/string.hpp>
#include <boost/range/adaptor/transformed.hpp>
#include <soci/sqlite3/soci-sqlite3.h>

namespace ripple {

class RelationalDBInterfaceSqliteImp : public RelationalDBInterfaceSqlite
{
public:
    RelationalDBInterfaceSqliteImp(
        Application& app,
        Config const& config,
        JobQueue& jobQueue)
        : app_(app)
        , useTxTables_(config.useTxTables())
        , j_(app_.journal("Ledger"))
    {
        DatabaseCon::Setup setup = setup_DatabaseCon(config, j_);
        if (!makeLedgerDBs(
                config,
                setup,
                DatabaseCon::CheckpointerSetup{&jobQueue, &app_.logs()}))
        {
            JLOG(app_.journal("RelationalDBInterfaceSqlite").fatal())
                << "AccountTransactions database should not have a primary key";
            Throw<std::runtime_error>(
                "AccountTransactions database initialization failed.");
        }

        if (app.getShardStore() &&
            !makeMetaDBs(
                config,
                setup,
                DatabaseCon::CheckpointerSetup{&jobQueue, &app_.logs()}))
        {
            JLOG(app_.journal("RelationalDBInterfaceSqlite").fatal())
                << "Error during meta DB init";
            Throw<std::runtime_error>(
                "Shard meta database initialization failed.");
        }
    }

    std::optional<LedgerIndex>
    getMinLedgerSeq() override;

    std::optional<LedgerIndex>
    getTransactionsMinLedgerSeq() override;

    std::optional<LedgerIndex>
    getAccountTransactionsMinLedgerSeq() override;

    std::optional<LedgerIndex>
    getMaxLedgerSeq() override;

    void
    deleteTransactionByLedgerSeq(LedgerIndex ledgerSeq) override;

    void
    deleteBeforeLedgerSeq(LedgerIndex ledgerSeq) override;

    void
    deleteTransactionsBeforeLedgerSeq(LedgerIndex ledgerSeq) override;

    void
    deleteAccountTransactionsBeforeLedgerSeq(LedgerIndex ledgerSeq) override;

    std::size_t
    getTransactionCount() override;

    std::size_t
    getAccountTransactionCount() override;

    RelationalDBInterface::CountMinMax
    getLedgerCountMinMax() override;

    bool
    saveValidatedLedger(
        std::shared_ptr<Ledger const> const& ledger,
        bool current) override;

    std::optional<LedgerInfo>
    getLedgerInfoByIndex(LedgerIndex ledgerSeq) override;

    std::optional<LedgerInfo>
    getNewestLedgerInfo() override;

    std::optional<LedgerInfo>
    getLimitedOldestLedgerInfo(LedgerIndex ledgerFirstIndex) override;

    std::optional<LedgerInfo>
    getLimitedNewestLedgerInfo(LedgerIndex ledgerFirstIndex) override;

    std::optional<LedgerInfo>
    getLedgerInfoByHash(uint256 const& ledgerHash) override;

    uint256
    getHashByIndex(LedgerIndex ledgerIndex) override;

    std::optional<LedgerHashPair>
    getHashesByIndex(LedgerIndex ledgerIndex) override;

    std::map<LedgerIndex, LedgerHashPair>
    getHashesByIndex(LedgerIndex minSeq, LedgerIndex maxSeq) override;

    std::vector<std::shared_ptr<Transaction>>
    getTxHistory(LedgerIndex startIndex) override;

    AccountTxs
    getOldestAccountTxs(AccountTxOptions const& options) override;

    AccountTxs
    getNewestAccountTxs(AccountTxOptions const& options) override;

    MetaTxsList
    getOldestAccountTxsB(AccountTxOptions const& options) override;

    MetaTxsList
    getNewestAccountTxsB(AccountTxOptions const& options) override;

    std::pair<AccountTxs, std::optional<AccountTxMarker>>
    oldestAccountTxPage(AccountTxPageOptions const& options) override;

    std::pair<AccountTxs, std::optional<AccountTxMarker>>
    newestAccountTxPage(AccountTxPageOptions const& options) override;

    std::pair<MetaTxsList, std::optional<AccountTxMarker>>
    oldestAccountTxPageB(AccountTxPageOptions const& options) override;

    std::pair<MetaTxsList, std::optional<AccountTxMarker>>
    newestAccountTxPageB(AccountTxPageOptions const& options) override;

    std::variant<AccountTx, TxSearched>
    getTransaction(
        uint256 const& id,
        std::optional<ClosedInterval<std::uint32_t>> const& range,
        error_code_i& ec) override;

    bool
    ledgerDbHasSpace(Config const& config) override;

    bool
    transactionDbHasSpace(Config const& config) override;

    std::uint32_t
    getKBUsedAll() override;

    std::uint32_t
    getKBUsedLedger() override;

    std::uint32_t
    getKBUsedTransaction() override;

    void
    closeLedgerDB() override;

    void
    closeTransactionDB() override;

private:
    Application& app_;
    bool const useTxTables_;
    beast::Journal j_;
    std::unique_ptr<DatabaseCon> lgrdb_, txdb_;
    std::unique_ptr<DatabaseCon> lgrMetaDB_, txMetaDB_;

    /**
     * @brief makeLedgerDBs Opens node ledger and transaction databases,
     *        and saves its descriptors into internal variables.
     * @param config Config object.
     * @param setup Path to database and other opening parameters.
     * @param checkpointerSetup Checkpointer parameters.
     * @return True if node databases opened succsessfully.
     */
    bool
    makeLedgerDBs(
        Config const& config,
        DatabaseCon::Setup const& setup,
        DatabaseCon::CheckpointerSetup const& checkpointerSetup);

    /**
     * @brief makeMetaDBs Opens shard index lookup databases, and saves
     *        their descriptors into internal variables.
     * @param config Config object.
     * @param setup Path to database and other opening parameters.
     * @param checkpointerSetup Checkpointer parameters.
     * @return True if node databases opened successfully.
     */
    bool
    makeMetaDBs(
        Config const& config,
        DatabaseCon::Setup const& setup,
        DatabaseCon::CheckpointerSetup const& checkpointerSetup);

    /**
     * @brief seqToShardIndex Converts ledgers sequence to shard index.
     * @param ledgerSeq Ledger sequence.
     * @return Shard index.
     */
    std::uint32_t
    seqToShardIndex(LedgerIndex ledgerSeq)
    {
        return app_.getShardStore()->seqToShardIndex(ledgerSeq);
    }

    /**
     * @brief firstLedgerSeq Returns first ledger sequence for given shard.
     * @param shardIndex Shard Index.
     * @return First ledger sequence.
     */
    LedgerIndex
    firstLedgerSeq(std::uint32_t shardIndex)
    {
        return app_.getShardStore()->firstLedgerSeq(shardIndex);
    }

    /**
     * @brief lastLedgerSeq Returns last ledger sequence for given shard.
     * @param shardIndex Shard Index.
     * @return Last ledger sequence.
     */
    LedgerIndex
    lastLedgerSeq(std::uint32_t shardIndex)
    {
        return app_.getShardStore()->lastLedgerSeq(shardIndex);
    }

    /**
     * @brief existsLedger Checks if node ledger DB exists.
     * @return True if node ledger DB exists.
     */
    bool
    existsLedger()
    {
        return static_cast<bool>(lgrdb_);
    }

    /**
     * @brief existsTransaction Checks if node transaction DB exists.
     * @return True if node transaction DB exists.
     */
    bool
    existsTransaction()
    {
        return static_cast<bool>(txdb_);
    }

    /**
     * shardStoreExists Checks whether the shard store exists
     * @return True if the shard store exists
     */
    bool
    shardStoreExists()
    {
        return app_.getShardStore() != nullptr;
    }

    /**
     * @brief checkoutTransaction Checks out and returns node ledger DB.
     * @return Session to node ledger DB.
     */
    auto
    checkoutLedger()
    {
        return lgrdb_->checkoutDb();
    }

    /**
     * @brief checkoutTransaction Checks out and returns node transaction DB.
     * @return Session to node transaction DB.
     */
    auto
    checkoutTransaction()
    {
        return txdb_->checkoutDb();
    }

    /**
     * @brief doLedger Checks out ledger database for shard
     *        containing given ledger and calls given callback function passing
     *        shard index and session with the database to it.
     * @param ledgerSeq Ledger sequence.
     * @param callback Callback function to call.
     * @return Value returned by callback function.
     */
    bool
    doLedger(
        LedgerIndex ledgerSeq,
        std::function<bool(soci::session& session)> const& callback)
    {
        return app_.getShardStore()->callForLedgerSQLByLedgerSeq(
            ledgerSeq, callback);
    }

    /**
     * @brief doTransaction Checks out transaction database for shard
     *        containing given ledger and calls given callback function passing
     *        shard index and session with the database to it.
     * @param ledgerSeq Ledger sequence.
     * @param callback Callback function to call.
     * @return Value returned by callback function.
     */
    bool
    doTransaction(
        LedgerIndex ledgerSeq,
        std::function<bool(soci::session& session)> const& callback)
    {
        return app_.getShardStore()->callForTransactionSQLByLedgerSeq(
            ledgerSeq, callback);
    }

    /**
     * @brief iterateLedgerForward Checks out ledger databases for
     *        all shards in ascending order starting from given shard index
     *        until shard with the largest index visited or callback returned
     *        false. For each visited shard calls given callback function
     *        passing shard index and session with the database to it.
     * @param firstIndex Start shard index to visit or none if all shards
     *        should be visited.
     * @param callback Callback function to call.
     * @return True if each callback function returned true, false otherwise.
     */
    bool
    iterateLedgerForward(
        std::optional<std::uint32_t> firstIndex,
        std::function<
            bool(soci::session& session, std::uint32_t shardIndex)> const&
            callback)
    {
        return app_.getShardStore()->iterateLedgerSQLsForward(
            firstIndex, callback);
    }

    /**
     * @brief iterateTransactionForward Checks out transaction databases for
     *        all shards in ascending order starting from given shard index
     *        until shard with the largest index visited or callback returned
     *        false. For each visited shard calls given callback function
     *        passing shard index and session with the database to it.
     * @param firstIndex Start shard index to visit or none if all shards
     *        should be visited.
     * @param callback Callback function to call.
     * @return True if each callback function returned true, false otherwise.
     */
    bool
    iterateTransactionForward(
        std::optional<std::uint32_t> firstIndex,
        std::function<
            bool(soci::session& session, std::uint32_t shardIndex)> const&
            callback)
    {
        return app_.getShardStore()->iterateLedgerSQLsForward(
            firstIndex, callback);
    }

    /**
     * @brief iterateLedgerBack Checks out ledger databases for
     *        all shards in descending order starting from given shard index
     *        until shard with the smallest index visited or callback returned
     *        false. For each visited shard calls given callback function
     *        passing shard index and session with the database to it.
     * @param firstIndex Start shard index to visit or none if all shards
     *        should be visited.
     * @param callback Callback function to call.
     * @return True if each callback function returned true, false otherwise.
     */
    bool
    iterateLedgerBack(
        std::optional<std::uint32_t> firstIndex,
        std::function<
            bool(soci::session& session, std::uint32_t shardIndex)> const&
            callback)
    {
        return app_.getShardStore()->iterateLedgerSQLsBack(
            firstIndex, callback);
    }

    /**
     * @brief iterateTransactionForward Checks out transaction databases for
     *        all shards in descending order starting from given shard index
     *        until shard with the smallest index visited or callback returned
     *        false. For each visited shard calls given callback function
     *        passing shard index and session with the database to it.
     * @param firstIndex Start shard index to visit or none if all shards
     *        should be visited.
     * @param callback Callback function to call.
     * @return True if each callback function returned true, false otherwise.
     */
    bool
    iterateTransactionBack(
        std::optional<std::uint32_t> firstIndex,
        std::function<
            bool(soci::session& session, std::uint32_t shardIndex)> const&
            callback)
    {
        return app_.getShardStore()->iterateLedgerSQLsBack(
            firstIndex, callback);
    }
};

bool
RelationalDBInterfaceSqliteImp::makeLedgerDBs(
    Config const& config,
    DatabaseCon::Setup const& setup,
    DatabaseCon::CheckpointerSetup const& checkpointerSetup)
{
    auto [lgr, tx, res] =
        ripple::makeLedgerDBs(config, setup, checkpointerSetup);
    txdb_ = std::move(tx);
    lgrdb_ = std::move(lgr);
    return res;
}

bool
RelationalDBInterfaceSqliteImp::makeMetaDBs(
    Config const& config,
    DatabaseCon::Setup const& setup,
    DatabaseCon::CheckpointerSetup const& checkpointerSetup)
{
    auto [lgrMetaDB, txMetaDB] =
        ripple::makeMetaDBs(config, setup, checkpointerSetup);

    txMetaDB_ = std::move(txMetaDB);
    lgrMetaDB_ = std::move(lgrMetaDB);

    return true;
}

std::optional<LedgerIndex>
RelationalDBInterfaceSqliteImp::getMinLedgerSeq()
{
    /* if databases exists, use it */
    if (existsLedger())
    {
        auto db = checkoutLedger();
        return ripple::getMinLedgerSeq(*db, TableType::Ledgers);
    }

    /* else use shard databases, if available */
    if (shardStoreExists())
    {
        std::optional<LedgerIndex> res;
        iterateLedgerForward(
            {}, [&](soci::session& session, std::uint32_t shardIndex) {
                res = ripple::getMinLedgerSeq(session, TableType::Ledgers);
                return !res;
            });
        return res;
    }

    /* else return empty value */
    return {};
}

std::optional<LedgerIndex>
RelationalDBInterfaceSqliteImp::getTransactionsMinLedgerSeq()
{
    if (!useTxTables_)
        return {};

    if (existsTransaction())
    {
        auto db = checkoutTransaction();
        return ripple::getMinLedgerSeq(*db, TableType::Transactions);
    }

    if (shardStoreExists())
    {
        std::optional<LedgerIndex> res;
        iterateTransactionForward(
            {}, [&](soci::session& session, std::uint32_t shardIndex) {
                res = ripple::getMinLedgerSeq(session, TableType::Transactions);
                return !res;
            });
        return res;
    }

    return {};
}

std::optional<LedgerIndex>
RelationalDBInterfaceSqliteImp::getAccountTransactionsMinLedgerSeq()
{
    if (!useTxTables_)
        return {};

    if (existsTransaction())
    {
        auto db = checkoutTransaction();
        return ripple::getMinLedgerSeq(*db, TableType::AccountTransactions);
    }

    if (shardStoreExists())
    {
        std::optional<LedgerIndex> res;
        iterateTransactionForward(
            {}, [&](soci::session& session, std::uint32_t shardIndex) {
                res = ripple::getMinLedgerSeq(
                    session, TableType::AccountTransactions);
                return !res;
            });
        return res;
    }

    return {};
}

std::optional<LedgerIndex>
RelationalDBInterfaceSqliteImp::getMaxLedgerSeq()
{
    if (existsLedger())
    {
        auto db = checkoutLedger();
        return ripple::getMaxLedgerSeq(*db, TableType::Ledgers);
    }

    if (shardStoreExists())
    {
        std::optional<LedgerIndex> res;
        iterateLedgerBack(
            {}, [&](soci::session& session, std::uint32_t shardIndex) {
                res = ripple::getMaxLedgerSeq(session, TableType::Ledgers);
                return !res;
            });
        return res;
    }

    return {};
}

void
RelationalDBInterfaceSqliteImp::deleteTransactionByLedgerSeq(
    LedgerIndex ledgerSeq)
{
    if (!useTxTables_)
        return;

    if (existsTransaction())
    {
        auto db = checkoutTransaction();
        ripple::deleteByLedgerSeq(*db, TableType::Transactions, ledgerSeq);
        return;
    }

    if (shardStoreExists())
    {
        doTransaction(ledgerSeq, [&](soci::session& session) {
            ripple::deleteByLedgerSeq(
                session, TableType::Transactions, ledgerSeq);
            return true;
        });
    }
}

void
RelationalDBInterfaceSqliteImp::deleteBeforeLedgerSeq(LedgerIndex ledgerSeq)
{
    if (existsLedger())
    {
        auto db = checkoutLedger();
        ripple::deleteBeforeLedgerSeq(*db, TableType::Ledgers, ledgerSeq);
        return;
    }

    if (shardStoreExists())
    {
        iterateLedgerBack(
            seqToShardIndex(ledgerSeq),
            [&](soci::session& session, std::uint32_t shardIndex) {
                ripple::deleteBeforeLedgerSeq(
                    session, TableType::Ledgers, ledgerSeq);
                return true;
            });
    }
}

void
RelationalDBInterfaceSqliteImp::deleteTransactionsBeforeLedgerSeq(
    LedgerIndex ledgerSeq)
{
    if (!useTxTables_)
        return;

    if (existsTransaction())
    {
        auto db = checkoutTransaction();
        ripple::deleteBeforeLedgerSeq(*db, TableType::Transactions, ledgerSeq);
        return;
    }

    if (shardStoreExists())
    {
        iterateTransactionBack(
            seqToShardIndex(ledgerSeq),
            [&](soci::session& session, std::uint32_t shardIndex) {
                ripple::deleteBeforeLedgerSeq(
                    session, TableType::Transactions, ledgerSeq);
                return true;
            });
    }
}

void
RelationalDBInterfaceSqliteImp::deleteAccountTransactionsBeforeLedgerSeq(
    LedgerIndex ledgerSeq)
{
    if (!useTxTables_)
        return;

    if (existsTransaction())
    {
        auto db = checkoutTransaction();
        ripple::deleteBeforeLedgerSeq(
            *db, TableType::AccountTransactions, ledgerSeq);
        return;
    }

    if (shardStoreExists())
    {
        iterateTransactionBack(
            seqToShardIndex(ledgerSeq),
            [&](soci::session& session, std::uint32_t shardIndex) {
                ripple::deleteBeforeLedgerSeq(
                    session, TableType::AccountTransactions, ledgerSeq);
                return true;
            });
    }
}

std::size_t
RelationalDBInterfaceSqliteImp::getTransactionCount()
{
    if (!useTxTables_)
        return 0;

    if (existsTransaction())
    {
        auto db = checkoutTransaction();
        return ripple::getRows(*db, TableType::Transactions);
    }

    if (shardStoreExists())
    {
        std::size_t rows = 0;
        iterateTransactionForward(
            {}, [&](soci::session& session, std::uint32_t shardIndex) {
                rows += ripple::getRows(session, TableType::Transactions);
                return true;
            });
        return rows;
    }

    return 0;
}

std::size_t
RelationalDBInterfaceSqliteImp::getAccountTransactionCount()
{
    if (!useTxTables_)
        return 0;

    if (existsTransaction())
    {
        auto db = checkoutTransaction();
        return ripple::getRows(*db, TableType::AccountTransactions);
    }

    if (shardStoreExists())
    {
        std::size_t rows = 0;
        iterateTransactionForward(
            {}, [&](soci::session& session, std::uint32_t shardIndex) {
                rows +=
                    ripple::getRows(session, TableType::AccountTransactions);
                return true;
            });
        return rows;
    }

    return 0;
}

RelationalDBInterface::CountMinMax
RelationalDBInterfaceSqliteImp::getLedgerCountMinMax()
{
    if (existsLedger())
    {
        auto db = checkoutLedger();
        return ripple::getRowsMinMax(*db, TableType::Ledgers);
    }

    if (shardStoreExists())
    {
        CountMinMax res{0, 0, 0};
        iterateLedgerForward(
            {}, [&](soci::session& session, std::uint32_t shardIndex) {
                auto r = ripple::getRowsMinMax(session, TableType::Ledgers);
                if (r.numberOfRows)
                {
                    res.numberOfRows += r.numberOfRows;
                    if (res.minLedgerSequence == 0)
                        res.minLedgerSequence = r.minLedgerSequence;
                    res.maxLedgerSequence = r.maxLedgerSequence;
                }
                return true;
            });
        return res;
    }

    return {0, 0, 0};
}

bool
RelationalDBInterfaceSqliteImp::saveValidatedLedger(
    std::shared_ptr<Ledger const> const& ledger,
    bool current)
{
    if (existsLedger())
    {
        if (!ripple::saveValidatedLedger(
                *lgrdb_, *txdb_, app_, ledger, current))
            return false;
    }

    if (auto shardStore = app_.getShardStore(); shardStore)
    {
        if (ledger->info().seq < shardStore->earliestLedgerSeq())
            // For the moment return false only when the ShardStore
            // should accept the ledger, but fails when attempting
            // to do so, i.e. when saveLedgerMeta fails. Later when
            // the ShardStore supercedes the NodeStore, change this
            // line to return false if the ledger is too early.
            return true;

        auto lgrMetaSession = lgrMetaDB_->checkoutDb();
        auto txMetaSession = txMetaDB_->checkoutDb();

        return ripple::saveLedgerMeta(
            ledger,
            app_,
            *lgrMetaSession,
            *txMetaSession,
            shardStore->seqToShardIndex(ledger->info().seq));
    }

    return true;
}

std::optional<LedgerInfo>
RelationalDBInterfaceSqliteImp::getLedgerInfoByIndex(LedgerIndex ledgerSeq)
{
    if (existsLedger())
    {
        auto db = checkoutLedger();
        return ripple::getLedgerInfoByIndex(*db, ledgerSeq, j_);
    }

    if (shardStoreExists())
    {
        std::optional<LedgerInfo> res;
        doLedger(ledgerSeq, [&](soci::session& session) {
            res = ripple::getLedgerInfoByIndex(session, ledgerSeq, j_);
            return true;
        });
        return res;
    }

    return {};
}

std::optional<LedgerInfo>
RelationalDBInterfaceSqliteImp::getNewestLedgerInfo()
{
    if (existsLedger())
    {
        auto db = checkoutLedger();
        return ripple::getNewestLedgerInfo(*db, j_);
    }

    if (shardStoreExists())
    {
        std::optional<LedgerInfo> res;
        iterateLedgerBack(
            {}, [&](soci::session& session, std::uint32_t shardIndex) {
                if (auto info = ripple::getNewestLedgerInfo(session, j_))
                {
                    res = info;
                    return false;
                }
                return true;
            });

        return res;
    }

    return {};
}

std::optional<LedgerInfo>
RelationalDBInterfaceSqliteImp::getLimitedOldestLedgerInfo(
    LedgerIndex ledgerFirstIndex)
{
    if (existsLedger())
    {
        auto db = checkoutLedger();
        return ripple::getLimitedOldestLedgerInfo(*db, ledgerFirstIndex, j_);
    }

    if (shardStoreExists())
    {
        std::optional<LedgerInfo> res;
        iterateLedgerForward(
            seqToShardIndex(ledgerFirstIndex),
            [&](soci::session& session, std::uint32_t shardIndex) {
                if (auto info = ripple::getLimitedOldestLedgerInfo(
                        session, ledgerFirstIndex, j_))
                {
                    res = info;
                    return false;
                }
                return true;
            });

        return res;
    }

    return {};
}

std::optional<LedgerInfo>
RelationalDBInterfaceSqliteImp::getLimitedNewestLedgerInfo(
    LedgerIndex ledgerFirstIndex)
{
    if (existsLedger())
    {
        auto db = checkoutLedger();
        return ripple::getLimitedNewestLedgerInfo(*db, ledgerFirstIndex, j_);
    }

    if (shardStoreExists())
    {
        std::optional<LedgerInfo> res;
        iterateLedgerBack(
            {}, [&](soci::session& session, std::uint32_t shardIndex) {
                if (auto info = ripple::getLimitedNewestLedgerInfo(
                        session, ledgerFirstIndex, j_))
                {
                    res = info;
                    return false;
                }
                return shardIndex >= seqToShardIndex(ledgerFirstIndex);
            });

        return res;
    }

    return {};
}

std::optional<LedgerInfo>
RelationalDBInterfaceSqliteImp::getLedgerInfoByHash(uint256 const& ledgerHash)
{
    if (existsLedger())
    {
        auto db = checkoutLedger();
        return ripple::getLedgerInfoByHash(*db, ledgerHash, j_);
    }

    if (auto shardStore = app_.getShardStore())
    {
        std::optional<LedgerInfo> res;
        auto lgrMetaSession = lgrMetaDB_->checkoutDb();

        if (auto const shardIndex =
                ripple::getShardIndexforLedger(*lgrMetaSession, ledgerHash))
        {
            shardStore->callForLedgerSQLByShardIndex(
                *shardIndex, [&](soci::session& session) {
                    res = ripple::getLedgerInfoByHash(session, ledgerHash, j_);
                    return false;  // unused
                });
        }

        return res;
    }

    return {};
}

uint256
RelationalDBInterfaceSqliteImp::getHashByIndex(LedgerIndex ledgerIndex)
{
    if (existsLedger())
    {
        auto db = checkoutLedger();
        return ripple::getHashByIndex(*db, ledgerIndex);
    }

    if (shardStoreExists())
    {
        uint256 hash;
        doLedger(ledgerIndex, [&](soci::session& session) {
            hash = ripple::getHashByIndex(session, ledgerIndex);
            return true;
        });
        return hash;
    }

    return uint256();
}

std::optional<LedgerHashPair>
RelationalDBInterfaceSqliteImp::getHashesByIndex(LedgerIndex ledgerIndex)
{
    if (existsLedger())
    {
        auto db = checkoutLedger();
        return ripple::getHashesByIndex(*db, ledgerIndex, j_);
    }

    if (shardStoreExists())
    {
        std::optional<LedgerHashPair> res;
        doLedger(ledgerIndex, [&](soci::session& session) {
            res = ripple::getHashesByIndex(session, ledgerIndex, j_);
            return true;
        });
        return res;
    }

    return {};
}

std::map<LedgerIndex, LedgerHashPair>
RelationalDBInterfaceSqliteImp::getHashesByIndex(
    LedgerIndex minSeq,
    LedgerIndex maxSeq)
{
    if (existsLedger())
    {
        auto db = checkoutLedger();
        return ripple::getHashesByIndex(*db, minSeq, maxSeq, j_);
    }

    if (shardStoreExists())
    {
        std::map<LedgerIndex, LedgerHashPair> res;
        while (minSeq <= maxSeq)
        {
            LedgerIndex shardMaxSeq = lastLedgerSeq(seqToShardIndex(minSeq));
            if (shardMaxSeq > maxSeq)
                shardMaxSeq = maxSeq;
            doLedger(minSeq, [&](soci::session& session) {
                auto r =
                    ripple::getHashesByIndex(session, minSeq, shardMaxSeq, j_);
                res.insert(r.begin(), r.end());
                return true;
            });
            minSeq = shardMaxSeq + 1;
        }

        return res;
    }

    return {};
}

std::vector<std::shared_ptr<Transaction>>
RelationalDBInterfaceSqliteImp::getTxHistory(LedgerIndex startIndex)
{
    if (!useTxTables_)
        return {};

    if (existsTransaction())
    {
        auto db = checkoutTransaction();
        return ripple::getTxHistory(*db, app_, startIndex, 20, false).first;
    }

    if (shardStoreExists())
    {
        std::vector<std::shared_ptr<Transaction>> txs;
        int quantity = 20;
        iterateTransactionBack(
            {}, [&](soci::session& session, std::uint32_t shardIndex) {
                auto [tx, total] = ripple::getTxHistory(
                    session, app_, startIndex, quantity, true);
                txs.insert(txs.end(), tx.begin(), tx.end());
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

    return {};
}

RelationalDBInterface::AccountTxs
RelationalDBInterfaceSqliteImp::getOldestAccountTxs(
    AccountTxOptions const& options)
{
    if (!useTxTables_)
        return {};

    LedgerMaster& ledgerMaster = app_.getLedgerMaster();

    if (existsTransaction())
    {
        auto db = checkoutTransaction();
        return ripple::getOldestAccountTxs(
                   *db, app_, ledgerMaster, options, {}, j_)
            .first;
    }

    if (shardStoreExists())
    {
        AccountTxs ret;
        AccountTxOptions opt = options;
        int limit_used = 0;
        iterateTransactionForward(
            opt.minLedger ? seqToShardIndex(opt.minLedger)
                          : std::optional<std::uint32_t>(),
            [&](soci::session& session, std::uint32_t shardIndex) {
                if (opt.maxLedger &&
                    shardIndex > seqToShardIndex(opt.maxLedger))
                    return false;
                auto [r, total] = ripple::getOldestAccountTxs(
                    session, app_, ledgerMaster, opt, limit_used, j_);
                ret.insert(ret.end(), r.begin(), r.end());
                if (!total)
                    return false;
                if (total > 0)
                {
                    limit_used += total;
                    opt.offset = 0;
                }
                else
                {
                    /*
                     * If total < 0, then -total means number of transactions
                     * skipped, see definition of return value of function
                     * ripple::getOldestAccountTxs().
                     */
                    total = -total;
                    if (opt.offset <= total)
                        opt.offset = 0;
                    else
                        opt.offset -= total;
                }
                return true;
            });

        return ret;
    }

    return {};
}

RelationalDBInterface::AccountTxs
RelationalDBInterfaceSqliteImp::getNewestAccountTxs(
    AccountTxOptions const& options)
{
    if (!useTxTables_)
        return {};

    LedgerMaster& ledgerMaster = app_.getLedgerMaster();

    if (existsTransaction())
    {
        auto db = checkoutTransaction();
        return ripple::getNewestAccountTxs(
                   *db, app_, ledgerMaster, options, {}, j_)
            .first;
    }

    if (shardStoreExists())
    {
        AccountTxs ret;
        AccountTxOptions opt = options;
        int limit_used = 0;
        iterateTransactionBack(
            opt.maxLedger ? seqToShardIndex(opt.maxLedger)
                          : std::optional<std::uint32_t>(),
            [&](soci::session& session, std::uint32_t shardIndex) {
                if (opt.minLedger &&
                    shardIndex < seqToShardIndex(opt.minLedger))
                    return false;
                auto [r, total] = ripple::getNewestAccountTxs(
                    session, app_, ledgerMaster, opt, limit_used, j_);
                ret.insert(ret.end(), r.begin(), r.end());
                if (!total)
                    return false;
                if (total > 0)
                {
                    limit_used += total;
                    opt.offset = 0;
                }
                else
                {
                    /*
                     * If total < 0, then -total means number of transactions
                     * skipped, see definition of return value of function
                     * ripple::getNewestAccountTxs().
                     */
                    total = -total;
                    if (opt.offset <= total)
                        opt.offset = 0;
                    else
                        opt.offset -= total;
                }
                return true;
            });

        return ret;
    }

    return {};
}

RelationalDBInterface::MetaTxsList
RelationalDBInterfaceSqliteImp::getOldestAccountTxsB(
    AccountTxOptions const& options)
{
    if (!useTxTables_)
        return {};

    if (existsTransaction())
    {
        auto db = checkoutTransaction();
        return ripple::getOldestAccountTxsB(*db, app_, options, {}, j_).first;
    }

    if (shardStoreExists())
    {
        MetaTxsList ret;
        AccountTxOptions opt = options;
        int limit_used = 0;
        iterateTransactionForward(
            opt.minLedger ? seqToShardIndex(opt.minLedger)
                          : std::optional<std::uint32_t>(),
            [&](soci::session& session, std::uint32_t shardIndex) {
                if (opt.maxLedger &&
                    shardIndex > seqToShardIndex(opt.maxLedger))
                    return false;
                auto [r, total] = ripple::getOldestAccountTxsB(
                    session, app_, opt, limit_used, j_);
                ret.insert(ret.end(), r.begin(), r.end());
                if (!total)
                    return false;
                if (total > 0)
                {
                    limit_used += total;
                    opt.offset = 0;
                }
                else
                {
                    /*
                     * If total < 0, then -total means number of transactions
                     * skipped, see definition of return value of function
                     * ripple::getOldestAccountTxsB().
                     */
                    total = -total;
                    if (opt.offset <= total)
                        opt.offset = 0;
                    else
                        opt.offset -= total;
                }
                return true;
            });

        return ret;
    }

    return {};
}

RelationalDBInterface::MetaTxsList
RelationalDBInterfaceSqliteImp::getNewestAccountTxsB(
    AccountTxOptions const& options)
{
    if (!useTxTables_)
        return {};

    if (existsTransaction())
    {
        auto db = checkoutTransaction();
        return ripple::getNewestAccountTxsB(*db, app_, options, {}, j_).first;
    }

    if (shardStoreExists())
    {
        MetaTxsList ret;
        AccountTxOptions opt = options;
        int limit_used = 0;
        iterateTransactionBack(
            opt.maxLedger ? seqToShardIndex(opt.maxLedger)
                          : std::optional<std::uint32_t>(),
            [&](soci::session& session, std::uint32_t shardIndex) {
                if (opt.minLedger &&
                    shardIndex < seqToShardIndex(opt.minLedger))
                    return false;
                auto [r, total] = ripple::getNewestAccountTxsB(
                    session, app_, opt, limit_used, j_);
                ret.insert(ret.end(), r.begin(), r.end());
                if (!total)
                    return false;
                if (total > 0)
                {
                    limit_used += total;
                    opt.offset = 0;
                }
                else
                {
                    /*
                     * If total < 0, then -total means number of transactions
                     * skipped, see definition of return value of function
                     * ripple::getNewestAccountTxsB().
                     */
                    total = -total;
                    if (opt.offset <= total)
                        opt.offset = 0;
                    else
                        opt.offset -= total;
                }
                return true;
            });

        return ret;
    }

    return {};
}

std::pair<
    RelationalDBInterface::AccountTxs,
    std::optional<RelationalDBInterface::AccountTxMarker>>
RelationalDBInterfaceSqliteImp::oldestAccountTxPage(
    AccountTxPageOptions const& options)
{
    if (!useTxTables_)
        return {};

    static std::uint32_t const page_length(200);
    auto& idCache = app_.accountIDCache();
    auto onUnsavedLedger =
        std::bind(saveLedgerAsync, std::ref(app_), std::placeholders::_1);
    AccountTxs ret;
    Application& app = app_;
    auto onTransaction = [&ret, &app](
                             std::uint32_t ledger_index,
                             std::string const& status,
                             Blob&& rawTxn,
                             Blob&& rawMeta) {
        convertBlobsToTxResult(ret, ledger_index, status, rawTxn, rawMeta, app);
    };

    if (existsTransaction())
    {
        auto db = checkoutTransaction();
        auto newmarker = ripple::oldestAccountTxPage(
                             *db,
                             idCache,
                             onUnsavedLedger,
                             onTransaction,
                             options,
                             0,
                             page_length)
                             .first;
        return {ret, newmarker};
    }

    if (shardStoreExists())
    {
        AccountTxPageOptions opt = options;
        int limit_used = 0;
        iterateTransactionForward(
            opt.minLedger ? seqToShardIndex(opt.minLedger)
                          : std::optional<std::uint32_t>(),
            [&](soci::session& session, std::uint32_t shardIndex) {
                if (opt.maxLedger != UINT32_MAX &&
                    shardIndex > seqToShardIndex(opt.minLedger))
                    return false;
                auto [marker, total] = ripple::oldestAccountTxPage(
                    session,
                    idCache,
                    onUnsavedLedger,
                    onTransaction,
                    opt,
                    limit_used,
                    page_length);
                opt.marker = marker;
                if (total < 0)
                    return false;
                limit_used += total;
                return true;
            });

        return {ret, opt.marker};
    }

    return {};
}

std::pair<
    RelationalDBInterface::AccountTxs,
    std::optional<RelationalDBInterface::AccountTxMarker>>
RelationalDBInterfaceSqliteImp::newestAccountTxPage(
    AccountTxPageOptions const& options)
{
    if (!useTxTables_)
        return {};

    static std::uint32_t const page_length(200);
    auto& idCache = app_.accountIDCache();
    auto onUnsavedLedger =
        std::bind(saveLedgerAsync, std::ref(app_), std::placeholders::_1);
    AccountTxs ret;
    Application& app = app_;
    auto onTransaction = [&ret, &app](
                             std::uint32_t ledger_index,
                             std::string const& status,
                             Blob&& rawTxn,
                             Blob&& rawMeta) {
        convertBlobsToTxResult(ret, ledger_index, status, rawTxn, rawMeta, app);
    };

    if (existsTransaction())
    {
        auto db = checkoutTransaction();
        auto newmarker = ripple::newestAccountTxPage(
                             *db,
                             idCache,
                             onUnsavedLedger,
                             onTransaction,
                             options,
                             0,
                             page_length)
                             .first;
        return {ret, newmarker};
    }

    if (shardStoreExists())
    {
        AccountTxPageOptions opt = options;
        int limit_used = 0;
        iterateTransactionBack(
            opt.maxLedger != UINT32_MAX ? seqToShardIndex(opt.maxLedger)
                                        : std::optional<std::uint32_t>(),
            [&](soci::session& session, std::uint32_t shardIndex) {
                if (opt.minLedger &&
                    shardIndex < seqToShardIndex(opt.minLedger))
                    return false;
                auto [marker, total] = ripple::newestAccountTxPage(
                    session,
                    idCache,
                    onUnsavedLedger,
                    onTransaction,
                    opt,
                    limit_used,
                    page_length);
                opt.marker = marker;
                if (total < 0)
                    return false;
                limit_used += total;
                return true;
            });

        return {ret, opt.marker};
    }

    return {};
}

std::pair<
    RelationalDBInterface::MetaTxsList,
    std::optional<RelationalDBInterface::AccountTxMarker>>
RelationalDBInterfaceSqliteImp::oldestAccountTxPageB(
    AccountTxPageOptions const& options)
{
    if (!useTxTables_)
        return {};

    static std::uint32_t const page_length(500);
    auto& idCache = app_.accountIDCache();
    auto onUnsavedLedger =
        std::bind(saveLedgerAsync, std::ref(app_), std::placeholders::_1);
    MetaTxsList ret;
    auto onTransaction = [&ret](
                             std::uint32_t ledgerIndex,
                             std::string const& status,
                             Blob&& rawTxn,
                             Blob&& rawMeta) {
        ret.emplace_back(std::move(rawTxn), std::move(rawMeta), ledgerIndex);
    };

    if (existsTransaction())
    {
        auto db = checkoutTransaction();
        auto newmarker = ripple::oldestAccountTxPage(
                             *db,
                             idCache,
                             onUnsavedLedger,
                             onTransaction,
                             options,
                             0,
                             page_length)
                             .first;
        return {ret, newmarker};
    }

    if (shardStoreExists())
    {
        AccountTxPageOptions opt = options;
        int limit_used = 0;
        iterateTransactionForward(
            opt.minLedger ? seqToShardIndex(opt.minLedger)
                          : std::optional<std::uint32_t>(),
            [&](soci::session& session, std::uint32_t shardIndex) {
                if (opt.maxLedger != UINT32_MAX &&
                    shardIndex > seqToShardIndex(opt.minLedger))
                    return false;
                auto [marker, total] = ripple::oldestAccountTxPage(
                    session,
                    idCache,
                    onUnsavedLedger,
                    onTransaction,
                    opt,
                    limit_used,
                    page_length);
                opt.marker = marker;
                if (total < 0)
                    return false;
                limit_used += total;
                return true;
            });

        return {ret, opt.marker};
    }

    return {};
}

std::pair<
    RelationalDBInterface::MetaTxsList,
    std::optional<RelationalDBInterface::AccountTxMarker>>
RelationalDBInterfaceSqliteImp::newestAccountTxPageB(
    AccountTxPageOptions const& options)
{
    if (!useTxTables_)
        return {};

    static std::uint32_t const page_length(500);
    auto& idCache = app_.accountIDCache();
    auto onUnsavedLedger =
        std::bind(saveLedgerAsync, std::ref(app_), std::placeholders::_1);
    MetaTxsList ret;
    auto onTransaction = [&ret](
                             std::uint32_t ledgerIndex,
                             std::string const& status,
                             Blob&& rawTxn,
                             Blob&& rawMeta) {
        ret.emplace_back(std::move(rawTxn), std::move(rawMeta), ledgerIndex);
    };

    if (existsTransaction())
    {
        auto db = checkoutTransaction();
        auto newmarker = ripple::newestAccountTxPage(
                             *db,
                             idCache,
                             onUnsavedLedger,
                             onTransaction,
                             options,
                             0,
                             page_length)
                             .first;
        return {ret, newmarker};
    }

    if (shardStoreExists())
    {
        AccountTxPageOptions opt = options;
        int limit_used = 0;
        iterateTransactionBack(
            opt.maxLedger != UINT32_MAX ? seqToShardIndex(opt.maxLedger)
                                        : std::optional<std::uint32_t>(),
            [&](soci::session& session, std::uint32_t shardIndex) {
                if (opt.minLedger &&
                    shardIndex < seqToShardIndex(opt.minLedger))
                    return false;
                auto [marker, total] = ripple::newestAccountTxPage(
                    session,
                    idCache,
                    onUnsavedLedger,
                    onTransaction,
                    opt,
                    limit_used,
                    page_length);
                opt.marker = marker;
                if (total < 0)
                    return false;
                limit_used += total;
                return true;
            });

        return {ret, opt.marker};
    }

    return {};
}

std::variant<RelationalDBInterface::AccountTx, TxSearched>
RelationalDBInterfaceSqliteImp::getTransaction(
    uint256 const& id,
    std::optional<ClosedInterval<std::uint32_t>> const& range,
    error_code_i& ec)
{
    if (!useTxTables_)
        return TxSearched::unknown;

    if (existsTransaction())
    {
        auto db = checkoutTransaction();
        return ripple::getTransaction(*db, app_, id, range, ec);
    }

    if (auto shardStore = app_.getShardStore(); shardStore)
    {
        std::variant<AccountTx, TxSearched> res(TxSearched::unknown);
        auto txMetaSession = txMetaDB_->checkoutDb();

        if (auto const shardIndex =
                ripple::getShardIndexforTransaction(*txMetaSession, id))
        {
            shardStore->callForTransactionSQLByShardIndex(
                *shardIndex, [&](soci::session& session) {
                    std::optional<ClosedInterval<std::uint32_t>> range1;
                    if (range)
                    {
                        std::uint32_t const low = std::max(
                            range->lower(), firstLedgerSeq(*shardIndex));
                        std::uint32_t const high = std::min(
                            range->upper(), lastLedgerSeq(*shardIndex));
                        if (low <= high)
                            range1 = ClosedInterval<std::uint32_t>(low, high);
                    }
                    res = ripple::getTransaction(session, app_, id, range1, ec);

                    return res.index() == 1 &&
                        std::get<TxSearched>(res) !=
                        TxSearched::unknown;  // unused
                });
        }

        return res;
    }

    return TxSearched::unknown;
}

bool
RelationalDBInterfaceSqliteImp::ledgerDbHasSpace(Config const& config)
{
    if (existsLedger())
    {
        auto db = checkoutLedger();
        return ripple::dbHasSpace(*db, config, j_);
    }

    if (shardStoreExists())
    {
        return iterateLedgerBack(
            {}, [&](soci::session& session, std::uint32_t shardIndex) {
                return ripple::dbHasSpace(session, config, j_);
            });
    }

    return true;
}

bool
RelationalDBInterfaceSqliteImp::transactionDbHasSpace(Config const& config)
{
    if (!useTxTables_)
        return true;

    if (existsTransaction())
    {
        auto db = checkoutTransaction();
        return ripple::dbHasSpace(*db, config, j_);
    }

    if (shardStoreExists())
    {
        return iterateTransactionBack(
            {}, [&](soci::session& session, std::uint32_t shardIndex) {
                return ripple::dbHasSpace(session, config, j_);
            });
    }

    return true;
}

std::uint32_t
RelationalDBInterfaceSqliteImp::getKBUsedAll()
{
    if (existsLedger())
    {
        return ripple::getKBUsedAll(lgrdb_->getSession());
    }

    if (shardStoreExists())
    {
        std::uint32_t sum = 0;
        iterateLedgerBack(
            {}, [&](soci::session& session, std::uint32_t shardIndex) {
                sum += ripple::getKBUsedAll(session);
                return true;
            });
        return sum;
    }

    return 0;
}

std::uint32_t
RelationalDBInterfaceSqliteImp::getKBUsedLedger()
{
    if (existsLedger())
    {
        return ripple::getKBUsedDB(lgrdb_->getSession());
    }

    if (shardStoreExists())
    {
        std::uint32_t sum = 0;
        iterateLedgerBack(
            {}, [&](soci::session& session, std::uint32_t shardIndex) {
                sum += ripple::getKBUsedDB(session);
                return true;
            });
        return sum;
    }

    return 0;
}

std::uint32_t
RelationalDBInterfaceSqliteImp::getKBUsedTransaction()
{
    if (!useTxTables_)
        return 0;

    if (existsTransaction())
    {
        return ripple::getKBUsedDB(txdb_->getSession());
    }

    if (shardStoreExists())
    {
        std::uint32_t sum = 0;
        iterateTransactionBack(
            {}, [&](soci::session& session, std::uint32_t shardIndex) {
                sum += ripple::getKBUsedDB(session);
                return true;
            });
        return sum;
    }

    return 0;
}

void
RelationalDBInterfaceSqliteImp::closeLedgerDB()
{
    lgrdb_.reset();
}

void
RelationalDBInterfaceSqliteImp::closeTransactionDB()
{
    txdb_.reset();
}

std::unique_ptr<RelationalDBInterface>
getRelationalDBInterfaceSqlite(
    Application& app,
    Config const& config,
    JobQueue& jobQueue)
{
    return std::make_unique<RelationalDBInterfaceSqliteImp>(
        app, config, jobQueue);
}

}  // namespace ripple
