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

#include <xrpld/app/ledger/AcceptedLedger.h>
#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/ledger/LedgerToJson.h>
#include <xrpld/app/ledger/TransactionMaster.h>
#include <xrpld/app/misc/Manifest.h>
#include <xrpld/app/misc/detail/AccountTxPaging.h>
#include <xrpld/app/rdb/backend/SQLiteDatabase.h>
#include <xrpld/app/rdb/backend/detail/Node.h>
#include <xrpld/core/DatabaseCon.h>
#include <xrpld/core/SociDB.h>
#include <xrpl/basics/BasicConfig.h>
#include <xrpl/basics/StringUtilities.h>
#include <xrpl/json/to_string.h>
#include <soci/sqlite3/soci-sqlite3.h>

namespace ripple {

class SQLiteDatabaseImp final : public SQLiteDatabase
{
public:
    SQLiteDatabaseImp(
        Application& app,
        Config const& config,
        JobQueue& jobQueue)
        : app_(app)
        , useTxTables_(config.useTxTables())
        , j_(app_.journal("SQLiteDatabaseImp"))
    {
        DatabaseCon::Setup const setup = setup_DatabaseCon(config, j_);
        if (!makeLedgerDBs(
                config,
                setup,
                DatabaseCon::CheckpointerSetup{&jobQueue, &app_.logs()}))
        {
            std::string_view constexpr error =
                "Failed to create ledger databases";

            JLOG(j_.fatal()) << error;
            Throw<std::runtime_error>(error.data());
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

    RelationalDatabase::CountMinMax
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

    /**
     * @brief makeLedgerDBs Opens ledger and transaction databases for the node
     *        store, and stores their descriptors in private member variables.
     * @param config Config object.
     * @param setup Path to the databases and other opening parameters.
     * @param checkpointerSetup Checkpointer parameters.
     * @return True if node databases opened successfully.
     */
    bool
    makeLedgerDBs(
        Config const& config,
        DatabaseCon::Setup const& setup,
        DatabaseCon::CheckpointerSetup const& checkpointerSetup);

    /**
     * @brief existsLedger Checks if the node store ledger database exists.
     * @return True if the node store ledger database exists.
     */
    bool
    existsLedger()
    {
        return static_cast<bool>(lgrdb_);
    }

    /**
     * @brief existsTransaction Checks if the node store transaction database
     *        exists.
     * @return True if the node store transaction database exists.
     */
    bool
    existsTransaction()
    {
        return static_cast<bool>(txdb_);
    }

    /**
     * @brief checkoutTransaction Checks out and returns node store ledger
     *        database.
     * @return Session to the node store ledger database.
     */
    auto
    checkoutLedger()
    {
        return lgrdb_->checkoutDb();
    }

    /**
     * @brief checkoutTransaction Checks out and returns the node store
     *        transaction database.
     * @return Session to the node store transaction database.
     */
    auto
    checkoutTransaction()
    {
        return txdb_->checkoutDb();
    }
};

bool
SQLiteDatabaseImp::makeLedgerDBs(
    Config const& config,
    DatabaseCon::Setup const& setup,
    DatabaseCon::CheckpointerSetup const& checkpointerSetup)
{
    auto [lgr, tx, res] =
        detail::makeLedgerDBs(config, setup, checkpointerSetup, j_);
    txdb_ = std::move(tx);
    lgrdb_ = std::move(lgr);
    return res;
}

std::optional<LedgerIndex>
SQLiteDatabaseImp::getMinLedgerSeq()
{
    /* if databases exists, use it */
    if (existsLedger())
    {
        auto db = checkoutLedger();
        return detail::getMinLedgerSeq(*db, detail::TableType::Ledgers);
    }

    /* else return empty value */
    return {};
}

std::optional<LedgerIndex>
SQLiteDatabaseImp::getTransactionsMinLedgerSeq()
{
    if (!useTxTables_)
        return {};

    if (existsTransaction())
    {
        auto db = checkoutTransaction();
        return detail::getMinLedgerSeq(*db, detail::TableType::Transactions);
    }

    return {};
}

std::optional<LedgerIndex>
SQLiteDatabaseImp::getAccountTransactionsMinLedgerSeq()
{
    if (!useTxTables_)
        return {};

    if (existsTransaction())
    {
        auto db = checkoutTransaction();
        return detail::getMinLedgerSeq(
            *db, detail::TableType::AccountTransactions);
    }

    return {};
}

std::optional<LedgerIndex>
SQLiteDatabaseImp::getMaxLedgerSeq()
{
    if (existsLedger())
    {
        auto db = checkoutLedger();
        return detail::getMaxLedgerSeq(*db, detail::TableType::Ledgers);
    }

    return {};
}

void
SQLiteDatabaseImp::deleteTransactionByLedgerSeq(LedgerIndex ledgerSeq)
{
    if (!useTxTables_)
        return;

    if (existsTransaction())
    {
        auto db = checkoutTransaction();
        detail::deleteByLedgerSeq(
            *db, detail::TableType::Transactions, ledgerSeq);
        return;
    }
}

void
SQLiteDatabaseImp::deleteBeforeLedgerSeq(LedgerIndex ledgerSeq)
{
    if (existsLedger())
    {
        auto db = checkoutLedger();
        detail::deleteBeforeLedgerSeq(
            *db, detail::TableType::Ledgers, ledgerSeq);
        return;
    }
}

void
SQLiteDatabaseImp::deleteTransactionsBeforeLedgerSeq(LedgerIndex ledgerSeq)
{
    if (!useTxTables_)
        return;

    if (existsTransaction())
    {
        auto db = checkoutTransaction();
        detail::deleteBeforeLedgerSeq(
            *db, detail::TableType::Transactions, ledgerSeq);
        return;
    }
}

void
SQLiteDatabaseImp::deleteAccountTransactionsBeforeLedgerSeq(
    LedgerIndex ledgerSeq)
{
    if (!useTxTables_)
        return;

    if (existsTransaction())
    {
        auto db = checkoutTransaction();
        detail::deleteBeforeLedgerSeq(
            *db, detail::TableType::AccountTransactions, ledgerSeq);
        return;
    }
}

std::size_t
SQLiteDatabaseImp::getTransactionCount()
{
    if (!useTxTables_)
        return 0;

    if (existsTransaction())
    {
        auto db = checkoutTransaction();
        return detail::getRows(*db, detail::TableType::Transactions);
    }

    return 0;
}

std::size_t
SQLiteDatabaseImp::getAccountTransactionCount()
{
    if (!useTxTables_)
        return 0;

    if (existsTransaction())
    {
        auto db = checkoutTransaction();
        return detail::getRows(*db, detail::TableType::AccountTransactions);
    }

    return 0;
}

RelationalDatabase::CountMinMax
SQLiteDatabaseImp::getLedgerCountMinMax()
{
    if (existsLedger())
    {
        auto db = checkoutLedger();
        return detail::getRowsMinMax(*db, detail::TableType::Ledgers);
    }

    return {0, 0, 0};
}

bool
SQLiteDatabaseImp::saveValidatedLedger(
    std::shared_ptr<Ledger const> const& ledger,
    bool current)
{
    if (existsLedger())
    {
        if (!detail::saveValidatedLedger(
                *lgrdb_, *txdb_, app_, ledger, current))
            return false;
    }

    return true;
}

std::optional<LedgerInfo>
SQLiteDatabaseImp::getLedgerInfoByIndex(LedgerIndex ledgerSeq)
{
    if (existsLedger())
    {
        auto db = checkoutLedger();
        auto const res = detail::getLedgerInfoByIndex(*db, ledgerSeq, j_);

        if (res.has_value())
            return res;
    }

    return {};
}

std::optional<LedgerInfo>
SQLiteDatabaseImp::getNewestLedgerInfo()
{
    if (existsLedger())
    {
        auto db = checkoutLedger();
        auto const res = detail::getNewestLedgerInfo(*db, j_);

        if (res.has_value())
            return res;
    }

    return {};
}

std::optional<LedgerInfo>
SQLiteDatabaseImp::getLimitedOldestLedgerInfo(LedgerIndex ledgerFirstIndex)
{
    if (existsLedger())
    {
        auto db = checkoutLedger();
        auto const res =
            detail::getLimitedOldestLedgerInfo(*db, ledgerFirstIndex, j_);

        if (res.has_value())
            return res;
    }

    return {};
}

std::optional<LedgerInfo>
SQLiteDatabaseImp::getLimitedNewestLedgerInfo(LedgerIndex ledgerFirstIndex)
{
    if (existsLedger())
    {
        auto db = checkoutLedger();
        auto const res =
            detail::getLimitedNewestLedgerInfo(*db, ledgerFirstIndex, j_);

        if (res.has_value())
            return res;
    }

    return {};
}

std::optional<LedgerInfo>
SQLiteDatabaseImp::getLedgerInfoByHash(uint256 const& ledgerHash)
{
    if (existsLedger())
    {
        auto db = checkoutLedger();
        auto const res = detail::getLedgerInfoByHash(*db, ledgerHash, j_);

        if (res.has_value())
            return res;
    }

    return {};
}

uint256
SQLiteDatabaseImp::getHashByIndex(LedgerIndex ledgerIndex)
{
    if (existsLedger())
    {
        auto db = checkoutLedger();
        auto const res = detail::getHashByIndex(*db, ledgerIndex);

        if (res.isNonZero())
            return res;
    }

    return uint256();
}

std::optional<LedgerHashPair>
SQLiteDatabaseImp::getHashesByIndex(LedgerIndex ledgerIndex)
{
    if (existsLedger())
    {
        auto db = checkoutLedger();
        auto const res = detail::getHashesByIndex(*db, ledgerIndex, j_);

        if (res.has_value())
            return res;
    }

    return {};
}

std::map<LedgerIndex, LedgerHashPair>
SQLiteDatabaseImp::getHashesByIndex(LedgerIndex minSeq, LedgerIndex maxSeq)
{
    if (existsLedger())
    {
        auto db = checkoutLedger();
        auto const res = detail::getHashesByIndex(*db, minSeq, maxSeq, j_);

        if (!res.empty())
            return res;
    }

    return {};
}

std::vector<std::shared_ptr<Transaction>>
SQLiteDatabaseImp::getTxHistory(LedgerIndex startIndex)
{
    if (!useTxTables_)
        return {};

    if (existsTransaction())
    {
        auto db = checkoutTransaction();
        auto const res = detail::getTxHistory(*db, app_, startIndex, 20).first;

        if (!res.empty())
            return res;
    }

    return {};
}

RelationalDatabase::AccountTxs
SQLiteDatabaseImp::getOldestAccountTxs(AccountTxOptions const& options)
{
    if (!useTxTables_)
        return {};

    LedgerMaster& ledgerMaster = app_.getLedgerMaster();

    if (existsTransaction())
    {
        auto db = checkoutTransaction();
        return detail::getOldestAccountTxs(*db, app_, ledgerMaster, options, j_)
            .first;
    }

    return {};
}

RelationalDatabase::AccountTxs
SQLiteDatabaseImp::getNewestAccountTxs(AccountTxOptions const& options)
{
    if (!useTxTables_)
        return {};

    LedgerMaster& ledgerMaster = app_.getLedgerMaster();

    if (existsTransaction())
    {
        auto db = checkoutTransaction();
        return detail::getNewestAccountTxs(*db, app_, ledgerMaster, options, j_)
            .first;
    }

    return {};
}

RelationalDatabase::MetaTxsList
SQLiteDatabaseImp::getOldestAccountTxsB(AccountTxOptions const& options)
{
    if (!useTxTables_)
        return {};

    if (existsTransaction())
    {
        auto db = checkoutTransaction();
        return detail::getOldestAccountTxsB(*db, app_, options, j_).first;
    }

    return {};
}

RelationalDatabase::MetaTxsList
SQLiteDatabaseImp::getNewestAccountTxsB(AccountTxOptions const& options)
{
    if (!useTxTables_)
        return {};

    if (existsTransaction())
    {
        auto db = checkoutTransaction();
        return detail::getNewestAccountTxsB(*db, app_, options, j_).first;
    }

    return {};
}

std::pair<
    RelationalDatabase::AccountTxs,
    std::optional<RelationalDatabase::AccountTxMarker>>
SQLiteDatabaseImp::oldestAccountTxPage(AccountTxPageOptions const& options)
{
    if (!useTxTables_)
        return {};

    static std::uint32_t const page_length(200);
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
        auto newmarker =
            detail::oldestAccountTxPage(
                *db, onUnsavedLedger, onTransaction, options, page_length)
                .first;
        return {ret, newmarker};
    }

    return {};
}

std::pair<
    RelationalDatabase::AccountTxs,
    std::optional<RelationalDatabase::AccountTxMarker>>
SQLiteDatabaseImp::newestAccountTxPage(AccountTxPageOptions const& options)
{
    if (!useTxTables_)
        return {};

    static std::uint32_t const page_length(200);
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
        auto newmarker =
            detail::newestAccountTxPage(
                *db, onUnsavedLedger, onTransaction, options, page_length)
                .first;
        return {ret, newmarker};
    }

