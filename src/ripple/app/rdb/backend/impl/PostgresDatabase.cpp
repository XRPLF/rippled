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
#include <ripple/app/rdb/backend/PostgresDatabase.h>
#include <ripple/app/rdb/backend/detail/Node.h>
#include <ripple/basics/BasicConfig.h>
#include <ripple/basics/StringUtilities.h>
#include <ripple/core/DatabaseCon.h>
#include <ripple/core/Pg.h>
#include <ripple/core/SociDB.h>
#include <ripple/json/json_reader.h>
#include <ripple/json/to_string.h>
#include <ripple/nodestore/DatabaseShard.h>
#include <boost/algorithm/string.hpp>
#include <boost/range/adaptor/transformed.hpp>
#include <soci/sqlite3/soci-sqlite3.h>

namespace ripple {

class PgPool;

using AccountTxResult = RelationalDatabase::AccountTxResult;
using TxnsData = RelationalDatabase::AccountTxs;
using TxnsDataBinary = RelationalDatabase::MetaTxsList;

class PostgresDatabaseImp final : public PostgresDatabase
{
public:
    PostgresDatabaseImp(
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
#ifdef RIPPLED_REPORTING
        pgPool_->stop();
#endif
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

    bool
    isCaughtUp(std::string& reason) override;

private:
    Application& app_;
    beast::Journal j_;
    std::shared_ptr<PgPool> pgPool_;

    bool
    dbHasSpace(Config const& config);
};

/**
 * @brief loadLedgerInfos Loads the ledger info for the specified
 *        ledger/s from the database
 * @param pgPool Link to postgres database
 * @param whichLedger Specifies the ledger to load via ledger sequence,
 *        ledger hash, a range of ledgers, or std::monostate
 *        (which loads the most recent)
 * @param app Application
 * @return Vector of LedgerInfos
 */
static std::vector<LedgerInfo>
loadLedgerInfos(
    std::shared_ptr<PgPool> const& pgPool,
    std::variant<
        std::monostate,
        uint256,
        uint32_t,
        std::pair<uint32_t, uint32_t>> const& whichLedger,
    Application& app)
{
    std::vector<LedgerInfo> infos;
#ifdef RIPPLED_REPORTING
    auto log = app.journal("Ledger");
    assert(app.config().reporting());
    std::stringstream sql;
    sql << "SELECT ledger_hash, prev_hash, account_set_hash, trans_set_hash, "
           "total_coins, closing_time, prev_closing_time, close_time_res, "
           "close_flags, ledger_seq FROM ledgers ";

    uint32_t expNumResults = 1;

    if (auto ledgerSeq = std::get_if<uint32_t>(&whichLedger))
    {
        sql << "WHERE ledger_seq = " + std::to_string(*ledgerSeq);
    }
    else if (auto ledgerHash = std::get_if<uint256>(&whichLedger))
    {
        sql << ("WHERE ledger_hash = \'\\x" + strHex(*ledgerHash) + "\'");
    }
    else if (
        auto minAndMax =
            std::get_if<std::pair<uint32_t, uint32_t>>(&whichLedger))
    {
        expNumResults = minAndMax->second - minAndMax->first;

        sql
            << ("WHERE ledger_seq >= " + std::to_string(minAndMax->first) +
                " AND ledger_seq <= " + std::to_string(minAndMax->second));
    }
    else
    {
        sql << ("ORDER BY ledger_seq desc LIMIT 1");
    }
    sql << ";";

    JLOG(log.trace()) << __func__ << " : sql = " << sql.str();

    auto res = PgQuery(pgPool)(sql.str().data());
    if (!res)
    {
        JLOG(log.error()) << __func__ << " : Postgres response is null - sql = "
                          << sql.str();
        assert(false);
        return {};
    }
    else if (res.status() != PGRES_TUPLES_OK)
    {
        JLOG(log.error()) << __func__
                          << " : Postgres response should have been "
                             "PGRES_TUPLES_OK but instead was "
                          << res.status() << " - msg  = " << res.msg()
                          << " - sql = " << sql.str();
        assert(false);
        return {};
    }

    JLOG(log.trace()) << __func__ << " Postgres result msg  : " << res.msg();

    if (res.isNull() || res.ntuples() == 0)
    {
        JLOG(log.debug()) << __func__
                          << " : Ledger not found. sql = " << sql.str();
        return {};
    }
    else if (res.ntuples() > 0)
    {
        if (res.nfields() != 10)
        {
            JLOG(log.error()) << __func__
                              << " : Wrong number of fields in Postgres "
                                 "response. Expected 10, but got "
                              << res.nfields() << " . sql = " << sql.str();
            assert(false);
            return {};
        }
    }

    for (size_t i = 0; i < res.ntuples(); ++i)
    {
        char const* hash = res.c_str(i, 0);
        char const* prevHash = res.c_str(i, 1);
        char const* accountHash = res.c_str(i, 2);
        char const* txHash = res.c_str(i, 3);
        std::int64_t totalCoins = res.asBigInt(i, 4);
        std::int64_t closeTime = res.asBigInt(i, 5);
        std::int64_t parentCloseTime = res.asBigInt(i, 6);
        std::int64_t closeTimeRes = res.asBigInt(i, 7);
        std::int64_t closeFlags = res.asBigInt(i, 8);
        std::int64_t ledgerSeq = res.asBigInt(i, 9);

        JLOG(log.trace()) << __func__ << " - Postgres response = " << hash
                          << " , " << prevHash << " , " << accountHash << " , "
                          << txHash << " , " << totalCoins << ", " << closeTime
                          << ", " << parentCloseTime << ", " << closeTimeRes
                          << ", " << closeFlags << ", " << ledgerSeq
                          << " - sql = " << sql.str();
        JLOG(log.debug()) << __func__
                          << " - Successfully fetched ledger with sequence = "
                          << ledgerSeq << " from Postgres";

        using time_point = NetClock::time_point;
        using duration = NetClock::duration;

        LedgerInfo info;
        if (!info.parentHash.parseHex(prevHash + 2))
            assert(false);
        if (!info.txHash.parseHex(txHash + 2))
            assert(false);
        if (!info.accountHash.parseHex(accountHash + 2))
            assert(false);
        info.drops = totalCoins;
        info.closeTime = time_point{duration{closeTime}};
        info.parentCloseTime = time_point{duration{parentCloseTime}};
        info.closeFlags = closeFlags;
        info.closeTimeResolution = duration{closeTimeRes};
        info.seq = ledgerSeq;
        if (!info.hash.parseHex(hash + 2))
            assert(false);
        info.validated = true;
        infos.push_back(info);
    }

#endif
    return infos;
}

/**
 * @brief loadLedgerHelper Load a ledger info from Postgres
 * @param pgPool Link to postgres database
 * @param whichLedger Specifies sequence or hash of ledger. Passing
 *        std::monostate loads the most recent ledger
 * @param app The Application
 * @return Ledger info
 */
static std::optional<LedgerInfo>
loadLedgerHelper(
    std::shared_ptr<PgPool> const& pgPool,
    std::variant<std::monostate, uint256, uint32_t> const& whichLedger,
    Application& app)
{
    std::vector<LedgerInfo> infos;
    std::visit(
        [&infos, &app, &pgPool](auto&& arg) {
            infos = loadLedgerInfos(pgPool, arg, app);
        },
        whichLedger);
    assert(infos.size() <= 1);
    if (!infos.size())
        return {};
    return infos[0];
}

#ifdef RIPPLED_REPORTING
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

enum class DataFormat { binary, expanded };
static std::variant<TxnsData, TxnsDataBinary>
flatFetchTransactions(
    Application& app,
    std::vector<uint256>& nodestoreHashes,
    std::vector<uint32_t>& ledgerSequences,
    DataFormat format)
{
    std::variant<TxnsData, TxnsDataBinary> ret;
    if (format == DataFormat::binary)
        ret = TxnsDataBinary();
    else
        ret = TxnsData();

    std::vector<
        std::pair<std::shared_ptr<STTx const>, std::shared_ptr<STObject const>>>
        txns = flatFetchTransactions(app, nodestoreHashes);
    for (size_t i = 0; i < txns.size(); ++i)
    {
        auto& [txn, meta] = txns[i];
        if (format == DataFormat::binary)
        {
            auto& transactions = std::get<TxnsDataBinary>(ret);
            Serializer txnSer = txn->getSerializer();
            Serializer metaSer = meta->getSerializer();
            // SerialIter it(item->slice());
            Blob txnBlob = txnSer.getData();
            Blob metaBlob = metaSer.getData();
            transactions.push_back(
                std::make_tuple(txnBlob, metaBlob, ledgerSequences[i]));
        }
        else
        {
            auto& transactions = std::get<TxnsData>(ret);
            std::string reason;
            auto txnRet = std::make_shared<Transaction>(txn, reason, app);
            txnRet->setLedger(ledgerSequences[i]);
            txnRet->setStatus(COMMITTED);
            auto txMeta = std::make_shared<TxMeta>(
                txnRet->getID(), ledgerSequences[i], *meta);
            transactions.push_back(std::make_pair(txnRet, txMeta));
        }
    }
    return ret;
}

static std::pair<AccountTxResult, RPC::Status>
processAccountTxStoredProcedureResult(
    RelationalDatabase::AccountTxArgs const& args,
    Json::Value& result,
    Application& app,
    beast::Journal j)
{
    AccountTxResult ret;
    ret.limit = args.limit;

    try
    {
        if (result.isMember("transactions"))
        {
            std::vector<uint256> nodestoreHashes;
            std::vector<uint32_t> ledgerSequences;
            for (auto& t : result["transactions"])
            {
                if (t.isMember("ledger_seq") && t.isMember("nodestore_hash"))
                {
                    uint32_t ledgerSequence = t["ledger_seq"].asUInt();
                    std::string nodestoreHashHex =
                        t["nodestore_hash"].asString();
                    nodestoreHashHex.erase(0, 2);
                    uint256 nodestoreHash;
                    if (!nodestoreHash.parseHex(nodestoreHashHex))
                        assert(false);

                    if (nodestoreHash.isNonZero())
                    {
                        ledgerSequences.push_back(ledgerSequence);
                        nodestoreHashes.push_back(nodestoreHash);
                    }
                    else
                    {
                        assert(false);
                        return {ret, {rpcINTERNAL, "nodestoreHash is zero"}};
                    }
                }
                else
                {
                    assert(false);
                    return {ret, {rpcINTERNAL, "missing postgres fields"}};
                }
            }

            assert(nodestoreHashes.size() == ledgerSequences.size());
            ret.transactions = flatFetchTransactions(
                app,
                nodestoreHashes,
                ledgerSequences,
                args.binary ? DataFormat::binary : DataFormat::expanded);

            JLOG(j.trace()) << __func__ << " : processed db results";

            if (result.isMember("marker"))
            {
                auto& marker = result["marker"];
                assert(marker.isMember("ledger"));
                assert(marker.isMember("seq"));
                ret.marker = {
                    marker["ledger"].asUInt(), marker["seq"].asUInt()};
            }
            assert(result.isMember("ledger_index_min"));
            assert(result.isMember("ledger_index_max"));
            ret.ledgerRange = {
                result["ledger_index_min"].asUInt(),
                result["ledger_index_max"].asUInt()};
            return {ret, rpcSUCCESS};
        }
        else if (result.isMember("error"))
        {
            JLOG(j.debug())
                << __func__ << " : error = " << result["error"].asString();
            return {
                ret,
                RPC::Status{rpcINVALID_PARAMS, result["error"].asString()}};
        }
        else
        {
            return {ret, {rpcINTERNAL, "unexpected Postgres response"}};
        }
    }
    catch (std::exception& e)
    {
        JLOG(j.debug()) << __func__ << " : "
                        << "Caught exception : " << e.what();
        return {ret, {rpcINTERNAL, e.what()}};
    }
}
#endif

void
PostgresDatabaseImp::sweep()
{
#ifdef RIPPLED_REPORTING
    pgPool_->idleSweeper();
#endif
}

std::optional<LedgerIndex>
PostgresDatabaseImp::getMinLedgerSeq()
{
#ifdef RIPPLED_REPORTING
    auto seq = PgQuery(pgPool_)("SELECT min_ledger()");
    if (!seq)
    {
        JLOG(j_.error()) << "Error querying minimum ledger sequence.";
    }
    else if (!seq.isNull())
        return seq.asInt();
#endif
    return {};
}

std::optional<LedgerIndex>
PostgresDatabaseImp::getMaxLedgerSeq()
{
#ifdef RIPPLED_REPORTING
    auto seq = PgQuery(pgPool_)("SELECT max_ledger()");
    if (seq && !seq.isNull())
        return seq.asBigInt();
#endif
    return {};
}

std::string
PostgresDatabaseImp::getCompleteLedgers()
{
#ifdef RIPPLED_REPORTING
    auto range = PgQuery(pgPool_)("SELECT complete_ledgers()");
    if (range)
        return range.c_str();
#endif
    return "error";
}

std::chrono::seconds
PostgresDatabaseImp::getValidatedLedgerAge()
{
    using namespace std::chrono_literals;
#ifdef RIPPLED_REPORTING
    auto age = PgQuery(pgPool_)("SELECT age()");
    if (!age || age.isNull())
        JLOG(j_.debug()) << "No ledgers in database";
    else
        return std::chrono::seconds{age.asInt()};
#endif
    return weeks{2};
}

bool
PostgresDatabaseImp::writeLedgerAndTransactions(
    LedgerInfo const& info,
    std::vector<AccountTransactionsData> const& accountTxData)
{
#ifdef RIPPLED_REPORTING
    JLOG(j_.debug()) << __func__ << " : "
                     << "Beginning write to Postgres";

    try
    {
        // Create a PgQuery object to run multiple commands over the same
        // connection in a single transaction block.
        PgQuery pg(pgPool_);
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
        if (!writeToLedgersDB(info, pg, j_))
        {
            JLOG(j_.warn()) << __func__ << " : "
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

        JLOG(j_.info()) << __func__ << " : "
                        << "Successfully wrote to Postgres";
        return true;
    }
    catch (std::exception& e)
    {
        JLOG(j_.error()) << __func__
                         << "Caught exception writing to Postgres : "
                         << e.what();
        assert(false);
        return false;
    }
#else
    return false;
#endif
}

std::optional<LedgerInfo>
PostgresDatabaseImp::getLedgerInfoByIndex(LedgerIndex ledgerSeq)
{
    return loadLedgerHelper(pgPool_, ledgerSeq, app_);
}

std::optional<LedgerInfo>
PostgresDatabaseImp::getNewestLedgerInfo()
{
    return loadLedgerHelper(pgPool_, {}, app_);
}

std::optional<LedgerInfo>
PostgresDatabaseImp::getLedgerInfoByHash(uint256 const& ledgerHash)
{
    return loadLedgerHelper(pgPool_, ledgerHash, app_);
}

uint256
PostgresDatabaseImp::getHashByIndex(LedgerIndex ledgerIndex)
{
    auto infos = loadLedgerInfos(pgPool_, ledgerIndex, app_);
    assert(infos.size() <= 1);
    if (infos.size())
        return infos[0].hash;
    return {};
}

std::optional<LedgerHashPair>
PostgresDatabaseImp::getHashesByIndex(LedgerIndex ledgerIndex)
{
    LedgerHashPair p;
    auto infos = loadLedgerInfos(pgPool_, ledgerIndex, app_);
    assert(infos.size() <= 1);
    if (infos.size())
    {
        p.ledgerHash = infos[0].hash;
        p.parentHash = infos[0].parentHash;
        return p;
    }
    return {};
}

std::map<LedgerIndex, LedgerHashPair>
PostgresDatabaseImp::getHashesByIndex(LedgerIndex minSeq, LedgerIndex maxSeq)
{
    std::map<uint32_t, LedgerHashPair> ret;
    auto infos = loadLedgerInfos(pgPool_, std::make_pair(minSeq, maxSeq), app_);
    for (auto& info : infos)
    {
        ret[info.seq] = {info.hash, info.parentHash};
    }
    return ret;
}

std::vector<uint256>
PostgresDatabaseImp::getTxHashes(LedgerIndex seq)
{
    std::vector<uint256> nodestoreHashes;

#ifdef RIPPLED_REPORTING
    auto log = app_.journal("Ledger");

    std::string query =
        "SELECT nodestore_hash"
        "  FROM transactions "
        " WHERE ledger_seq = " +
        std::to_string(seq);
    auto res = PgQuery(pgPool_)(query.c_str());

    if (!res)
    {
        JLOG(log.error()) << __func__
                          << " : Postgres response is null - query = " << query;
        assert(false);
        return {};
    }
    else if (res.status() != PGRES_TUPLES_OK)
    {
        JLOG(log.error()) << __func__
                          << " : Postgres response should have been "
                             "PGRES_TUPLES_OK but instead was "
                          << res.status() << " - msg  = " << res.msg()
                          << " - query = " << query;
        assert(false);
        return {};
    }

    JLOG(log.trace()) << __func__ << " Postgres result msg  : " << res.msg();

    if (res.isNull() || res.ntuples() == 0)
    {
        JLOG(log.debug()) << __func__
                          << " : Ledger not found. query = " << query;
        return {};
    }
    else if (res.ntuples() > 0)
    {
        if (res.nfields() != 1)
        {
            JLOG(log.error()) << __func__
                              << " : Wrong number of fields in Postgres "
                                 "response. Expected 1, but got "
                              << res.nfields() << " . query = " << query;
            assert(false);
            return {};
        }
    }

    JLOG(log.trace()) << __func__ << " : result = " << res.c_str()
                      << " : query = " << query;
    for (size_t i = 0; i < res.ntuples(); ++i)
    {
        char const* nodestoreHash = res.c_str(i, 0);
        uint256 hash;
        if (!hash.parseHex(nodestoreHash + 2))
            assert(false);

        nodestoreHashes.push_back(hash);
    }
#endif

    return nodestoreHashes;
}

std::vector<std::shared_ptr<Transaction>>
PostgresDatabaseImp::getTxHistory(LedgerIndex startIndex)
{
    std::vector<std::shared_ptr<Transaction>> ret;

#ifdef RIPPLED_REPORTING
    if (!app_.config().reporting())
    {
        assert(false);
        Throw<std::runtime_error>(
            "called getTxHistory but not in reporting mode");
    }

    std::string sql = boost::str(
        boost::format("SELECT nodestore_hash, ledger_seq "
                      "  FROM transactions"
                      " ORDER BY ledger_seq DESC LIMIT 20 "
                      "OFFSET %u;") %
        startIndex);

    auto res = PgQuery(pgPool_)(sql.data());

    if (!res)
    {
        JLOG(j_.error()) << __func__
                         << " : Postgres response is null - sql = " << sql;
        assert(false);
        return {};
    }
    else if (res.status() != PGRES_TUPLES_OK)
    {
        JLOG(j_.error()) << __func__
                         << " : Postgres response should have been "
                            "PGRES_TUPLES_OK but instead was "
                         << res.status() << " - msg  = " << res.msg()
                         << " - sql = " << sql;
        assert(false);
        return {};
    }

    JLOG(j_.trace()) << __func__ << " Postgres result msg  : " << res.msg();

    if (res.isNull() || res.ntuples() == 0)
    {
        JLOG(j_.debug()) << __func__ << " : Empty postgres response";
        assert(false);
        return {};
    }
    else if (res.ntuples() > 0)
    {
        if (res.nfields() != 2)
        {
            JLOG(j_.error()) << __func__
                             << " : Wrong number of fields in Postgres "
                                "response. Expected 1, but got "
                             << res.nfields() << " . sql = " << sql;
            assert(false);
            return {};
        }
    }

    JLOG(j_.trace()) << __func__ << " : Postgres result = " << res.c_str();

    std::vector<uint256> nodestoreHashes;
    std::vector<uint32_t> ledgerSequences;
    for (size_t i = 0; i < res.ntuples(); ++i)
    {
        uint256 hash;
        if (!hash.parseHex(res.c_str(i, 0) + 2))
            assert(false);
        nodestoreHashes.push_back(hash);
        ledgerSequences.push_back(res.asBigInt(i, 1));
    }

    auto txns = flatFetchTransactions(app_, nodestoreHashes);
    for (size_t i = 0; i < txns.size(); ++i)
    {
        auto const& [sttx, meta] = txns[i];
        assert(sttx);

        std::string reason;
        auto txn = std::make_shared<Transaction>(sttx, reason, app_);
        txn->setLedger(ledgerSequences[i]);
        txn->setStatus(COMMITTED);
        ret.push_back(txn);
    }

#endif
    return ret;
}

std::pair<AccountTxResult, RPC::Status>
PostgresDatabaseImp::getAccountTx(AccountTxArgs const& args)
{
#ifdef RIPPLED_REPORTING
    pg_params dbParams;

    char const*& command = dbParams.first;
    std::vector<std::optional<std::string>>& values = dbParams.second;
    command =
        "SELECT account_tx($1::bytea, $2::bool, "
        "$3::bigint, $4::bigint, $5::bigint, $6::bytea, "
        "$7::bigint, $8::bool, $9::bigint, $10::bigint)";
    values.resize(10);
    values[0] = "\\x" + strHex(args.account);
    values[1] = args.forward ? "true" : "false";

    static std::uint32_t const page_length(200);
    if (args.limit == 0 || args.limit > page_length)
        values[2] = std::to_string(page_length);
    else
        values[2] = std::to_string(args.limit);

    if (args.ledger)
    {
        if (auto range = std::get_if<LedgerRange>(&args.ledger.value()))
        {
            values[3] = std::to_string(range->min);
            values[4] = std::to_string(range->max);
        }
        else if (auto hash = std::get_if<LedgerHash>(&args.ledger.value()))
        {
            values[5] = ("\\x" + strHex(*hash));
        }
        else if (
            auto sequence = std::get_if<LedgerSequence>(&args.ledger.value()))
        {
            values[6] = std::to_string(*sequence);
        }
        else if (std::get_if<LedgerShortcut>(&args.ledger.value()))
        {
            // current, closed and validated are all treated as validated
            values[7] = "true";
        }
        else
        {
            JLOG(j_.error()) << "doAccountTxStoredProcedure - "
                             << "Error parsing ledger args";
            return {};
        }
    }

    if (args.marker)
    {
        values[8] = std::to_string(args.marker->ledgerSeq);
        values[9] = std::to_string(args.marker->txnSeq);
    }
    for (size_t i = 0; i < values.size(); ++i)
    {
        JLOG(j_.trace()) << "value " << std::to_string(i) << " = "
                         << (values[i] ? values[i].value() : "null");
    }

    auto res = PgQuery(pgPool_)(dbParams);
    if (!res)
    {
        JLOG(j_.error()) << __func__
                         << " : Postgres response is null - account = "
                         << strHex(args.account);
        assert(false);
        return {{}, {rpcINTERNAL, "Postgres error"}};
    }
    else if (res.status() != PGRES_TUPLES_OK)
    {
        JLOG(j_.error()) << __func__
                         << " : Postgres response should have been "
                            "PGRES_TUPLES_OK but instead was "
                         << res.status() << " - msg  = " << res.msg()
                         << " - account = " << strHex(args.account);
        assert(false);
        return {{}, {rpcINTERNAL, "Postgres error"}};
    }

    JLOG(j_.trace()) << __func__ << " Postgres result msg  : " << res.msg();
    if (res.isNull() || res.ntuples() == 0)
    {
        JLOG(j_.debug()) << __func__
                         << " : No data returned from Postgres : account = "
                         << strHex(args.account);

        assert(false);
        return {{}, {rpcINTERNAL, "Postgres error"}};
    }

    char const* resultStr = res.c_str();
    JLOG(j_.trace()) << __func__ << " : "
                     << "postgres result = " << resultStr
                     << " : account = " << strHex(args.account);

    Json::Value v;
    Json::Reader reader;
    bool success = reader.parse(resultStr, resultStr + strlen(resultStr), v);
    if (success)
    {
        return processAccountTxStoredProcedureResult(args, v, app_, j_);
    }
#endif
    // This shouldn't happen. Postgres should return a parseable error
    assert(false);
    return {{}, {rpcINTERNAL, "Failed to deserialize Postgres result"}};
}

Transaction::Locator
PostgresDatabaseImp::locateTransaction(uint256 const& id)
{
#ifdef RIPPLED_REPORTING
    auto baseCmd = boost::format(R"(SELECT tx('%s');)");

    std::string txHash = "\\x" + strHex(id);
    std::string sql = boost::str(baseCmd % txHash);

    auto res = PgQuery(pgPool_)(sql.data());

    if (!res)
    {
        JLOG(app_.journal("Transaction").error())
            << __func__
            << " : Postgres response is null - tx ID = " << strHex(id);
        assert(false);
        return {};
    }
    else if (res.status() != PGRES_TUPLES_OK)
    {
        JLOG(app_.journal("Transaction").error())
            << __func__
            << " : Postgres response should have been "
               "PGRES_TUPLES_OK but instead was "
            << res.status() << " - msg  = " << res.msg()
            << " - tx ID = " << strHex(id);
        assert(false);
        return {};
    }

    JLOG(app_.journal("Transaction").trace())
        << __func__ << " Postgres result msg  : " << res.msg();
    if (res.isNull() || res.ntuples() == 0)
    {
        JLOG(app_.journal("Transaction").debug())
            << __func__
            << " : No data returned from Postgres : tx ID = " << strHex(id);
        // This shouldn't happen
        assert(false);
        return {};
    }

    char const* resultStr = res.c_str();
    JLOG(app_.journal("Transaction").debug())
        << "postgres result = " << resultStr;

    Json::Value v;
    Json::Reader reader;
    bool success = reader.parse(resultStr, resultStr + strlen(resultStr), v);
    if (success)
    {
        if (v.isMember("nodestore_hash") && v.isMember("ledger_seq"))
        {
            uint256 nodestoreHash;
            if (!nodestoreHash.parseHex(
                    v["nodestore_hash"].asString().substr(2)))
                assert(false);
            uint32_t ledgerSeq = v["ledger_seq"].asUInt();
            if (nodestoreHash.isNonZero())
                return {std::make_pair(nodestoreHash, ledgerSeq)};
        }
        if (v.isMember("min_seq") && v.isMember("max_seq"))
        {
            return {ClosedInterval<uint32_t>(
                v["min_seq"].asUInt(), v["max_seq"].asUInt())};
        }
    }
#endif
    // Shouldn' happen. Postgres should return the ledger range searched if
    // the transaction was not found
    assert(false);
    Throw<std::runtime_error>(
        "Transaction::Locate - Invalid Postgres response");
    return {};
}

bool
PostgresDatabaseImp::dbHasSpace(Config const& config)
{
    /* Postgres server could be running on a different machine. */

    return true;
}

bool
PostgresDatabaseImp::ledgerDbHasSpace(Config const& config)
{
    return dbHasSpace(config);
}

bool
PostgresDatabaseImp::transactionDbHasSpace(Config const& config)
{
    return dbHasSpace(config);
}

std::unique_ptr<RelationalDatabase>
getPostgresDatabase(Application& app, Config const& config, JobQueue& jobQueue)
{
    return std::make_unique<PostgresDatabaseImp>(app, config, jobQueue);
}

bool
PostgresDatabaseImp::isCaughtUp(std::string& reason)
{
#ifdef RIPPLED_REPORTING
    using namespace std::chrono_literals;
    auto age = PgQuery(pgPool_)("SELECT age()");
    if (!age || age.isNull())
    {
        reason = "No ledgers in database";
        return false;
    }
    if (std::chrono::seconds{age.asInt()} > 3min)
    {
        reason = "No recently-published ledger";
        return false;
    }
#endif
    return true;
}

}  // namespace ripple
