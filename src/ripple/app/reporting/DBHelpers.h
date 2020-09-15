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

#ifndef RIPPLE_APP_REPORTING_DBHELPERS_H_INCLUDED
#define RIPPLE_APP_REPORTING_DBHELPERS_H_INCLUDED

#include <ripple/app/reporting/ReportingETL.h>
#include <ripple/basics/Log.h>
#include <ripple/core/Pg.h>
#include <boost/container/flat_set.hpp>

namespace ripple {

/// Struct used to keep track of what to write to transactions and
/// account_transactions tables in Postgres
struct AccountTransactionsData
{
    boost::container::flat_set<AccountID> accounts;
    uint32_t ledgerSequence;
    uint32_t transactionIndex;
    uint256 txHash;
    uint256 nodestoreHash;

    AccountTransactionsData(
        TxMeta& meta,
        uint256&& nodestoreHash,
        beast::Journal& j)
        : accounts(meta.getAffectedAccounts(j))
        , ledgerSequence(meta.getLgrSeq())
        , transactionIndex(meta.getIndex())
        , txHash(meta.getTxID())
        , nodestoreHash(std::move(nodestoreHash))
    {
    }
};

#ifdef RIPPLED_REPORTING
/// Write new ledger and transaction data to Postgres
/// @param info Ledger Info to write
/// @param accountTxData transaction data to write
/// @param pgPool pool of Postgres connections
/// @param j journal (for logging)
/// @return whether the write succeeded
bool
writeToPostgres(
    LedgerInfo const& info,
    std::vector<AccountTransactionsData> const& accountTxData,
    std::shared_ptr<PgPool> const& pgPool,
    beast::Journal& j);

#endif
}  // namespace ripple
#endif
