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

#ifndef RIPPLE_CORE_RELATIONALDBINTERFACESQLITE_H_INCLUDED
#define RIPPLE_CORE_RELATIONALDBINTERFACESQLITE_H_INCLUDED

#include <ripple/app/rdb/RelationalDBInterface.h>

namespace ripple {

class RelationalDBInterfaceSqlite : public RelationalDBInterface
{
public:
    /**
     * @brief getTransactionsMinLedgerSeq Returns minimum ledger sequence
     *        among records in the Transactions table.
     * @return Ledger sequence or none if no ledgers exist.
     */
    virtual std::optional<LedgerIndex>
    getTransactionsMinLedgerSeq() = 0;

    /**
     * @brief getAccountTransactionsMinLedgerSeq Returns minimum ledger
     *        sequence among records in the AccountTransactions table.
     * @return Ledger sequence or none if no ledgers exist.
     */
    virtual std::optional<LedgerIndex>
    getAccountTransactionsMinLedgerSeq() = 0;

    /**
     * @brief deleteTransactionByLedgerSeq Deletes transactions from ledger
     *        with given sequence.
     * @param ledgerSeq Ledger sequence.
     */
    virtual void
    deleteTransactionByLedgerSeq(LedgerIndex ledgerSeq) = 0;

    /**
     * @brief deleteBeforeLedgerSeq Deletes all ledgers with given sequence
     *        and all sequences below it.
     * @param ledgerSeq Ledger sequence.
     */
    virtual void
    deleteBeforeLedgerSeq(LedgerIndex ledgerSeq) = 0;

    /**
     * @brief deleteTransactionsBeforeLedgerSeq Deletes all transactions with
     *        given ledger sequence and all sequences below it.
     * @param ledgerSeq Ledger sequence.
     */
    virtual void
    deleteTransactionsBeforeLedgerSeq(LedgerIndex ledgerSeq) = 0;

    /**
     * @brief deleteAccountTransactionsBeforeLedgerSeq Deletes all account
     *        transactions with given ledger sequence and all sequences
     *        below it.
     * @param ledgerSeq Ledger sequence.
     */
    virtual void
    deleteAccountTransactionsBeforeLedgerSeq(LedgerIndex ledgerSeq) = 0;

    /**
     * @brief getTransactionCount Returns number of transactions.
     * @return Number of transactions.
     */
    virtual std::size_t
    getTransactionCount() = 0;

    /**
     * @brief getAccountTransactionCount Returns number of account
     *        transactions.
     * @return Number of account transactions.
     */
    virtual std::size_t
    getAccountTransactionCount() = 0;

    /**
     * @brief getLedgerCountMinMax Returns minumum ledger sequence,
     *        maximum ledger sequence and total number of saved ledgers.
     * @return Struct CountMinMax which contain minimum sequence,
     *         maximum sequence and number of ledgers.
     */
    virtual struct CountMinMax
    getLedgerCountMinMax() = 0;

    /**
     * @brief saveValidatedLedger Saves ledger into database.
     * @param ledger The ledger.
     * @param current True if ledger is current.
     * @return True is saving was successfull.
     */
    virtual bool
    saveValidatedLedger(
        std::shared_ptr<Ledger const> const& ledger,
        bool current) = 0;

    /**
     * @brief getLimitedOldestLedgerInfo Returns info of oldest ledger
     *        from ledgers with sequences greater or equal to given.
     * @param ledgerFirstIndex Minimum ledger sequence.
     * @return Ledger info or none if ledger not found.
     */
    virtual std::optional<LedgerInfo>
    getLimitedOldestLedgerInfo(LedgerIndex ledgerFirstIndex) = 0;

    /**
     * @brief getLimitedNewestLedgerInfo Returns info of newest ledger
     *        from ledgers with sequences greater or equal to given.
     * @param ledgerFirstIndex Minimum ledger sequence.
     * @return Ledger info or none if ledger not found.
     */
    virtual std::optional<LedgerInfo>
    getLimitedNewestLedgerInfo(LedgerIndex ledgerFirstIndex) = 0;

    /**
     * @brief getOldestAccountTxs Returns oldest transactions for given
     *        account which match given criteria starting from given offset.
     * @param options Struct AccountTxOptions which contain criteria to match:
     *        the account, minimum and maximum ledger numbers to search,
     *        offset of first entry to return, number of transactions to return,
     *        flag if this number unlimited.
     * @return Vector of pairs of found transactions and their metadata
     *         sorted in ascending order by account sequence.
     */
    virtual AccountTxs
    getOldestAccountTxs(AccountTxOptions const& options) = 0;

    /**
     * @brief getNewestAccountTxs Returns newest transactions for given
     *        account which match given criteria starting from given offset.
     * @param options Struct AccountTxOptions which contain criteria to match:
     *        the account, minimum and maximum ledger numbers to search,
     *        offset of first entry to return, number of transactions to return,
     *        flag if this number unlimited.
     * @return Vector of pairs of found transactions and their metadata
     *         sorted in descending order by account sequence.
     */
    virtual AccountTxs
    getNewestAccountTxs(AccountTxOptions const& options) = 0;

