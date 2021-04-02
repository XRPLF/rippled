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

#include <ripple/app/ledger/AcceptedLedger.h>
#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/ledger/LedgerToJson.h>
#include <ripple/app/ledger/TransactionMaster.h>
#include <ripple/app/main/Application.h>
#include <ripple/app/misc/Manifest.h>
#include <ripple/app/misc/impl/AccountTxPaging.h>
#include <ripple/app/rdb/RelationalDBInterface_nodes.h>
#include <ripple/app/rdb/RelationalDBInterface_postgres.h>
#include <ripple/app/rdb/backend/RelationalDBInterfacePostgres.h>
#include <ripple/basics/BasicConfig.h>
#include <ripple/basics/StringUtilities.h>
#include <ripple/core/DatabaseCon.h>
#include <ripple/core/Pg.h>
#include <ripple/core/SociDB.h>
#include <ripple/json/to_string.h>
#include <ripple/nodestore/DatabaseShard.h>
#include <boost/algorithm/string.hpp>
#include <boost/range/adaptor/transformed.hpp>
#include <soci/sqlite3/soci-sqlite3.h>

namespace ripple {

class RelationalDBInterfacePostgresImp : public RelationalDBInterfacePostgres
{
public:
    RelationalDBInterfacePostgresImp(
        Application& app,
        Config const& config,
        JobQueue& jobQueue)
        : app_(app)
        , j_(app_.journal("PgPool"))
        , pgPool_(
#ifdef RIPPLED_REPORTING
              make_PgPool(config.section("ledger_tx_tables"), j_)
#endif
          )
    {
        assert(config.reporting());
#ifdef RIPPLED_REPORTING
        if (config.reporting() && !config.reportingReadOnly())  // use pg
        {
            initSchema(pgPool_);
        }
#endif
    }

    void
    stop() override
    {
        pgPool_->stop();
    }

    void
    sweep() override;

    std::optional<LedgerIndex>
    getMinLedgerSeq() override;

    std::optional<LedgerIndex>
    getMaxLedgerSeq() override;

    std::string
    getCompleteLedgers() override;

    std::chrono::seconds
    getValidatedLedgerAge() override;

    bool
    writeLedgerAndTransactions(
        LedgerInfo const& info,
        std::vector<AccountTransactionsData> const& accountTxData) override;

    std::optional<LedgerInfo>
    getLedgerInfoByIndex(LedgerIndex ledgerSeq) override;

    std::optional<LedgerInfo>
    getNewestLedgerInfo() override;

    std::optional<LedgerInfo>
    getLedgerInfoByHash(uint256 const& ledgerHash) override;

    uint256
    getHashByIndex(LedgerIndex ledgerIndex) override;

    std::optional<LedgerHashPair>
    getHashesByIndex(LedgerIndex ledgerIndex) override;

    std::map<LedgerIndex, LedgerHashPair>
    getHashesByIndex(LedgerIndex minSeq, LedgerIndex maxSeq) override;

    std::vector<uint256>
    getTxHashes(LedgerIndex seq) override;

    std::vector<std::shared_ptr<Transaction>>
    getTxHistory(LedgerIndex startIndex) override;

    std::pair<AccountTxResult, RPC::Status>
    getAccountTx(AccountTxArgs const& args) override;

    Transaction::Locator
    locateTransaction(uint256 const& id) override;

    bool
    ledgerDbHasSpace(Config const& config) override;

    bool
    transactionDbHasSpace(Config const& config) override;

private:
    Application& app_;
    beast::Journal j_;
    std::shared_ptr<PgPool> pgPool_;

