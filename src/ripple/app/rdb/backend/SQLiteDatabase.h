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

#ifndef RIPPLE_APP_RDB_BACKEND_SQLITEDATABASE_H_INCLUDED
#define RIPPLE_APP_RDB_BACKEND_SQLITEDATABASE_H_INCLUDED

#include <ripple/app/rdb/RelationalDatabase.h>

namespace ripple {

class SQLiteDatabase : public RelationalDatabase
{
public:
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
    saveValidatedLedger(
        std::shared_ptr<Ledger const> const& ledger,
        bool current) = 0;

    /**
     * @brief getLimitedOldestLedgerInfo Returns the info of the oldest ledger
     *        whose sequence number is greater than or equal to the given
     *        sequence number.
     * @param ledgerFirstIndex Minimum ledger sequence.
     * @return Ledger info if found, otherwise no value.
     */
    virtual std::optional<LedgerInfo>
    getLimitedOldestLedgerInfo(LedgerIndex ledgerFirstIndex) = 0;

    /**
     * @brief getLimitedNewestLedgerInfo Returns the info of the newest ledger
     *        whose sequence number is greater than or equal to the given
     *        sequence number.
     * @param ledgerFirstIndex Minimum ledger sequence.
     * @return Ledger info if found, otherwise no value.
     */
    virtual std::optional<LedgerInfo>
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
    getTransaction(
        uint256 const& id,
        std::optional<ClosedInterval<uint32_t>> const& range,
        error_code_i& ec) = 0;

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

}  // namespace ripple

#endif
