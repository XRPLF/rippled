//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2021 Ripple Labs Inc.

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

#ifndef RIPPLE_CORE_RELATIONALDBINTERFACE_POSTGRES_H_INCLUDED
#define RIPPLE_CORE_RELATIONALDBINTERFACE_POSTGRES_H_INCLUDED

#include <ripple/app/ledger/Ledger.h>
#include <ripple/app/main/Application.h>
#include <ripple/app/misc/Transaction.h>
#include <ripple/app/rdb/RelationalDBInterface.h>
#include <ripple/core/Pg.h>
#include <variant>
#include <vector>

namespace ripple {

class PgPool;

using AccountTxMarker = RelationalDBInterface::AccountTxMarker;
using AccountTxArgs = RelationalDBInterface::AccountTxArgs;
using AccountTxResult = RelationalDBInterface::AccountTxResult;
using AccountTransactionsData = RelationalDBInterface::AccountTransactionsData;

/**
 * @brief getMinLedgerSeq Returns minimum ledger sequence
 *        from Postgres database
 * @param pgPool Link to postgres database
 * @param app Application
 * @param j Journal
 * @return Minimum ledger sequence if any, none if no ledgers
 */
std::optional<LedgerIndex>
getMinLedgerSeq(std::shared_ptr<PgPool> const& pgPool, beast::Journal j);

/**
 * @brief getMaxLedgerSeq Returns maximum ledger sequence
 *        from Postgres database
 * @param pgPool Link to postgres database
 * @param app Application
 * @return Maximum ledger sequence if any, none if no ledgers
 */
std::optional<LedgerIndex>
getMaxLedgerSeq(std::shared_ptr<PgPool> const& pgPool);

/**
 * @brief getCompleteLedgers Returns string which contains
 *        list of completed ledgers
 * @param pgPool Link to postgres database
 * @param app Application
 * @return String with completed ledgers
 */
std::string
getCompleteLedgers(std::shared_ptr<PgPool> const& pgPool);

/**
 * @brief getValidatedLedgerAge Returns age of last
 *        validated ledger
 * @param pgPool Link to postgres database
 * @param app Application
 * @param j Journal
 * @return Age of last validated ledger
 */
std::chrono::seconds
getValidatedLedgerAge(std::shared_ptr<PgPool> const& pgPool, beast::Journal j);

/**
 * @brief getNewestLedgerInfo Load latest ledger info from Postgres
 * @param pgPool Link to postgres database
 * @param app reference to Application
 * @return Ledger info
 */
std::optional<LedgerInfo>
getNewestLedgerInfo(std::shared_ptr<PgPool> const& pgPool, Application& app);

/**
 * @brief getLedgerInfoByIndex Load ledger info by index (AKA sequence)
 *        from Postgres
 * @param pgPool Link to postgres database
 * @param ledgerIndex the ledger index (or sequence) to load
 * @param app reference to Application
 * @return Ledger info
 */
std::optional<LedgerInfo>
getLedgerInfoByIndex(
    std::shared_ptr<PgPool> const& pgPool,
    std::uint32_t ledgerIndex,
    Application& app);

/**
 * @brief getLedgerInfoByHash Load ledger info by hash from Postgres
 * @param pgPool Link to postgres database
 * @param hash Hash of the ledger to load
 * @param app reference to Application
 * @return Ledger info
 */
std::optional<LedgerInfo>
getLedgerInfoByHash(
    std::shared_ptr<PgPool> const& pgPool,
    uint256 const& ledgerHash,
    Application& app);

/**
 * @brief getHashByIndex Given a ledger sequence,
 *        return the ledger hash
 * @param pgPool Link to postgres database
 * @param ledgerIndex Ledger sequence
 * @param app Application
 * @return Hash of ledger
 */
uint256
getHashByIndex(
    std::shared_ptr<PgPool> const& pgPool,
    std::uint32_t ledgerIndex,
    Application& app);

/**
 * @brief getHashesByIndex Given a ledger sequence,
 *        return the ledger hash and the parent hash
 * @param pgPool Link to postgres database
 * @param ledgerIndex Ledger sequence
 * @param[out] ledgerHash Hash of ledger
 * @param[out] parentHash Hash of parent ledger
 * @param app Application
 * @return True if the data was found
 */
bool
getHashesByIndex(
    std::shared_ptr<PgPool> const& pgPool,
    std::uint32_t ledgerIndex,
    uint256& ledgerHash,
    uint256& parentHash,
    Application& app);

/**
 * @brief getHashesByIndex Given a contiguous range of sequences,
 *        return a map of sequence -> (hash, parent hash)
 * @param pgPool Link to postgres database
 * @param minSeq Lower bound of range
 * @param maxSeq Upper bound of range
 * @param app Application
 * @return Mapping of all found ledger sequences to their hash and parent hash
 */
std::map<std::uint32_t, LedgerHashPair>
getHashesByIndex(
    std::shared_ptr<PgPool> const& pgPool,
    std::uint32_t minSeq,
    std::uint32_t maxSeq,
    Application& app);

/**
 * @brief getTxHashes Returns vector of tx hashes by given ledger
 *        sequence
 * @param pgPool Link to postgres database
 * @param seq Ledger sequence
 * @param app Application
 * @return Vector of tx hashes
 */
std::vector<uint256>
getTxHashes(
    std::shared_ptr<PgPool> const& pgPool,
    LedgerIndex seq,
    Application& app);

/**
 * @brief locateTransaction Returns information used to locate
 *        a transaction. Function is specific to postgres backend.
 * @param pgPool Link to postgres database
 * @param id Hash of the transaction.
 * @param app Application
 * @return Information used to locate a transaction. Contains a nodestore
 *         hash and ledger sequence pair if the transaction was found.
 *         Otherwise, contains the range of ledgers present in the database
 *         at the time of search.
 */
Transaction::Locator
locateTransaction(
    std::shared_ptr<PgPool> const& pgPool,
    uint256 const& id,
    Application& app);

/**
 * @brief getTxHistory Returns most recent 20 transactions starting
 *        from given number or entry.
 * @param pgPool Link to postgres database
 * @param startIndex First number of returned entry.
 * @param app Application
 * @param j Journal
 * @return Vector of sharded pointers to transactions sorted in
 *         descending order by ledger sequence.
 */
std::vector<std::shared_ptr<Transaction>>
getTxHistory(
    std::shared_ptr<PgPool> const& pgPool,
    LedgerIndex startIndex,
    Application& app,
    beast::Journal j);

/**
 * @brief getAccountTx Get last account transactions specifies by
 *        passed argumenrs structure.
 * @param pgPool Link to postgres database
 * @param args Arguments which specify account and whose tx to return.
 * @param app Application
 * @param j Journal
 * @return Vector of account transactions and RPC status of responce.
 */
std::pair<AccountTxResult, RPC::Status>
getAccountTx(
    std::shared_ptr<PgPool> const& pgPool,
    AccountTxArgs const& args,
    Application& app,
    beast::Journal j);

/**
 * @brief writeLedgerAndTransactions Write new ledger and transaction
 *        data to Postgres.
 * @param pgPool Pool of Postgres connections
 * @param info Ledger info to write.
 * @param accountTxData Transaction data to write
 * @param j Journal (for logging)
 * @return True if success, false if failure.
 */
bool
writeLedgerAndTransactions(
    std::shared_ptr<PgPool> const& pgPool,
    LedgerInfo const& info,
    std::vector<AccountTransactionsData> const& accountTxData,
    beast::Journal& j);

}  // namespace ripple

#endif
