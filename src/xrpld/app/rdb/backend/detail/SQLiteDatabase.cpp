#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/ledger/TransactionMaster.h>
#include <xrpld/app/misc/detail/AccountTxPaging.h>
#include <xrpld/app/rdb/backend/SQLiteDatabase.h>
#include <xrpld/app/rdb/backend/detail/Node.h>

#include <xrpl/basics/StringUtilities.h>
#include <xrpl/rdb/DatabaseCon.h>
#include <xrpl/rdb/SociDB.h>

namespace xrpl {

bool
SQLiteDatabase::makeLedgerDBs(
    Config const& config,
    DatabaseCon::Setup const& setup,
    DatabaseCon::CheckpointerSetup const& checkpointerSetup)
{
    auto [lgr, tx, res] = detail::makeLedgerDBs(config, setup, checkpointerSetup, j_);
    txdb_ = std::move(tx);
    ledgerDb_ = std::move(lgr);
    return res;
}

std::optional<LedgerIndex>
SQLiteDatabase::getMinLedgerSeq()
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
SQLiteDatabase::getTransactionsMinLedgerSeq()
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
SQLiteDatabase::getAccountTransactionsMinLedgerSeq()
{
    if (!useTxTables_)
        return {};

    if (existsTransaction())
    {
        auto db = checkoutTransaction();
        return detail::getMinLedgerSeq(*db, detail::TableType::AccountTransactions);
    }

    return {};
}

std::optional<LedgerIndex>
SQLiteDatabase::getMaxLedgerSeq()
{
    if (existsLedger())
    {
        auto db = checkoutLedger();
        return detail::getMaxLedgerSeq(*db, detail::TableType::Ledgers);
    }

    return {};
}

void
SQLiteDatabase::deleteTransactionByLedgerSeq(LedgerIndex ledgerSeq)
{
    if (!useTxTables_)
        return;

    if (existsTransaction())
    {
        auto db = checkoutTransaction();
        detail::deleteByLedgerSeq(*db, detail::TableType::Transactions, ledgerSeq);
        return;
    }
}

void
SQLiteDatabase::deleteBeforeLedgerSeq(LedgerIndex ledgerSeq)
{
    if (existsLedger())
    {
        auto db = checkoutLedger();
        detail::deleteBeforeLedgerSeq(*db, detail::TableType::Ledgers, ledgerSeq);
        return;
    }
}

void
SQLiteDatabase::deleteTransactionsBeforeLedgerSeq(LedgerIndex ledgerSeq)
{
    if (!useTxTables_)
        return;

    if (existsTransaction())
    {
        auto db = checkoutTransaction();
        detail::deleteBeforeLedgerSeq(*db, detail::TableType::Transactions, ledgerSeq);
        return;
    }
}

void
SQLiteDatabase::deleteAccountTransactionsBeforeLedgerSeq(LedgerIndex ledgerSeq)
{
    if (!useTxTables_)
        return;

    if (existsTransaction())
    {
        auto db = checkoutTransaction();
        detail::deleteBeforeLedgerSeq(*db, detail::TableType::AccountTransactions, ledgerSeq);
        return;
    }
}

std::size_t
SQLiteDatabase::getTransactionCount()
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
SQLiteDatabase::getAccountTransactionCount()
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
SQLiteDatabase::getLedgerCountMinMax()
{
    if (existsLedger())
    {
        auto db = checkoutLedger();
        return detail::getRowsMinMax(*db, detail::TableType::Ledgers);
    }

    return {0, 0, 0};
}

bool
SQLiteDatabase::saveValidatedLedger(std::shared_ptr<Ledger const> const& ledger, bool current)
{
    if (existsLedger())
    {
        if (!detail::saveValidatedLedger(*ledgerDb_, txdb_, registry_.app(), ledger, current))
            return false;
    }

    return true;
}

std::optional<LedgerHeader>
SQLiteDatabase::getLedgerInfoByIndex(LedgerIndex ledgerSeq)
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

std::optional<LedgerHeader>
SQLiteDatabase::getNewestLedgerInfo()
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

std::optional<LedgerHeader>
SQLiteDatabase::getLimitedOldestLedgerInfo(LedgerIndex ledgerFirstIndex)
{
    if (existsLedger())
    {
        auto db = checkoutLedger();
        auto const res = detail::getLimitedOldestLedgerInfo(*db, ledgerFirstIndex, j_);

        if (res.has_value())
            return res;
    }

    return {};
}

std::optional<LedgerHeader>
SQLiteDatabase::getLimitedNewestLedgerInfo(LedgerIndex ledgerFirstIndex)
{
    if (existsLedger())
    {
        auto db = checkoutLedger();
        auto const res = detail::getLimitedNewestLedgerInfo(*db, ledgerFirstIndex, j_);

        if (res.has_value())
            return res;
    }

    return {};
}

std::optional<LedgerHeader>
SQLiteDatabase::getLedgerInfoByHash(uint256 const& ledgerHash)
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
SQLiteDatabase::getHashByIndex(LedgerIndex ledgerIndex)
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
SQLiteDatabase::getHashesByIndex(LedgerIndex ledgerIndex)
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
SQLiteDatabase::getHashesByIndex(LedgerIndex minSeq, LedgerIndex maxSeq)
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
SQLiteDatabase::getTxHistory(LedgerIndex startIndex)
{
    if (!useTxTables_)
        return {};

    if (existsTransaction())
    {
        auto db = checkoutTransaction();
        auto const res = detail::getTxHistory(*db, registry_.app(), startIndex, 20).first;

        if (!res.empty())
            return res;
    }

    return {};
}

RelationalDatabase::AccountTxs
SQLiteDatabase::getOldestAccountTxs(AccountTxOptions const& options)
{
    if (!useTxTables_)
        return {};

    LedgerMaster& ledgerMaster = registry_.getLedgerMaster();

    if (existsTransaction())
    {
        auto db = checkoutTransaction();
        return detail::getOldestAccountTxs(*db, registry_.app(), ledgerMaster, options, j_).first;
    }

    return {};
}

RelationalDatabase::AccountTxs
SQLiteDatabase::getNewestAccountTxs(AccountTxOptions const& options)
{
    if (!useTxTables_)
        return {};

    LedgerMaster& ledgerMaster = registry_.getLedgerMaster();

    if (existsTransaction())
    {
        auto db = checkoutTransaction();
        return detail::getNewestAccountTxs(*db, registry_.app(), ledgerMaster, options, j_).first;
    }

    return {};
}

RelationalDatabase::MetaTxsList
SQLiteDatabase::getOldestAccountTxsB(AccountTxOptions const& options)
{
    if (!useTxTables_)
        return {};

    if (existsTransaction())
    {
        auto db = checkoutTransaction();
        return detail::getOldestAccountTxsB(*db, registry_.app(), options, j_).first;
    }

    return {};
}

RelationalDatabase::MetaTxsList
SQLiteDatabase::getNewestAccountTxsB(AccountTxOptions const& options)
{
    if (!useTxTables_)
        return {};

    if (existsTransaction())
    {
        auto db = checkoutTransaction();
        return detail::getNewestAccountTxsB(*db, registry_.app(), options, j_).first;
    }

    return {};
}

std::pair<RelationalDatabase::AccountTxs, std::optional<RelationalDatabase::AccountTxMarker>>
SQLiteDatabase::oldestAccountTxPage(AccountTxPageOptions const& options)
{
    if (!useTxTables_)
        return {};

    static std::uint32_t const page_length(200);
    auto onUnsavedLedger =
        std::bind(saveLedgerAsync, std::ref(registry_.app()), std::placeholders::_1);
    AccountTxs ret;
    auto onTransaction =
        [&ret, &app = registry_.app()](
            std::uint32_t ledger_index, std::string const& status, Blob&& rawTxn, Blob&& rawMeta) {
            convertBlobsToTxResult(ret, ledger_index, status, rawTxn, rawMeta, app);
        };

    if (existsTransaction())
    {
        auto db = checkoutTransaction();
        auto newmarker =
            detail::oldestAccountTxPage(*db, onUnsavedLedger, onTransaction, options, page_length)
                .first;
        return {ret, newmarker};
    }

    return {};
}

std::pair<RelationalDatabase::AccountTxs, std::optional<RelationalDatabase::AccountTxMarker>>
SQLiteDatabase::newestAccountTxPage(AccountTxPageOptions const& options)
{
    if (!useTxTables_)
        return {};

    static std::uint32_t const page_length(200);
    auto onUnsavedLedger =
        std::bind(saveLedgerAsync, std::ref(registry_.app()), std::placeholders::_1);
    AccountTxs ret;
    auto onTransaction =
        [&ret, &app = registry_.app()](
            std::uint32_t ledger_index, std::string const& status, Blob&& rawTxn, Blob&& rawMeta) {
            convertBlobsToTxResult(ret, ledger_index, status, rawTxn, rawMeta, app);
        };

    if (existsTransaction())
    {
        auto db = checkoutTransaction();
        auto newmarker =
            detail::newestAccountTxPage(*db, onUnsavedLedger, onTransaction, options, page_length)
                .first;
        return {ret, newmarker};
    }

    return {};
}

std::pair<RelationalDatabase::MetaTxsList, std::optional<RelationalDatabase::AccountTxMarker>>
SQLiteDatabase::oldestAccountTxPageB(AccountTxPageOptions const& options)
{
    if (!useTxTables_)
        return {};

    static std::uint32_t const page_length(500);
    auto onUnsavedLedger =
        std::bind(saveLedgerAsync, std::ref(registry_.app()), std::placeholders::_1);
    MetaTxsList ret;
    auto onTransaction =
        [&ret](
            std::uint32_t ledgerIndex, std::string const& status, Blob&& rawTxn, Blob&& rawMeta) {
            ret.emplace_back(std::move(rawTxn), std::move(rawMeta), ledgerIndex);
        };

    if (existsTransaction())
    {
        auto db = checkoutTransaction();
        auto newmarker =
            detail::oldestAccountTxPage(*db, onUnsavedLedger, onTransaction, options, page_length)
                .first;
        return {ret, newmarker};
    }

    return {};
}

std::pair<RelationalDatabase::MetaTxsList, std::optional<RelationalDatabase::AccountTxMarker>>
SQLiteDatabase::newestAccountTxPageB(AccountTxPageOptions const& options)
{
    if (!useTxTables_)
        return {};

    static std::uint32_t const page_length(500);
    auto onUnsavedLedger =
        std::bind(saveLedgerAsync, std::ref(registry_.app()), std::placeholders::_1);
    MetaTxsList ret;
    auto onTransaction =
        [&ret](
            std::uint32_t ledgerIndex, std::string const& status, Blob&& rawTxn, Blob&& rawMeta) {
            ret.emplace_back(std::move(rawTxn), std::move(rawMeta), ledgerIndex);
        };

    if (existsTransaction())
    {
        auto db = checkoutTransaction();
        auto newmarker =
            detail::newestAccountTxPage(*db, onUnsavedLedger, onTransaction, options, page_length)
                .first;
        return {ret, newmarker};
    }

    return {};
}

std::variant<RelationalDatabase::AccountTx, TxSearched>
SQLiteDatabase::getTransaction(
    uint256 const& id,
    std::optional<ClosedInterval<std::uint32_t>> const& range,
    error_code_i& ec)
{
    if (!useTxTables_)
        return TxSearched::unknown;

    if (existsTransaction())
    {
        auto db = checkoutTransaction();
        return detail::getTransaction(*db, registry_.app(), id, range, ec);
    }

    return TxSearched::unknown;
}

SQLiteDatabase::SQLiteDatabase(SQLiteDatabase&& rhs) noexcept
    : registry_(rhs.registry_), useTxTables_(rhs.useTxTables_), j_(rhs.j_)
{
    std::exchange(ledgerDb_, std::move(rhs.ledgerDb_));
    std::exchange(txdb_, std::move(rhs.txdb_));
}

bool
SQLiteDatabase::ledgerDbHasSpace(Config const& config)
{
    if (existsLedger())
    {
        auto db = checkoutLedger();
        return detail::dbHasSpace(*db, config, j_);
    }

    return true;
}

bool
SQLiteDatabase::transactionDbHasSpace(Config const& config)
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
SQLiteDatabase::getKBUsedAll()
{
    if (existsLedger())
    {
        return xrpl::getKBUsedAll(ledgerDb_->getSession());
    }

    return 0;
}

std::uint32_t
SQLiteDatabase::getKBUsedLedger()
{
    if (existsLedger())
    {
        return xrpl::getKBUsedDB(ledgerDb_->getSession());
    }

    return 0;
}

std::uint32_t
SQLiteDatabase::getKBUsedTransaction()
{
    if (!useTxTables_)
        return 0;

    if (existsTransaction())
    {
        return xrpl::getKBUsedDB(txdb_->getSession());
    }

    return 0;
}

void
SQLiteDatabase::closeLedgerDB()
{
    ledgerDb_.reset();
}

void
SQLiteDatabase::closeTransactionDB()
{
    txdb_.reset();
}

SQLiteDatabase::SQLiteDatabase(ServiceRegistry& registry, Config const& config, JobQueue& jobQueue)
    : registry_(registry)
    , useTxTables_(config.useTxTables())
    , j_(registry.journal("SQLiteDatabase"))
{
    DatabaseCon::Setup const setup = setup_DatabaseCon(config, j_);
    if (!makeLedgerDBs(config, setup, DatabaseCon::CheckpointerSetup{&jobQueue, &registry_.logs()}))
    {
        std::string_view constexpr error = "Failed to create ledger databases";

        JLOG(j_.fatal()) << error;
        Throw<std::runtime_error>(error.data());
    }
}

SQLiteDatabase
setup_RelationalDatabase(ServiceRegistry& registry, Config const& config, JobQueue& jobQueue)
{
    return {registry, config, jobQueue};
}

}  // namespace xrpl
