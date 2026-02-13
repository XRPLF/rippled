#pragma once

#include <xrpl/basics/RangeSet.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/LedgerHeader.h>
#include <xrpl/protocol/LedgerShortcut.h>
#include <xrpl/protocol/TxMeta.h>
#include <xrpl/protocol/TxSearched.h>
#include <xrpl/rdb/DatabaseCon.h>

#include <boost/filesystem.hpp>
#include <boost/variant.hpp>

namespace xrpl {

class Transaction;
class Ledger;

struct LedgerHashPair
{
    uint256 ledgerHash;
    uint256 parentHash;
};

struct LedgerRange
{
    uint32_t min;
    uint32_t max;
};

class RelationalDatabase
{
public:
    struct CountMinMax
    {
        std::size_t numberOfRows;
        LedgerIndex minLedgerSequence;
        LedgerIndex maxLedgerSequence;
    };

    struct AccountTxMarker
    {
        std::uint32_t ledgerSeq = 0;
        std::uint32_t txnSeq = 0;
    };

    struct AccountTxOptions
    {
        AccountID const& account;
        std::uint32_t minLedger;
        std::uint32_t maxLedger;
        std::uint32_t offset;
        std::uint32_t limit;
        bool bUnlimited;
    };

    struct AccountTxPageOptions
    {
        AccountID const& account;
        std::uint32_t minLedger;
        std::uint32_t maxLedger;
        std::optional<AccountTxMarker> marker;
        std::uint32_t limit;
        bool bAdmin;
    };

    using AccountTx = std::pair<std::shared_ptr<Transaction>, std::shared_ptr<TxMeta>>;
    using AccountTxs = std::vector<AccountTx>;
    using txnMetaLedgerType = std::tuple<Blob, Blob, std::uint32_t>;
    using MetaTxsList = std::vector<txnMetaLedgerType>;

    using LedgerSequence = uint32_t;
    using LedgerHash = uint256;
    using LedgerSpecifier = std::variant<LedgerRange, LedgerShortcut, LedgerSequence, LedgerHash>;

    struct AccountTxArgs
    {
        AccountID account;
        std::optional<LedgerSpecifier> ledger;
        bool binary = false;
        bool forward = false;
        uint32_t limit = 0;
        std::optional<AccountTxMarker> marker;
    };

    struct AccountTxResult
    {
        std::variant<AccountTxs, MetaTxsList> transactions;
        LedgerRange ledgerRange;
        uint32_t limit;
        std::optional<AccountTxMarker> marker;
    };

    virtual ~RelationalDatabase() = default;

    /**
     * @brief getMinLedgerSeq Returns the minimum ledger sequence in the Ledgers
     *        table.
     * @return Ledger sequence or no value if no ledgers exist.
     */
    virtual std::optional<LedgerIndex>
    getMinLedgerSeq() = 0;

    /**
     * @brief getMaxLedgerSeq Returns the maximum ledger sequence in the Ledgers
     *        table.
     * @return Ledger sequence or none if no ledgers exist.
     */
    virtual std::optional<LedgerIndex>
    getMaxLedgerSeq() = 0;

    /**
     * @brief getLedgerInfoByIndex Returns a ledger by its sequence.
     * @param ledgerSeq Ledger sequence.
     * @return The ledger if found, otherwise no value.
     */
    virtual std::optional<LedgerHeader>
    getLedgerInfoByIndex(LedgerIndex ledgerSeq) = 0;

    /**
     * @brief getNewestLedgerInfo Returns the info of the newest saved ledger.
     * @return Ledger info if found, otherwise no value.
     */
    virtual std::optional<LedgerHeader>
    getNewestLedgerInfo() = 0;

    /**
     * @brief getLedgerInfoByHash Returns the info of the ledger with given
     *        hash.
     * @param ledgerHash Hash of the ledger.
     * @return Ledger if found, otherwise no value.
     */
    virtual std::optional<LedgerHeader>
    getLedgerInfoByHash(uint256 const& ledgerHash) = 0;

    /**
     * @brief getHashByIndex Returns the hash of the ledger with the given
     *        sequence.
     * @param ledgerIndex Ledger sequence.
     * @return Hash of the ledger.
     */
    virtual uint256
    getHashByIndex(LedgerIndex ledgerIndex) = 0;