    /**
     * @brief getOldestAccountTxsB Returns oldest transactions in binary form
     *        for given account which match given criteria starting from given
     *        offset.
     * @param options Struct AccountTxOptions which contain criteria to match:
     *        the account, minimum and maximum ledger numbers to search,
     *        offset of first entry to return, number of transactions to return,
     *        flag if this number unlimited.
     * @return Vector of tuples of found transactions, their metadata and
     *         account sequences sorted in ascending order by account sequence.
     */
    virtual MetaTxsList
    getOldestAccountTxsB(AccountTxOptions const& options) = 0;

    /**
     * @brief getNewestAccountTxsB Returns newest transactions in binary form
     *        for given account which match given criteria starting from given
     *        offset.
     * @param options Struct AccountTxOptions which contain criteria to match:
     *        the account, minimum and maximum ledger numbers to search,
     *        offset of first entry to return, number of transactions to return,
     *        flag if this number unlimited.
     * @return Vector of tuples of found transactions, their metadata and
     *         account sequences sorted in descending order by account
     *         sequence.
     */
    virtual MetaTxsList
    getNewestAccountTxsB(AccountTxOptions const& options) = 0;

    /**
     * @brief oldestAccountTxPage Returns oldest transactions for given
     *        account which match given criteria starting from given marker.
     * @param options Struct AccountTxPageOptions which contain criteria to
     *        match: the account, minimum and maximum ledger numbers to search,
     *        marker of first returned entry, number of transactions to return,
     *        flag if this number unlimited.
     * @return Vector of pairs of found transactions and their metadata
     *         sorted in ascending order by account sequence and marker
     *         for next search if search not finished.
     */
    virtual std::pair<AccountTxs, std::optional<AccountTxMarker>>
    oldestAccountTxPage(AccountTxPageOptions const& options) = 0;

    /**
     * @brief newestAccountTxPage Returns newest transactions for given
     *        account which match given criteria starting from given marker.
     * @param options Struct AccountTxPageOptions which contain criteria to
     *        match: the account, minimum and maximum ledger numbers to search,
     *        marker of first returned entry, number of transactions to return,
     *        flag if this number unlimited.
     * @return Vector of pairs of found transactions and their metadata
     *         sorted in descending order by account sequence and marker
     *         for next search if search not finished.
     */
    virtual std::pair<AccountTxs, std::optional<AccountTxMarker>>
    newestAccountTxPage(AccountTxPageOptions const& options) = 0;

    /**
     * @brief oldestAccountTxPageB Returns oldest transactions in binary form
     *        for given account which match given criteria starting from given
     *        marker.
     * @param options Struct AccountTxPageOptions which contain criteria to
     *        match: the account, minimum and maximum ledger numbers to search,
     *        marker of first returned entry, number of transactions to return,
     *        flag if this number unlimited.
     * @return Vector of tuples of found transactions, their metadata and
     *         account sequences sorted in ascending order by account
     *         sequence and marker for next search if search not finished.
     */
    virtual std::pair<MetaTxsList, std::optional<AccountTxMarker>>
    oldestAccountTxPageB(AccountTxPageOptions const& options) = 0;

    /**
     * @brief newestAccountTxPageB Returns newest transactions in binary form
     *        for given account which match given criteria starting from given
     *        marker.
     * @param options Struct AccountTxPageOptions which contain criteria to
     *        match: the account, minimum and maximum ledger numbers to search,
     *        marker of first returned entry, number of transactions to return,
     *        flag if this number unlimited.
     * @return Vector of tuples of found transactions, their metadata and
     *         account sequences sorted in descending order by account
     *         sequence and marker for next search if search not finished.
     */
    virtual std::pair<MetaTxsList, std::optional<AccountTxMarker>>
    newestAccountTxPageB(AccountTxPageOptions const& options) = 0;

    /**
     * @brief getTransaction Returns transaction with given hash. If not found
     *        and range given then check if all ledgers from the range are
     *        present in the database.
     * @param id Hash of the transaction.
     * @param range Range of ledgers to check, if present.
     * @param ec Default value of error code.
     * @return Transaction and its metadata if found, TxSearched::all if range
     *         given and all ledgers from range are present in the database,
     *         TxSearched::some if range given and not all ledgers are present,
     *         TxSearched::unknown if range not given or deserializing error
     *         occured. In the last case error code returned via ec link
     *         parameter, in other cases default error code not changed.
     */
    virtual std::variant<AccountTx, TxSearched>
    getTransaction(
        uint256 const& id,
        std::optional<ClosedInterval<uint32_t>> const& range,
        error_code_i& ec) = 0;

    /**
     * @brief getKBUsedAll Returns space used by all databases.
     * @return Space in kilobytes.
     */
    virtual uint32_t
    getKBUsedAll() = 0;

    /**
     * @brief getKBUsedLedger Returns space used by ledger database.
     * @return Space in kilobytes.
     */
    virtual uint32_t
    getKBUsedLedger() = 0;

    /**
     * @brief getKBUsedTransaction Returns space used by transaction
     *        database.
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
