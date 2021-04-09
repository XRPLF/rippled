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

#ifndef RIPPLE_CORE_RELATIONALDBINTERFACE_NODES_H_INCLUDED
#define RIPPLE_CORE_RELATIONALDBINTERFACE_NODES_H_INCLUDED

#include <ripple/app/ledger/Ledger.h>
#include <ripple/app/misc/Manifest.h>
#include <ripple/app/rdb/RelationalDBInterface.h>
#include <ripple/core/Config.h>
#include <ripple/overlay/PeerReservationTable.h>
#include <ripple/peerfinder/impl/Store.h>
#include <boost/filesystem.hpp>

namespace ripple {

/* Need to change TableTypeCount if TableType is modified. */
enum class TableType { Ledgers, Transactions, AccountTransactions };
constexpr int TableTypeCount = 3;

struct DatabasePairValid
{
    std::unique_ptr<DatabaseCon> ledgerDb;
    std::unique_ptr<DatabaseCon> transactionDb;
    bool valid;
};

/**
 * @brief makeLedgerDBs Opens ledger and transactions databases.
 * @param config Config object.
 * @param setup Path to database and opening parameters.
 * @param checkpointerSetup Database checkpointer setup.
 * @return Struct DatabasePairValid which contain unique pointers to ledger
 *         and transaction databases and flag if opening was successfull.
 */
DatabasePairValid
makeLedgerDBs(
    Config const& config,
    DatabaseCon::Setup const& setup,
    DatabaseCon::CheckpointerSetup const& checkpointerSetup);

/**
 * @brief getMinLedgerSeq Returns minimum ledger sequence in given table.
 * @param session Session with database.
 * @param type Table ID for which the result is returned.
 * @return Ledger sequence or none if no ledgers exist.
 */
std::optional<LedgerIndex>
getMinLedgerSeq(soci::session& session, TableType type);

/**
 * @brief getMaxLedgerSeq Returns maximum ledger sequence in given table.
 * @param session Session with database.
 * @param type Table ID for which the result is returned.
 * @return Ledger sequence or none if no ledgers exist.
 */
std::optional<LedgerIndex>
getMaxLedgerSeq(soci::session& session, TableType type);

/**
 * @brief deleteByLedgerSeq Deletes all entries in given table
 *        for the ledger with given sequence.
 * @param session Session with database.
 * @param type Table ID from which entries will be deleted.
 * @param ledgerSeq Ledger sequence.
 */
void
deleteByLedgerSeq(
    soci::session& session,
    TableType type,
    LedgerIndex ledgerSeq);

/**
 * @brief deleteBeforeLedgerSeq Deletes all entries in given table
 *        for the ledgers with given sequence and all sequences below it.
 * @param session Session with database.
 * @param type Table ID from which entries will be deleted.
 * @param ledgerSeq Ledger sequence.
 */
void
deleteBeforeLedgerSeq(
    soci::session& session,
    TableType type,
    LedgerIndex ledgerSeq);

/**
 * @brief getRows Returns number of rows in given table.
 * @param session Session with database.
 * @param type Table ID for which the result is returned.
 * @return Number of rows.
 */
std::size_t
getRows(soci::session& session, TableType type);

/**
 * @brief getRowsMinMax Returns minumum ledger sequence,
 *        maximum ledger sequence and total number of rows in given table.
 * @param session Session with database.
 * @param type Table ID for which the result is returned.
 * @return Struct CountMinMax which contain minimum sequence,
 *         maximum sequence and number of rows.
 */
RelationalDBInterface::CountMinMax
getRowsMinMax(soci::session& session, TableType type);

/**
 * @brief saveValidatedLedger Saves ledger into database.
 * @param lgrDB Link to ledgers database.
 * @param txnDB Link to transactions database.
 * @param app Application object.
 * @param ledger The ledger.
 * @param current True if ledger is current.
 * @return True is saving was successfull.
 */
bool
saveValidatedLedger(
    DatabaseCon& ldgDB,
    DatabaseCon& txnDB,
    Application& app,
    std::shared_ptr<Ledger const> const& ledger,
    bool current);

/**
 * @brief getLedgerInfoByIndex Returns ledger by its sequence.
 * @param session Session with database.
 * @param ledgerSeq Ledger sequence.
 * @param j Journal.
 * @return Ledger or none if ledger not found.
 */
std::optional<LedgerInfo>
getLedgerInfoByIndex(
    soci::session& session,
    LedgerIndex ledgerSeq,
    beast::Journal j);

/**
 * @brief getNewestLedgerInfo Returns info of newest saved ledger.
 * @param session Session with database.
 * @param j Journal.
 * @return Ledger info or none if ledger not found.
 */
std::optional<LedgerInfo>
getNewestLedgerInfo(soci::session& session, beast::Journal j);

/**
 * @brief getLimitedOldestLedgerInfo Returns info of oldest ledger
 *        from ledgers with sequences greather or equal to given.
 * @param session Session with database.
 * @param ledgerFirstIndex Minimum ledger sequence.
 * @param j Journal.
 * @return Ledger info or none if ledger not found.
 */
std::optional<LedgerInfo>
getLimitedOldestLedgerInfo(
    soci::session& session,
    LedgerIndex ledgerFirstIndex,
    beast::Journal j);

/**
 * @brief getLimitedNewestLedgerInfo Returns info of newest ledger
 *        from ledgers with sequences greather or equal to given.
 * @param session Session with database.
 * @param ledgerFirstIndex Minimum ledger sequence.
 * @param j Journal.
 * @return Ledger info or none if ledger not found.
 */
std::optional<LedgerInfo>
getLimitedNewestLedgerInfo(
    soci::session& session,
    LedgerIndex ledgerFirstIndex,
    beast::Journal j);

/**
 * @brief getLedgerInfoByHash Returns info of ledger with given hash.
 * @param session Session with database.
 * @param ledgerHash Hash of the ledger.
 * @param j Journal.
 * @return Ledger or none if ledger not found.
 */
std::optional<LedgerInfo>
getLedgerInfoByHash(
    soci::session& session,
    uint256 const& ledgerHash,
    beast::Journal j);

/**
 * @brief getHashByIndex Returns hash of ledger with given sequence.
 * @param session Session with database.
 * @param ledgerIndex Ledger sequence.
 * @return Hash of the ledger.
 */
uint256
getHashByIndex(soci::session& session, LedgerIndex ledgerIndex);

/**
 * @brief getHashesByIndex Returns hash of the ledger and hash of parent
 *        ledger for the ledger of given sequence.
 * @param session Session with database.
 * @param ledgerIndex Ledger sequence.
 * @param j Journal.
 * @return Struct LedgerHashPair which contain hashes of the ledger and
 *         its parent ledger.
 */
std::optional<LedgerHashPair>
getHashesByIndex(
    soci::session& session,
    LedgerIndex ledgerIndex,
    beast::Journal j);

/**
 * @brief getHashesByIndex Returns hash of the ledger and hash of parent
 *        ledger for all ledgers with seqyences from given minimum limit
 *        to fiven maximum limit.
 * @param session Session with database.
 * @param minSeq Minimum ledger sequence.
 * @param maxSeq Maximum ledger sequence.
 * @param j Journal.
 * @return Map which points sequence number of found ledger to the struct
 *         LedgerHashPair which contauns ledger hash and its parent hash.
 */
std::map<LedgerIndex, LedgerHashPair>
getHashesByIndex(
    soci::session& session,
    LedgerIndex minSeq,
    LedgerIndex maxSeq,
    beast::Journal j);

/**
 * @brief getTxHistory Returns given number of most recent transactions
 *        starting from given number of entry.
 * @param session Session with database.
 * @param app Application object.
 * @param startIndex Offset of first returned entry.
 * @param quantity Number of returned entries.
 * @param count True if counting of all transaction in that shard required.
 * @return Vector of shared pointers to transactions sorted in
 *         descending order by ledger sequence. Also number of transactions
 *         if count == true.
 */
std::pair<std::vector<std::shared_ptr<Transaction>>, int>
getTxHistory(
    soci::session& session,
    Application& app,
    LedgerIndex startIndex,
    int quantity,
    bool count);

/**
 * @brief getOldestAccountTxs Returns oldest transactions for given
 *        account which match given criteria starting from given offset.
 * @param session Session with database.
 * @param app Application object.
 * @param ledgerMaster LedgerMaster object.
 * @param options Struct AccountTxOptions which contain criteria to match:
 *        the account, minimum and maximum ledger numbers to search,
 *        offset of first entry to return, number of transactions to return,
 *        flag if this number unlimited.
 * @param limit_used Number or transactions already returned in calls
 *        to another shard databases, if shard databases are used.
 *        None if node database is used.
 * @param j Journal.
 * @return Vector of pairs of found transactions and their metadata
 *         sorted in ascending order by account sequence.
 *         Also number of transactions processed or skipped.
 *         If this number is >= 0, then it means number of transactions
 *         processed, if it is < 0, then -number means number of transactions
 *         skipped. We need to skip some quantity of transactions if option
 *         offset is > 0 in the options structure.
 */
std::pair<RelationalDBInterface::AccountTxs, int>
getOldestAccountTxs(
    soci::session& session,
    Application& app,
    LedgerMaster& ledgerMaster,
    RelationalDBInterface::AccountTxOptions const& options,
    std::optional<int> const& limit_used,
    beast::Journal j);

/**
 * @brief getNewestAccountTxs Returns newest transactions for given
 *        account which match given criteria starting from given offset.
 * @param session Session with database.
 * @param app Application object.
 * @param ledgerMaster LedgerMaster object.
 * @param options Struct AccountTxOptions which contain criteria to match:
 *        the account, minimum and maximum ledger numbers to search,
 *        offset of first entry to return, number of transactions to return,
 *        flag if this number unlimited.
 * @param limit_used Number or transactions already returned in calls
 *        to another shard databases, if shard databases are used.
 *        None if node database is used.
 * @param j Journal.
 * @return Vector of pairs of found transactions and their metadata
 *         sorted in descending order by account sequence.
 *         Also number of transactions processed or skipped.
 *         If this number is >= 0, then it means number of transactions
 *         processed, if it is < 0, then -number means number of transactions
 *         skipped. We need to skip some quantity of transactions if option
 *         offset is > 0 in the options structure.
 */
std::pair<RelationalDBInterface::AccountTxs, int>
getNewestAccountTxs(
    soci::session& session,
    Application& app,
    LedgerMaster& ledgerMaster,
    RelationalDBInterface::AccountTxOptions const& options,
    std::optional<int> const& limit_used,
    beast::Journal j);

/**
 * @brief getOldestAccountTxsB Returns oldest transactions in binary form
 *        for given account which match given criteria starting from given
 *        offset.
 * @param session Session with database.
 * @param app Application object.
 * @param options Struct AccountTxOptions which contain criteria to match:
 *        the account, minimum and maximum ledger numbers to search,
 *        offset of first entry to return, number of transactions to return,
 *        flag if this number unlimited.
 * @param limit_used Number or transactions already returned in calls
 *        to another shard databases, if shard databases are used.
 *        None if node database is used.
 * @param j Journal.
 * @return Vector of tuples of found transactions, their metadata and
 *         account sequences sorted in ascending order by account
 *         sequence. Also number of transactions processed or skipped.
 *         If this number is >= 0, then it means number of transactions
 *         processed, if it is < 0, then -number means number of transactions
 *         skipped. We need to skip some quantity of transactions if option
 *         offset is > 0 in the options structure.
 */
std::pair<std::vector<RelationalDBInterface::txnMetaLedgerType>, int>
getOldestAccountTxsB(
    soci::session& session,
    Application& app,
    RelationalDBInterface::AccountTxOptions const& options,
    std::optional<int> const& limit_used,
    beast::Journal j);

/**
 * @brief getNewestAccountTxsB Returns newest transactions in binary form
 *        for given account which match given criteria starting from given
 *        offset.
 * @param session Session with database.
 * @param app Application object.
 * @param options Struct AccountTxOptions which contain criteria to match:
 *        the account, minimum and maximum ledger numbers to search,
 *        offset of first entry to return, number of transactions to return,
 *        flag if this number unlimited.
 * @param limit_used Number or transactions already returned in calls
 *        to another shard databases, if shard databases are used.
 *        None if node database is used.
 * @param j Journal.
 * @return Vector of tuples of found transactions, their metadata and
 *         account sequences sorted in descending order by account
 *         sequence. Also number of transactions processed or skipped.
 *         If this number is >= 0, then it means number of transactions
 *         processed, if it is < 0, then -number means number of transactions
 *         skipped. We need to skip some quantity of transactions if option
 *         offset is > 0 in the options structure.
 */
std::pair<std::vector<RelationalDBInterface::txnMetaLedgerType>, int>
getNewestAccountTxsB(
    soci::session& session,
    Application& app,
    RelationalDBInterface::AccountTxOptions const& options,
    std::optional<int> const& limit_used,
    beast::Journal j);

/**
 * @brief oldestAccountTxPage Searches oldest transactions for given
 *        account which match given criteria starting from given marker
 *        and calls callback for each found transaction.
 * @param session Session with database.
 * @param idCache Account ID cache.
 * @param onUnsavedLedger Callback function to call on each found unsaved
 *        ledger within given range.
 * @param onTransaction Callback function to call on each found transaction.
 * @param options Struct AccountTxPageOptions which contain criteria to
 *        match: the account, minimum and maximum ledger numbers to search,
 *        marker of first returned entry, number of transactions to return,
 *        flag if this number unlimited.
 * @param limit_used Number or transactions already returned in calls
 *        to another shard databases.
 * @param page_length Total number of transactions to return.
 * @return Vector of tuples of found transactions, their metadata and
 *         account sequences sorted in ascending order by account
 *         sequence and marker for next search if search not finished.
 *         Also number of transactions processed during this call.
 */
std::pair<std::optional<RelationalDBInterface::AccountTxMarker>, int>
oldestAccountTxPage(
    soci::session& session,
    AccountIDCache const& idCache,
    std::function<void(std::uint32_t)> const& onUnsavedLedger,
    std::function<
        void(std::uint32_t, std::string const&, Blob&&, Blob&&)> const&
        onTransaction,
    RelationalDBInterface::AccountTxPageOptions const& options,
    int limit_used,
    std::uint32_t page_length);

/**
 * @brief newestAccountTxPage Searches newest transactions for given
 *        account which match given criteria starting from given marker
 *        and calls callback for each found transaction.
 * @param session Session with database.
 * @param idCache Account ID cache.
 * @param onUnsavedLedger Callback function to call on each found unsaved
 *        ledger within given range.
 * @param onTransaction Callback function to call on each found transaction.
 * @param options Struct AccountTxPageOptions which contain criteria to
 *        match: the account, minimum and maximum ledger numbers to search,
 *        marker of first returned entry, number of transactions to return,
 *        flag if this number unlimited.
 * @param limit_used Number or transactions already returned in calls
 *        to another shard databases.
 * @param page_length Total number of transactions to return.
 * @return Vector of tuples of found transactions, their metadata and
 *         account sequences sorted in descending order by account
 *         sequence and marker for next search if search not finished.
 *         Also number of transactions processed during this call.
 */
std::pair<std::optional<RelationalDBInterface::AccountTxMarker>, int>
newestAccountTxPage(
    soci::session& session,
    AccountIDCache const& idCache,
    std::function<void(std::uint32_t)> const& onUnsavedLedger,
    std::function<
        void(std::uint32_t, std::string const&, Blob&&, Blob&&)> const&
        onTransaction,
    RelationalDBInterface::AccountTxPageOptions const& options,
    int limit_used,
    std::uint32_t page_length);

/**
 * @brief getTransaction Returns transaction with given hash. If not found
 *        and range given then check if all ledgers from the range are
 *        present in the database.
 * @param session Session with database.
 * @param app Application object.
 * @param id Hash of the transaction.
 * @param range Range of ledgers to check, if present.
 * @param ec Default value of error code.
 * @return Transaction and its metadata if found, TxSearched::all if range
 *         given and all ledgers from range are present in the database,
 *         TxSearched::some if range given and not all ledgers are present,
 *         TxSearched::unknown if range not given or deserializing error
 *         occured. In the last case error code modified in ec link
 *         parameter, in other cases default error code remained.
 */
std::variant<RelationalDBInterface::AccountTx, TxSearched>
getTransaction(
    soci::session& session,
    Application& app,
    uint256 const& id,
    std::optional<ClosedInterval<uint32_t>> const& range,
    error_code_i& ec);

/**
 * @brief dbHasSpace Checks if given database has available space.
 * @param session Session with database.
 * @param config Config object.
 * @param j Journal.
 * @return True if space is available.
 */
bool
dbHasSpace(soci::session& session, Config const& config, beast::Journal j);

}  // namespace ripple

#endif
