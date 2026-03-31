#pragma once

#include <xrpl/rdb/RelationalDatabase.h>

#include <map>
#include <memory>
#include <optional>
#include <variant>
#include <vector>

namespace xrpl {

class Config;
class JobQueue;
class ServiceRegistry;

class SQLiteDatabase final : public RelationalDatabase
{
public:
    /**
     * @brief getMinLedgerSeq Returns the minimum ledger sequence in the Ledgers
     *        table.
     * @return Ledger sequence or no value if no ledgers exist.
     */
    std::optional<LedgerIndex>
    getMinLedgerSeq() override;

    /**
     * @brief getMaxLedgerSeq Returns the maximum ledger sequence in the Ledgers
     *        table.
     * @return Ledger sequence or none if no ledgers exist.
     */
    std::optional<LedgerIndex>
    getMaxLedgerSeq() override;

    /**
     * @brief getLedgerInfoByIndex Returns a ledger by its sequence.
     * @param ledgerSeq Ledger sequence.
     * @return The ledger if found, otherwise no value.
     */
    std::optional<LedgerHeader>
    getLedgerInfoByIndex(LedgerIndex ledgerSeq) override;

    /**
     * @brief getNewestLedgerInfo Returns the info of the newest saved ledger.
     * @return Ledger info if found, otherwise no value.
     */
    std::optional<LedgerHeader>
    getNewestLedgerInfo() override;

    /**
     * @brief getLedgerInfoByHash Returns the info of the ledger with given
     *        hash.
     * @param ledgerHash Hash of the ledger.
     * @return Ledger if found, otherwise no value.
     */
    std::optional<LedgerHeader>
    getLedgerInfoByHash(uint256 const& ledgerHash) override;

    /**
     * @brief getHashByIndex Returns the hash of the ledger with the given
     *        sequence.
     * @param ledgerIndex Ledger sequence.
     * @return Hash of the ledger.
     */
    uint256
    getHashByIndex(LedgerIndex ledgerIndex) override;

    /**
     * @brief getHashesByIndex Returns the hashes of the ledger and its parent
     *        as specified by the ledgerIndex.
     * @param ledgerIndex Ledger sequence.
     * @return Struct LedgerHashPair which contains hashes of the ledger and
     *         its parent.
     */
    std::optional<LedgerHashPair>
    getHashesByIndex(LedgerIndex ledgerIndex) override;

    /**
     * @brief getHashesByIndex Returns hashes of each ledger and its parent for
     *        all ledgers within the provided range.
     * @param minSeq Minimum ledger sequence.
     * @param maxSeq Maximum ledger sequence.
     * @return Container that maps the sequence number of a found ledger to the
     *         struct LedgerHashPair which contains the hashes of the ledger and
     *         its parent.
     */
    std::map<LedgerIndex, LedgerHashPair>
    getHashesByIndex(LedgerIndex minSeq, LedgerIndex maxSeq) override;

    /**
     * @brief getTxHistory Returns the 20 most recent transactions starting from
     *        the given number.
     * @param startIndex First number of returned entry.
     * @return Vector of shared pointers to transactions sorted in
     *         descending order by ledger sequence.
     */
    std::vector<std::shared_ptr<Transaction>>
    getTxHistory(LedgerIndex startIndex) override;

    /**
     * @brief getTransactionsMinLedgerSeq Returns the minimum ledger sequence
     *        stored in the Transactions table.
     * @return Ledger sequence or no value if no ledgers exist.
     */
    std::optional<LedgerIndex>
    getTransactionsMinLedgerSeq() override;

    /**
     * @brief getAccountTransactionsMinLedgerSeq Returns the minimum ledger
     *        sequence stored in the AccountTransactions table.
     * @return Ledger sequence or no value if no ledgers exist.
     */
    std::optional<LedgerIndex>
    getAccountTransactionsMinLedgerSeq() override;

    /**
     * @brief deleteTransactionByLedgerSeq Deletes transactions from the ledger
     *        with the given sequence.
     * @param ledgerSeq Ledger sequence.
     */
    void
    deleteTransactionByLedgerSeq(LedgerIndex ledgerSeq) override;

    /**
     * @brief deleteBeforeLedgerSeq Deletes all ledgers with a sequence number
     *        less than or equal to the given ledger sequence.
     * @param ledgerSeq Ledger sequence.
     */
    void
    deleteBeforeLedgerSeq(LedgerIndex ledgerSeq) override;

    /**
     * @brief deleteTransactionsBeforeLedgerSeq Deletes all transactions with
     *        a sequence number less than or equal to the given ledger
     *        sequence.
     * @param ledgerSeq Ledger sequence.
     */
    void
    deleteTransactionsBeforeLedgerSeq(LedgerIndex ledgerSeq) override;

    /**
     * @brief deleteAccountTransactionsBeforeLedgerSeq Deletes all account
     *        transactions with a sequence number less than or equal to the
     *        given ledger sequence.
     * @param ledgerSeq Ledger sequence.
     */
    void
    deleteAccountTransactionsBeforeLedgerSeq(LedgerIndex ledgerSeq) override;