    bool
    dbHasSpace(Config const& config);
};

void
RelationalDBInterfacePostgresImp::sweep()
{
#ifdef RIPPLED_REPORTING
    pgPool_->idleSweeper();
#endif
}

std::optional<LedgerIndex>
RelationalDBInterfacePostgresImp::getMinLedgerSeq()
{
    return ripple::getMinLedgerSeq(pgPool_, j_);
}

std::optional<LedgerIndex>
RelationalDBInterfacePostgresImp::getMaxLedgerSeq()
{
    return ripple::getMaxLedgerSeq(pgPool_);
}

std::string
RelationalDBInterfacePostgresImp::getCompleteLedgers()
{
    return ripple::getCompleteLedgers(pgPool_);
}

std::chrono::seconds
RelationalDBInterfacePostgresImp::getValidatedLedgerAge()
{
    return ripple::getValidatedLedgerAge(pgPool_, j_);
}

bool
RelationalDBInterfacePostgresImp::writeLedgerAndTransactions(
    LedgerInfo const& info,
    std::vector<AccountTransactionsData> const& accountTxData)
{
    return ripple::writeLedgerAndTransactions(pgPool_, info, accountTxData, j_);
}

std::optional<LedgerInfo>
RelationalDBInterfacePostgresImp::getLedgerInfoByIndex(LedgerIndex ledgerSeq)
{
    return ripple::getLedgerInfoByIndex(pgPool_, ledgerSeq, app_);
}

std::optional<LedgerInfo>
RelationalDBInterfacePostgresImp::getNewestLedgerInfo()
{
    return ripple::getNewestLedgerInfo(pgPool_, app_);
}

std::optional<LedgerInfo>
RelationalDBInterfacePostgresImp::getLedgerInfoByHash(uint256 const& ledgerHash)
{
    return ripple::getLedgerInfoByHash(pgPool_, ledgerHash, app_);
}

uint256
RelationalDBInterfacePostgresImp::getHashByIndex(LedgerIndex ledgerIndex)
{
    return ripple::getHashByIndex(pgPool_, ledgerIndex, app_);
}

std::optional<LedgerHashPair>
RelationalDBInterfacePostgresImp::getHashesByIndex(LedgerIndex ledgerIndex)
{
    LedgerHashPair p;
    if (!ripple::getHashesByIndex(
            pgPool_, ledgerIndex, p.ledgerHash, p.parentHash, app_))
        return {};
    return p;
}

std::map<LedgerIndex, LedgerHashPair>
RelationalDBInterfacePostgresImp::getHashesByIndex(
    LedgerIndex minSeq,
    LedgerIndex maxSeq)
{
    return ripple::getHashesByIndex(pgPool_, minSeq, maxSeq, app_);
}

std::vector<uint256>
RelationalDBInterfacePostgresImp::getTxHashes(LedgerIndex seq)
{
    return ripple::getTxHashes(pgPool_, seq, app_);
}

std::vector<std::shared_ptr<Transaction>>
RelationalDBInterfacePostgresImp::getTxHistory(LedgerIndex startIndex)
{
    return ripple::getTxHistory(pgPool_, startIndex, app_, j_);
}

std::pair<AccountTxResult, RPC::Status>
RelationalDBInterfacePostgresImp::getAccountTx(AccountTxArgs const& args)
{
    return ripple::getAccountTx(pgPool_, args, app_, j_);
}

Transaction::Locator
RelationalDBInterfacePostgresImp::locateTransaction(uint256 const& id)
{
    return ripple::locateTransaction(pgPool_, id, app_);
}

bool
RelationalDBInterfacePostgresImp::dbHasSpace(Config const& config)
{
    /* Postgres server could be running on a different machine. */

    return true;
}

bool
RelationalDBInterfacePostgresImp::ledgerDbHasSpace(Config const& config)
{
    return dbHasSpace(config);
}

bool
RelationalDBInterfacePostgresImp::transactionDbHasSpace(Config const& config)
{
    return dbHasSpace(config);
}

std::unique_ptr<RelationalDBInterface>
getRelationalDBInterfacePostgres(
    Application& app,
    Config const& config,
    JobQueue& jobQueue)
{
    return std::make_unique<RelationalDBInterfacePostgresImp>(
        app, config, jobQueue);
}

}  // namespace ripple
