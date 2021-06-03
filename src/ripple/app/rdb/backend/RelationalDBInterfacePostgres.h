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

#ifndef RIPPLE_CORE_RELATIONALDBINTERFACEPOSTGRES_H_INCLUDED
#define RIPPLE_CORE_RELATIONALDBINTERFACEPOSTGRES_H_INCLUDED

#include <ripple/app/rdb/RelationalDBInterface.h>

namespace ripple {

class RelationalDBInterfacePostgres : public RelationalDBInterface
{
public:
    /** There is only one implementation of this interface:
     * RelationalDBInterfacePostgresImp. It wraps a stoppable object (PgPool)
     * that does not follow RAII, and it does not go through the effort of
     * following RAII either. The owner of the only object of that type
     * (ApplicationImp) holds it by the type of its interface instead of its
     * implementation, and thus the lifetime management methods need to be
     * part of the interface.
     */
    virtual void
    stop() = 0;

    /**
     * @brief sweep Sweep the database. Method is specific for postgres backend.
     */
    virtual void
    sweep() = 0;

    /**
     * @brief getCompleteLedgers Returns string which contains list of
     *        completed ledgers. Method is specific for postgres backend.
     * @return String with completed ledger numbers
     */
    virtual std::string
    getCompleteLedgers() = 0;

    /**
     * @brief getValidatedLedgerAge Returns age of last
     *        validated ledger. Method is specific for postgres backend.
     * @return Age of last validated ledger in seconds
     */
    virtual std::chrono::seconds
    getValidatedLedgerAge() = 0;

    /**
     * @brief writeLedgerAndTransactions Write new ledger and transaction data
     *        into database. Method is specific for Postgres backend.
     * @param info Ledger info to write.
     * @param accountTxData Transaction data to write
     * @return True if success, false if failure.
     */
    virtual bool
    writeLedgerAndTransactions(
        LedgerInfo const& info,
        std::vector<AccountTransactionsData> const& accountTxData) = 0;

    /**
     * @brief getTxHashes Returns vector of tx hashes by given ledger
     *        sequence. Method is specific to postgres backend.
     * @param seq Ledger sequence
     * @return Vector of tx hashes
     */
    virtual std::vector<uint256>
    getTxHashes(LedgerIndex seq) = 0;

    /**
     * @brief getAccountTx Get last account transactions specifies by
     *        passed argumenrs structure. Function if specific to postgres
     *        backend.
     * @param args Arguments which specify account and whose tx to return.
     * @param app Application
     * @param j Journal
     * @return Vector of account transactions and RPC status of responce.
     */
    virtual std::pair<AccountTxResult, RPC::Status>
    getAccountTx(AccountTxArgs const& args) = 0;

    /**
     * @brief locateTransaction Returns information used to locate
     *        a transaction. Function is specific to postgres backend.
     * @param id Hash of the transaction.
     * @return Information used to locate a transaction. Contains a nodestore
     *         hash and ledger sequence pair if the transaction was found.
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
     * @return     false if the most recently written
     *             ledger has a close time over 3 minutes ago, or if there are
     *             no ledgers in the database. true otherwise
     */
    virtual bool
    isCaughtUp(std::string& reason) = 0;
};

}  // namespace ripple

#endif