    /**
     * @brief getHashesByIndex Returns the hashes of the ledger and its parent
     *        as specified by the ledgerIndex.
     * @param ledgerIndex Ledger sequence.
     * @return Struct LedgerHashPair which contains hashes of the ledger and
     *         its parent.
     */
    virtual std::optional<LedgerHashPair>
    getHashesByIndex(LedgerIndex ledgerIndex) = 0;

    /**
     * @brief getHashesByIndex Returns hashes of each ledger and its parent for
     *        all ledgers within the provided range.
     * @param minSeq Minimum ledger sequence.
     * @param maxSeq Maximum ledger sequence.
     * @return Container that maps the sequence number of a found ledger to the
     *         struct LedgerHashPair which contains the hashes of the ledger and
     *         its parent.
     */
    virtual std::map<LedgerIndex, LedgerHashPair>
    getHashesByIndex(LedgerIndex minSeq, LedgerIndex maxSeq) = 0;

    /**
     * @brief getTxHistory Returns the 20 most recent transactions starting from
     *        the given number.
     * @param startIndex First number of returned entry.
     * @return Vector of shared pointers to transactions sorted in
     *         descending order by ledger sequence.
     */
    virtual std::vector<std::shared_ptr<Transaction>>
    getTxHistory(LedgerIndex startIndex) = 0;

    /**
     * @brief getTransactionsMinLedgerSeq Returns the minimum ledger sequence
     *        stored in the Transactions table.
     * @return Ledger sequence or no value if no ledgers exist.
     */
    virtual std::optional<LedgerIndex>
    getTransactionsMinLedgerSeq() = 0;

    /**
     * @brief getAccountTransactionsMinLedgerSeq Returns the minimum ledger
     *        sequence stored in the AccountTransactions table.
     * @return Ledger sequence or no value if no ledgers exist.
     */
    virtual std::optional<LedgerIndex>
    getAccountTransactionsMinLedgerSeq() = 0;

    /**
     * @brief deleteTransactionByLedgerSeq Deletes transactions from the ledger
     *        with the given sequence.
     * @param ledgerSeq Ledger sequence.
     */
    virtual void
    deleteTransactionByLedgerSeq(LedgerIndex ledgerSeq) = 0;

    /**
     * @brief deleteBeforeLedgerSeq Deletes all ledgers with a sequence number
     *        less than or equal to the given ledger sequence.
     * @param ledgerSeq Ledger sequence.
     */
    virtual void
    deleteBeforeLedgerSeq(LedgerIndex ledgerSeq) = 0;

    /**
     * @brief deleteTransactionsBeforeLedgerSeq Deletes all transactions with
     *        a sequence number less than or equal to the given ledger
     *        sequence.
     * @param ledgerSeq Ledger sequence.
     */
    virtual void
    deleteTransactionsBeforeLedgerSeq(LedgerIndex ledgerSeq) = 0;

    /**
     * @brief deleteAccountTransactionsBeforeLedgerSeq Deletes all account
     *        transactions with a sequence number less than or equal to the
     *        given ledger sequence.
     * @param ledgerSeq Ledger sequence.
     */
    virtual void
    deleteAccountTransactionsBeforeLedgerSeq(LedgerIndex ledgerSeq) = 0;

    /**
     * @brief getTransactionCount Returns the number of transactions.
     * @return Number of transactions.
     */
    virtual std::size_t
    getTransactionCount() = 0;

    /**
     * @brief getAccountTransactionCount Returns the number of account
     *        transactions.
     * @return Number of account transactions.
     */
    virtual std::size_t
    getAccountTransactionCount() = 0;

    /**
     * @brief getLedgerCountMinMax Returns the minimum ledger sequence,
     *        maximum ledger sequence and total number of saved ledgers.
     * @return Struct CountMinMax which contains the minimum sequence,
     *         maximum sequence and number of ledgers.
     */
    virtual struct CountMinMax
    getLedgerCountMinMax() = 0;

    /**
     * @brief saveValidatedLedger Saves a ledger into the database.
     * @param ledger The ledger.
     * @param current True if the ledger is current.
     * @return True if saving was successful.
     */
    virtual bool
    saveValidatedLedger(std::shared_ptr<Ledger const> const& ledger, bool current) = 0;