    /**
     * @brief getTransactionCount Returns the number of transactions.
     * @return Number of transactions.
     */
    std::size_t
    getTransactionCount() override;

    /**
     * @brief getAccountTransactionCount Returns the number of account
     *        transactions.
     * @return Number of account transactions.
     */
    std::size_t
    getAccountTransactionCount() override;

    /**
     * @brief getLedgerCountMinMax Returns the minimum ledger sequence,
     *        maximum ledger sequence and total number of saved ledgers.
     * @return Struct CountMinMax which contains the minimum sequence,
     *         maximum sequence and number of ledgers.
     */
    CountMinMax
    getLedgerCountMinMax() override;

    /**
     * @brief saveValidatedLedger Saves a ledger into the database.
     * @param ledger The ledger.
     * @param current True if the ledger is current.
     * @return True if saving was successful.
     */
    bool
    saveValidatedLedger(std::shared_ptr<Ledger const> const& ledger, bool current) override;

    /**
     * @brief getLimitedOldestLedgerInfo Returns the info of the oldest ledger
     *        whose sequence number is greater than or equal to the given
     *        sequence number.
     * @param ledgerFirstIndex Minimum ledger sequence.
     * @return Ledger info if found, otherwise no value.
     */
    std::optional<LedgerHeader>
    getLimitedOldestLedgerInfo(LedgerIndex ledgerFirstIndex) override;

    /**
     * @brief getLimitedNewestLedgerInfo Returns the info of the newest ledger
     *        whose sequence number is greater than or equal to the given
     *        sequence number.
     * @param ledgerFirstIndex Minimum ledger sequence.
     * @return Ledger info if found, otherwise no value.
     */
    std::optional<LedgerHeader>
    getLimitedNewestLedgerInfo(LedgerIndex ledgerFirstIndex) override;

    /**
     * @brief getOldestAccountTxs Returns the oldest transactions for the
     *        account that matches the given criteria starting from the provided
     *        offset.
     * @param options Struct AccountTxOptions which contains the criteria to
     *        match: the account, ledger search range, the offset of the first
     *        entry to return, the number of transactions to return, a flag if
     *        this number is unlimited.
     * @return Vector of pairs of found transactions and their metadata
     *         sorted in ascending order by account sequence.
     */
    AccountTxs
    getOldestAccountTxs(AccountTxOptions const& options) override;

    /**
     * @brief getNewestAccountTxs Returns the newest transactions for the
     *        account that matches the given criteria starting from the provided
     *        offset.
     * @param options Struct AccountTxOptions which contains the criteria to
     *        match: the account, the ledger search range, the offset of  the
     *        first entry to return, the number of transactions to return, a
     *        flag if this number unlimited.
     * @return Vector of pairs of found transactions and their metadata
     *         sorted in descending order by account sequence.
     */
    AccountTxs
    getNewestAccountTxs(AccountTxOptions const& options) override;

    /**
     * @brief getOldestAccountTxsB Returns the oldest transactions in binary
     *        form for the account that matches the given criteria starting from
     *        the provided offset.
     * @param options Struct AccountTxOptions which contains the criteria to
     *        match: the account, the ledger search range, the offset of the
     *        first entry to return, the number of transactions to return, a
     *        flag if this number unlimited.
     * @return Vector of tuples of found transactions, their metadata and
     *         account sequences sorted in ascending order by account sequence.
     */
    MetaTxsList
    getOldestAccountTxsB(AccountTxOptions const& options) override;

    /**
     * @brief getNewestAccountTxsB Returns the newest transactions in binary
     *        form for the account that matches the given criteria starting from
     *        the provided offset.
     * @param options Struct AccountTxOptions which contains the criteria to
     *        match: the account, the ledger search range, the offset of the
     *        first entry to return, the number of transactions to return, a
     *        flag if this number is unlimited.
     * @return Vector of tuples of found transactions, their metadata and
     *         account sequences sorted in descending order by account
     *         sequence.
     */
    MetaTxsList
    getNewestAccountTxsB(AccountTxOptions const& options) override;

    /**
     * @brief oldestAccountTxPage Returns the oldest transactions for the
     *        account that matches the given criteria starting from the
     *        provided marker.
     * @param options Struct AccountTxPageOptions which contains the criteria to
     *        match: the account, the ledger search range, the marker of first
     *        returned entry, the number of transactions to return, a flag if
     *        this number is unlimited.
     * @return Vector of pairs of found transactions and their metadata
     *         sorted in ascending order by account sequence and a marker
     *         for the next search if the search was not finished.
     */
    std::pair<AccountTxs, std::optional<AccountTxMarker>>
    oldestAccountTxPage(AccountTxPageOptions const& options) override;

    /**
     * @brief newestAccountTxPage Returns the newest transactions for the
     *        account that matches the given criteria starting from the provided
     *        marker.
     * @param options Struct AccountTxPageOptions which contains the criteria to
     *        match: the account, the ledger search range, the marker of the
     *        first returned entry, the number of transactions to return, a flag
     *        if this number unlimited.
     * @return Vector of pairs of found transactions and their metadata
     *         sorted in descending order by account sequence and a marker
     *         for the next search if the search was not finished.
     */
    std::pair<AccountTxs, std::optional<AccountTxMarker>>
    newestAccountTxPage(AccountTxPageOptions const& options) override;