    return {};
}

std::pair<
    RelationalDatabase::MetaTxsList,
    std::optional<RelationalDatabase::AccountTxMarker>>
SQLiteDatabaseImp::oldestAccountTxPageB(AccountTxPageOptions const& options)
{
    if (!useTxTables_)
        return {};

    static std::uint32_t const page_length(500);
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
        auto newmarker =
            detail::oldestAccountTxPage(
                *db, onUnsavedLedger, onTransaction, options, page_length)
                .first;
        return {ret, newmarker};
    }

    return {};
}

std::pair<
    RelationalDatabase::MetaTxsList,
    std::optional<RelationalDatabase::AccountTxMarker>>
SQLiteDatabaseImp::newestAccountTxPageB(AccountTxPageOptions const& options)
{
    if (!useTxTables_)
        return {};

    static std::uint32_t const page_length(500);
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
        auto newmarker =
            detail::newestAccountTxPage(
                *db, onUnsavedLedger, onTransaction, options, page_length)
                .first;
        return {ret, newmarker};
    }

    return {};
}

std::variant<RelationalDatabase::AccountTx, TxSearched>
SQLiteDatabaseImp::getTransaction(
    uint256 const& id,
    std::optional<ClosedInterval<std::uint32_t>> const& range,
    error_code_i& ec)
{
    if (!useTxTables_)
        return TxSearched::unknown;

    if (existsTransaction())
    {
        auto db = checkoutTransaction();
        return detail::getTransaction(*db, app_, id, range, ec);
    }

    return TxSearched::unknown;
}

