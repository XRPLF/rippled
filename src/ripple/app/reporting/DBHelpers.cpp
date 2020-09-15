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

#ifdef RIPPLED_REPORTING
#include <ripple/app/reporting/DBHelpers.h>
#include <memory>

namespace ripple {

static bool
writeToLedgersDB(LedgerInfo const& info, PgQuery& pgQuery, beast::Journal& j)
{
    JLOG(j.debug()) << __func__;
    auto cmd = boost::format(
        R"(INSERT INTO ledgers
           VALUES (%u,'\x%s', '\x%s',%u,%u,%u,%u,%u,'\x%s','\x%s'))");

    auto ledgerInsert = boost::str(
        cmd % info.seq % strHex(info.hash) % strHex(info.parentHash) %
        info.drops.drops() % info.closeTime.time_since_epoch().count() %
        info.parentCloseTime.time_since_epoch().count() %
        info.closeTimeResolution.count() % info.closeFlags %
        strHex(info.accountHash) % strHex(info.txHash));
    JLOG(j.trace()) << __func__ << " : "
                    << " : "
                    << "query string = " << ledgerInsert;

    auto res = pgQuery(ledgerInsert.data());

    return res;
}

bool
writeToPostgres(
    LedgerInfo const& info,
    std::vector<AccountTransactionsData> const& accountTxData,
    std::shared_ptr<PgPool> const& pgPool,
    beast::Journal& j)
{
    JLOG(j.debug()) << __func__ << " : "
                    << "Beginning write to Postgres";

    try
    {
        // Create a PgQuery object to run multiple commands over the same
        // connection in a single transaction block.
        PgQuery pg(pgPool);
        auto res = pg("BEGIN");
        if (!res || res.status() != PGRES_COMMAND_OK)
        {
            std::stringstream msg;
            msg << "bulkWriteToTable : Postgres insert error: " << res.msg();
            Throw<std::runtime_error>(msg.str());
        }

        // Writing to the ledgers db fails if the ledger already exists in the
        // db. In this situation, the ETL process has detected there is another
        // writer, and falls back to only publishing
        if (!writeToLedgersDB(info, pg, j))
        {
            JLOG(j.warn()) << __func__ << " : "
                           << "Failed to write to ledgers database.";
            return false;
        }

        std::stringstream transactionsCopyBuffer;
        std::stringstream accountTransactionsCopyBuffer;
        for (auto const& data : accountTxData)
        {
            std::string txHash = strHex(data.txHash);
            std::string nodestoreHash = strHex(data.nodestoreHash);
            auto idx = data.transactionIndex;
            auto ledgerSeq = data.ledgerSequence;

            transactionsCopyBuffer << std::to_string(ledgerSeq) << '\t'
                                   << std::to_string(idx) << '\t' << "\\\\x"
                                   << txHash << '\t' << "\\\\x" << nodestoreHash
                                   << '\n';

            for (auto const& a : data.accounts)
            {
                std::string acct = strHex(a);
                accountTransactionsCopyBuffer
                    << "\\\\x" << acct << '\t' << std::to_string(ledgerSeq)
                    << '\t' << std::to_string(idx) << '\n';
            }
        }

        pg.bulkInsert("transactions", transactionsCopyBuffer.str());
        pg.bulkInsert(
            "account_transactions", accountTransactionsCopyBuffer.str());

        res = pg("COMMIT");
        if (!res || res.status() != PGRES_COMMAND_OK)
        {
            std::stringstream msg;
            msg << "bulkWriteToTable : Postgres insert error: " << res.msg();
            assert(false);
            Throw<std::runtime_error>(msg.str());
        }

        JLOG(j.info()) << __func__ << " : "
                       << "Successfully wrote to Postgres";
        return true;
    }
    catch (std::exception& e)
    {
        JLOG(j.error()) << __func__ << "Caught exception writing to Postgres : "
                        << e.what();
        assert(false);
        return false;
    }
}

}  // namespace ripple
#endif
