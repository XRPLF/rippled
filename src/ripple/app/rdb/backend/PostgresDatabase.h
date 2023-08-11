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

#ifndef RIPPLE_APP_RDB_BACKEND_POSTGRESDATABASE_H_INCLUDED
#define RIPPLE_APP_RDB_BACKEND_POSTGRESDATABASE_H_INCLUDED

#include <ripple/app/rdb/RelationalDatabase.h>

namespace ripple {

class PostgresDatabase : public RelationalDatabase
{
public:
    virtual void
    stop() = 0;

    /**
     * @brief sweep Sweeps the database.
     */
    virtual void
    sweep() = 0;

    /**
     * @brief getCompleteLedgers Returns a string which contains a list of
     *        completed ledgers.
     * @return String with completed ledger sequences
     */
    virtual std::string
    getCompleteLedgers() = 0;

    /**
     * @brief getValidatedLedgerAge Returns the age of the last validated
     *        ledger.
     * @return Age of the last validated ledger in seconds
     */
    virtual std::chrono::seconds
    getValidatedLedgerAge() = 0;

    /**
     * @brief writeLedgerAndTransactions Writes new ledger and transaction data
     *        into the database.
     * @param info Ledger info to write.
     * @param accountTxData Transaction data to write
     * @return True on success, false on failure.
     */
    virtual bool
    writeLedgerAndTransactions(
        LedgerInfo const& info,
        std::vector<AccountTransactionsData> const& accountTxData) = 0;

    /**
     * @brief getTxHashes Returns a vector of the hashes of transactions
     *        belonging to the ledger with the provided sequence.
     * @param seq Ledger sequence
     * @return Vector of transaction hashes
     */
    virtual std::vector<uint256>
    getTxHashes(LedgerIndex seq) = 0;

    /**
     * @brief getAccountTx Get the last account transactions specified by the
     *        AccountTxArgs struct.
     * @param args Arguments which specify the account and which transactions to
     *        return.
     * @return Vector of account transactions and the RPC status response.
     */
    virtual std::pair<AccountTxResult, RPC::Status>
    getAccountTx(AccountTxArgs const& args) = 0;

    /**
     * @brief locateTransaction Returns information used to locate
     *        a transaction.
     * @param id Hash of the transaction.
     * @return Information used to locate a transaction. Contains a nodestore
     *         hash and a ledger sequence pair if the transaction was found.
     *         Otherwise, contains the range of ledgers present in the database
     *         at the time of search.
     */
    virtual Transaction::Locator
    locateTransaction(uint256 const& id) = 0;

    /**
     * @brief      isCaughtUp returns whether the database is caught up with the
     *             network
     * @param[out] reason if the database is not caught up, reason contains a
     *             helpful message describing why
     * @return     false if the most recently written ledger has a close time
     *             over 3 minutes ago, or if there are no ledgers in the
     *             database. true otherwise
     */
    virtual bool
    isCaughtUp(std::string& reason) = 0;
};

}  // namespace ripple

#endif