bool
SQLiteDatabaseImp::ledgerDbHasSpace(Config const& config)
{
    if (existsLedger())
    {
        auto db = checkoutLedger();
        return detail::dbHasSpace(*db, config, j_);
    }

    return true;
}

bool
SQLiteDatabaseImp::transactionDbHasSpace(Config const& config)
{
    if (!useTxTables_)
        return true;

    if (existsTransaction())
    {
        auto db = checkoutTransaction();
        return detail::dbHasSpace(*db, config, j_);
    }

    return true;
}

std::uint32_t
SQLiteDatabaseImp::getKBUsedAll()
{
    if (existsLedger())
    {
        return ripple::getKBUsedAll(lgrdb_->getSession());
    }

    return 0;
}

std::uint32_t
SQLiteDatabaseImp::getKBUsedLedger()
{
    if (existsLedger())
    {
        return ripple::getKBUsedDB(lgrdb_->getSession());
    }

    return 0;
}

std::uint32_t
SQLiteDatabaseImp::getKBUsedTransaction()
{
    if (!useTxTables_)
        return 0;

    if (existsTransaction())
    {
        return ripple::getKBUsedDB(txdb_->getSession());
    }

    return 0;
}

void
SQLiteDatabaseImp::closeLedgerDB()
{
    lgrdb_.reset();
}

void
SQLiteDatabaseImp::closeTransactionDB()
{
    txdb_.reset();
}

std::unique_ptr<RelationalDatabase>
getSQLiteDatabase(Application& app, Config const& config, JobQueue& jobQueue)
{
    return std::make_unique<SQLiteDatabaseImp>(app, config, jobQueue);
}

}  // namespace ripple