    /**
     * @brief oldestAccountTxPageB Returns the oldest transactions in binary
     *        form for the account that matches the given criteria starting from
     *        the provided marker.
     * @param options Struct AccountTxPageOptions which contains criteria to
     *        match: the account, the ledger search range, the marker of the
     *        first returned entry, the number of transactions to return, a flag
     *        if this number unlimited.
     * @return Vector of tuples of found transactions, their metadata and
     *         account sequences sorted in ascending order by account
     *         sequence and a marker for the next search if the search was not
     *         finished.
     */
    std::pair<MetaTxsList, std::optional<AccountTxMarker>>
    oldestAccountTxPageB(AccountTxPageOptions const& options) override;

    /**
     * @brief newestAccountTxPageB Returns the newest transactions in binary
     *        form for the account that matches the given criteria starting from
     *        the provided marker.
     * @param options Struct AccountTxPageOptions which contains the criteria to
     *        match: the account, the ledger search range, the marker of the
     *        first returned entry, the number of transactions to return, a flag
     *        if this number is unlimited.
     * @return Vector of tuples of found transactions, their metadata and
     *         account sequences sorted in descending order by account
     *         sequence and a marker for the next search if the search was not
     *         finished.
     */
    std::pair<MetaTxsList, std::optional<AccountTxMarker>>
    newestAccountTxPageB(AccountTxPageOptions const& options) override;

    /**
     * @brief getTransaction Returns the transaction with the given hash. If a
     *        range is provided but the transaction is not found, then check if
     *        all ledgers in the range are present in the database.
     * @param id Hash of the transaction.
     * @param range Range of ledgers to check, if present.
     * @param ec Default error code value.
     * @return Transaction and its metadata if found, otherwise TxSearched::All
     *         if a range is provided and all ledgers from the range are present
     *         in the database, TxSearched::Some if a range is provided and not
     *         all ledgers are present, TxSearched::Unknown if the range is not
     *         provided or a deserializing error occurred. In the last case the
     *         error code is returned via the ec parameter, in other cases the
     *         default error code is not changed.
     */
    std::variant<AccountTx, TxSearched>
    getTransaction(
        uint256 const& id,
        std::optional<ClosedInterval<std::uint32_t>> const& range,
        error_code_i& ec) override;

    /**
     * @brief getKBUsedAll Returns the amount of space used by all databases.
     * @return Space in kilobytes.
     */
    std::uint32_t
    getKBUsedAll() override;

    /**
     * @brief getKBUsedLedger Returns the amount of space space used by the
     *        ledger database.
     * @return Space in kilobytes.
     */
    std::uint32_t
    getKBUsedLedger() override;

    /**
     * @brief getKBUsedTransaction Returns the amount of space used by the
     *        transaction database.
     * @return Space in kilobytes.
     */
    std::uint32_t
    getKBUsedTransaction() override;

    /**
     * @brief Closes the ledger database
     */
    void
    closeLedgerDB() override;

    /**
     * @brief Closes the transaction database
     */
    void
    closeTransactionDB() override;

    SQLiteDatabase(ServiceRegistry& registry, Config const& config, JobQueue& jobQueue);

    SQLiteDatabase(SQLiteDatabase const&) = delete;
    SQLiteDatabase(SQLiteDatabase&& rhs) noexcept;

    SQLiteDatabase&
    operator=(SQLiteDatabase const&) = delete;
    SQLiteDatabase&
    operator=(SQLiteDatabase&&) = delete;

    /**
     * @brief ledgerDbHasSpace Checks if the ledger database has available
     *        space.
     * @param config Config object.
     * @return True if space is available.
     */
    bool
    ledgerDbHasSpace(Config const& config);

    /**
     * @brief transactionDbHasSpace Checks if the transaction database has
     *        available space.
     * @param config Config object.
     * @return True if space is available.
     */
    bool
    transactionDbHasSpace(Config const& config);

private:
    std::reference_wrapper<ServiceRegistry> registry_;
    bool useTxTables_;
    beast::Journal j_;
    std::unique_ptr<DatabaseCon> ledgerDb_, txdb_;

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
        return static_cast<bool>(ledgerDb_);
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
        return ledgerDb_->checkoutDb();
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

/**
 * @brief setup_RelationalDatabase Creates and returns a SQLiteDatabase
 *        instance based on configuration. It's recommended to use it as
 *        a singleton, but it's not enforced (e.g. if you have more than one
 *        database).
 * @param registry The service registry.
 * @param config Config object.
 * @param jobQueue JobQueue object.
 * @return SQLiteDatabase instance.
 */
SQLiteDatabase
setup_RelationalDatabase(ServiceRegistry& registry, Config const& config, JobQueue& jobQueue);

}  // namespace xrpl