    /**
     * @brief getLimitedOldestLedgerInfo Returns the info of the oldest ledger
     *        whose sequence number is greater than or equal to the given
     *        sequence number.
     * @param ledgerFirstIndex Minimum ledger sequence.
     * @return Ledger info if found, otherwise no value.
     */
    virtual std::optional<LedgerHeader>
    getLimitedOldestLedgerInfo(LedgerIndex ledgerFirstIndex) = 0;

    /**
     * @brief getLimitedNewestLedgerInfo Returns the info of the newest ledger
     *        whose sequence number is greater than or equal to the given
     *        sequence number.
     * @param ledgerFirstIndex Minimum ledger sequence.
     * @return Ledger info if found, otherwise no value.
     */
    virtual std::optional<LedgerHeader>
    getLimitedNewestLedgerInfo(LedgerIndex ledgerFirstIndex) = 0;

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
    virtual AccountTxs
    getOldestAccountTxs(AccountTxOptions const& options) = 0;

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
    virtual AccountTxs
    getNewestAccountTxs(AccountTxOptions const& options) = 0;

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
    virtual MetaTxsList
    getOldestAccountTxsB(AccountTxOptions const& options) = 0;

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
    virtual MetaTxsList
    getNewestAccountTxsB(AccountTxOptions const& options) = 0;

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
    virtual std::pair<AccountTxs, std::optional<AccountTxMarker>>
    oldestAccountTxPage(AccountTxPageOptions const& options) = 0;

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
    virtual std::pair<AccountTxs, std::optional<AccountTxMarker>>
    newestAccountTxPage(AccountTxPageOptions const& options) = 0;

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
    virtual std::pair<MetaTxsList, std::optional<AccountTxMarker>>
    oldestAccountTxPageB(AccountTxPageOptions const& options) = 0;

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
    virtual std::pair<MetaTxsList, std::optional<AccountTxMarker>>
    newestAccountTxPageB(AccountTxPageOptions const& options) = 0;

    /**
     * @brief getTransaction Returns the transaction with the given hash. If a
     *        range is provided but the transaction is not found, then check if
     *        all ledgers in the range are present in the database.
     * @param id Hash of the transaction.
     * @param range Range of ledgers to check, if present.
     * @param ec Default error code value.
     * @return Transaction and its metadata if found, otherwise TxSearched::all
     *         if a range is provided and all ledgers from the range are present
     *         in the database, TxSearched::some if a range is provided and not
     *         all ledgers are present, TxSearched::unknown if the range is not
     *         provided or a deserializing error occurred. In the last case the
     *         error code is returned via the ec parameter, in other cases the
     *         default error code is not changed.
     */
    virtual std::variant<AccountTx, TxSearched>
    getTransaction(uint256 const& id, std::optional<ClosedInterval<uint32_t>> const& range, error_code_i& ec) = 0;

    /**
     * @brief getKBUsedAll Returns the amount of space used by all databases.
     * @return Space in kilobytes.
     */
    virtual uint32_t
    getKBUsedAll() = 0;

    /**
     * @brief getKBUsedLedger Returns the amount of space space used by the
     *        ledger database.
     * @return Space in kilobytes.
     */
    virtual uint32_t
    getKBUsedLedger() = 0;

    /**
     * @brief getKBUsedTransaction Returns the amount of space used by the
     *        transaction database.
     * @return Space in kilobytes.
     */
    virtual uint32_t
    getKBUsedTransaction() = 0;

    /**
     * @brief Closes the ledger database
     */
    virtual void
    closeLedgerDB() = 0;

    /**
     * @brief Closes the transaction database
     */
    virtual void
    closeTransactionDB() = 0;
};

template <class T, class C>
T
rangeCheckedCast(C c)
{
    if ((c > std::numeric_limits<T>::max()) || (!std::numeric_limits<T>::is_signed && c < 0) ||
        (std::numeric_limits<T>::is_signed && std::numeric_limits<C>::is_signed &&
         c < std::numeric_limits<T>::lowest()))
    {
        // This should never happen
        // LCOV_EXCL_START
        UNREACHABLE("xrpl::rangeCheckedCast : domain error");
        JLOG(debugLog().error()) << "rangeCheckedCast domain error:"
                                 << " value = " << c << " min = " << std::numeric_limits<T>::lowest()
                                 << " max: " << std::numeric_limits<T>::max();
        // LCOV_EXCL_STOP
    }

    return static_cast<T>(c);
}

}  // namespace xrpl
