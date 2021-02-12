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
#include <ripple/app/rdb/RelationalDBInterface.h>
#include <ripple/app/rdb/RelationalDBInterface_shards.h>
#include <ripple/basics/BasicConfig.h>
#include <ripple/basics/StringUtilities.h>
#include <ripple/core/DatabaseCon.h>
#include <ripple/core/SociDB.h>
#include <ripple/json/to_string.h>
#include <boost/algorithm/string.hpp>
#include <boost/range/adaptor/transformed.hpp>

namespace ripple {

DatabasePair
makeMetaDBs(
    Config const& config,
    DatabaseCon::Setup const& setup,
    DatabaseCon::CheckpointerSetup const& checkpointerSetup)
{
    // ledger meta database
    auto lgrMetaDB{std::make_unique<DatabaseCon>(
        setup,
        LgrMetaDBName,
        LgrMetaDBPragma,
        LgrMetaDBInit,
        checkpointerSetup)};

    if (config.useTxTables())
    {
        // transaction meta database
        auto txMetaDB{std::make_unique<DatabaseCon>(
            setup,
            TxMetaDBName,
            TxMetaDBPragma,
            TxMetaDBInit,
            checkpointerSetup)};

        return {std::move(lgrMetaDB), std::move(txMetaDB)};
    }

    return {std::move(lgrMetaDB), nullptr};
}

bool
saveLedgerMeta(
    std::shared_ptr<Ledger const> const& ledger,
    Application& app,
    soci::session& lgrMetaSession,
    soci::session& txnMetaSession,
    std::uint32_t const shardIndex)
{
    std::string_view constexpr lgrSQL =
        R"sql(INSERT OR REPLACE INTO LedgerMeta VALUES
              (:ledgerHash,:shardIndex);)sql";

    auto const hash = to_string(ledger->info().hash);
    lgrMetaSession << lgrSQL, soci::use(hash), soci::use(shardIndex);

    if (app.config().useTxTables())
    {
        AcceptedLedger::pointer const aLedger = [&app, ledger] {
            try
            {
                auto aLedger =
                    app.getAcceptedLedgerCache().fetch(ledger->info().hash);
                if (!aLedger)
                {
                    aLedger = std::make_shared<AcceptedLedger>(ledger, app);
                    app.getAcceptedLedgerCache().canonicalize_replace_client(
                        ledger->info().hash, aLedger);
                }

                return aLedger;
            }
            catch (std::exception const&)
            {
                JLOG(app.journal("Ledger").warn())
                    << "An accepted ledger was missing nodes";
            }

            return AcceptedLedger::pointer{nullptr};
        }();

        if (!aLedger)
            return false;

        soci::transaction tr(txnMetaSession);

        for (auto const& [_, acceptedLedgerTx] : aLedger->getMap())
        {
            (void)_;

            std::string_view constexpr txnSQL =
                R"sql(INSERT OR REPLACE INTO TransactionMeta VALUES
                      (:transactionID,:shardIndex);)sql";

            auto const transactionID =
                to_string(acceptedLedgerTx->getTransactionID());

            txnMetaSession << txnSQL, soci::use(transactionID),
                soci::use(shardIndex);
        }

        tr.commit();
    }

    return true;
}

std::optional<std::uint32_t>
getShardIndexforLedger(soci::session& session, LedgerHash const& hash)
{
    std::uint32_t shardIndex;
    session << "SELECT ShardIndex FROM LedgerMeta WHERE LedgerHash = '" << hash
            << "';",
        soci::into(shardIndex);

    if (!session.got_data())
        return std::nullopt;

    return shardIndex;
}

std::optional<std::uint32_t>
getShardIndexforTransaction(soci::session& session, TxID const& id)
{
    std::uint32_t shardIndex;
    session << "SELECT ShardIndex FROM TransactionMeta WHERE TransID = '" << id
            << "';",
        soci::into(shardIndex);

    if (!session.got_data())
        return std::nullopt;

    return shardIndex;
}

DatabasePair
makeShardCompleteLedgerDBs(
    Config const& config,
    DatabaseCon::Setup const& setup)
{
    auto tx{std::make_unique<DatabaseCon>(
        setup, TxDBName, FinalShardDBPragma, TxDBInit)};
    tx->getSession() << boost::str(
        boost::format("PRAGMA cache_size=-%d;") %
        kilobytes(config.getValueFor(SizedItem::txnDBCache, std::nullopt)));

    auto lgr{std::make_unique<DatabaseCon>(
        setup, LgrDBName, FinalShardDBPragma, LgrDBInit)};
    lgr->getSession() << boost::str(
        boost::format("PRAGMA cache_size=-%d;") %
        kilobytes(config.getValueFor(SizedItem::lgrDBCache, std::nullopt)));

    return {std::move(lgr), std::move(tx)};
}

DatabasePair
makeShardIncompleteLedgerDBs(
    Config const& config,
    DatabaseCon::Setup const& setup,
    DatabaseCon::CheckpointerSetup const& checkpointerSetup)
{
    // transaction database
    auto tx{std::make_unique<DatabaseCon>(
        setup, TxDBName, TxDBPragma, TxDBInit, checkpointerSetup)};
    tx->getSession() << boost::str(
        boost::format("PRAGMA cache_size=-%d;") %
        kilobytes(config.getValueFor(SizedItem::txnDBCache)));

    // ledger database
    auto lgr{std::make_unique<DatabaseCon>(
        setup, LgrDBName, LgrDBPragma, LgrDBInit, checkpointerSetup)};
    lgr->getSession() << boost::str(
        boost::format("PRAGMA cache_size=-%d;") %
        kilobytes(config.getValueFor(SizedItem::lgrDBCache)));

    return {std::move(lgr), std::move(tx)};
}

bool
updateLedgerDBs(
    soci::session& txsession,
    soci::session& lgrsession,
    std::shared_ptr<Ledger const> const& ledger,
    std::uint32_t index,
    std::atomic<bool>& stop,
    beast::Journal j)
{
    auto const ledgerSeq{ledger->info().seq};

    // Update the transactions database
    {
        auto& session{txsession};
        soci::transaction tr(session);

        session << "DELETE FROM Transactions "
                   "WHERE LedgerSeq = :seq;",
            soci::use(ledgerSeq);
        session << "DELETE FROM AccountTransactions "
                   "WHERE LedgerSeq = :seq;",
            soci::use(ledgerSeq);

        if (ledger->info().txHash.isNonZero())
        {
            auto const sSeq{std::to_string(ledgerSeq)};
            if (!ledger->txMap().isValid())
            {
                JLOG(j.error())
                    << "shard " << index << " has an invalid transaction map"
                    << " on sequence " << sSeq;
                return false;
            }

            for (auto const& item : ledger->txs)
            {
                if (stop)
                    return false;

                auto const txID{item.first->getTransactionID()};
                auto const sTxID{to_string(txID)};
                auto const txMeta{std::make_shared<TxMeta>(
                    txID, ledger->seq(), *item.second)};

                session << "DELETE FROM AccountTransactions "
                           "WHERE TransID = :txID;",
                    soci::use(sTxID);

                auto const& accounts = txMeta->getAffectedAccounts(j);
                if (!accounts.empty())
                {
                    auto const sTxnSeq{std::to_string(txMeta->getIndex())};
                    auto const s{boost::str(
                        boost::format("('%s','%s',%s,%s)") % sTxID % "%s" %
                        sSeq % sTxnSeq)};
                    std::string sql;
                    sql.reserve((accounts.size() + 1) * 128);
                    sql =
                        "INSERT INTO AccountTransactions "
                        "(TransID, Account, LedgerSeq, TxnSeq) VALUES ";
                    sql += boost::algorithm::join(
                        accounts |
                            boost::adaptors::transformed(
                                [&](AccountID const& accountID) {
                                    return boost::str(
                                        boost::format(s) %
                                        ripple::toBase58(accountID));
                                }),
                        ",");
                    sql += ';';
                    session << sql;

                    JLOG(j.trace())
                        << "shard " << index << " account transaction: " << sql;
                }
                else
                {
                    JLOG(j.warn())
                        << "shard " << index << " transaction in ledger "
                        << sSeq << " affects no accounts";
                }

                Serializer s;
                item.second->add(s);
                session
                    << (STTx::getMetaSQLInsertReplaceHeader() +
                        item.first->getMetaSQL(
                            ledgerSeq, sqlBlobLiteral(s.modData())) +
                        ';');
            }
        }

        tr.commit();
    }

    auto const sHash{to_string(ledger->info().hash)};

    // Update the ledger database
    {
        auto& session{lgrsession};
        soci::transaction tr(session);

        auto const sParentHash{to_string(ledger->info().parentHash)};
        auto const sDrops{to_string(ledger->info().drops)};
        auto const sAccountHash{to_string(ledger->info().accountHash)};
        auto const sTxHash{to_string(ledger->info().txHash)};

        session << "DELETE FROM Ledgers "
                   "WHERE LedgerSeq = :seq;",
            soci::use(ledgerSeq);
        session << "INSERT OR REPLACE INTO Ledgers ("
                   "LedgerHash, LedgerSeq, PrevHash, TotalCoins, ClosingTime,"
                   "PrevClosingTime, CloseTimeRes, CloseFlags, AccountSetHash,"
                   "TransSetHash)"
                   "VALUES ("
                   ":ledgerHash, :ledgerSeq, :prevHash, :totalCoins,"
                   ":closingTime, :prevClosingTime, :closeTimeRes,"
                   ":closeFlags, :accountSetHash, :transSetHash);",
            soci::use(sHash), soci::use(ledgerSeq), soci::use(sParentHash),
            soci::use(sDrops),
            soci::use(ledger->info().closeTime.time_since_epoch().count()),
            soci::use(
                ledger->info().parentCloseTime.time_since_epoch().count()),
            soci::use(ledger->info().closeTimeResolution.count()),
            soci::use(ledger->info().closeFlags), soci::use(sAccountHash),
            soci::use(sTxHash);

        tr.commit();
    }

    return true;
}

/* Shard acquire db */

std::unique_ptr<DatabaseCon>
makeAcquireDB(
    DatabaseCon::Setup const& setup,
    DatabaseCon::CheckpointerSetup const& checkpointerSetup)
{
    return std::make_unique<DatabaseCon>(
        setup,
        AcquireShardDBName,
        AcquireShardDBPragma,
        AcquireShardDBInit,
        checkpointerSetup);
}

void
insertAcquireDBIndex(soci::session& session, std::uint32_t index)
{
    session << "INSERT INTO Shard (ShardIndex) "
               "VALUES (:shardIndex);",
        soci::use(index);
}

std::pair<bool, std::optional<std::string>>
selectAcquireDBLedgerSeqs(soci::session& session, std::uint32_t index)
{
    // resIndex and must be boost::optional (not std) because that's
    // what SOCI expects in its interface.
    boost::optional<std::uint32_t> resIndex;
    soci::blob sociBlob(session);
    soci::indicator blobPresent;

    session << "SELECT ShardIndex, StoredLedgerSeqs "
               "FROM Shard "
               "WHERE ShardIndex = :index;",
        soci::into(resIndex), soci::into(sociBlob, blobPresent),
        soci::use(index);

    if (!resIndex || index != resIndex)
        return {false, {}};

    if (blobPresent != soci::i_ok)
        return {true, {}};

    std::string s;
    convert(sociBlob, s);

    return {true, s};
}

std::pair<bool, AcquireShardSeqsHash>
selectAcquireDBLedgerSeqsHash(soci::session& session, std::uint32_t index)
{
    // resIndex and sHash0 must be boost::optional (not std) because that's
    // what SOCI expects in its interface.
    boost::optional<std::uint32_t> resIndex;
    boost::optional<std::string> sHash0;
    soci::blob sociBlob(session);
    soci::indicator blobPresent;

    session << "SELECT ShardIndex, LastLedgerHash, StoredLedgerSeqs "
               "FROM Shard "
               "WHERE ShardIndex = :index;",
        soci::into(resIndex), soci::into(sHash0),
        soci::into(sociBlob, blobPresent), soci::use(index);

    std::optional<std::string> sHash =
        (sHash0 ? *sHash0 : std::optional<std::string>());

    if (!resIndex || index != resIndex)
        return {false, {{}, {}}};

    if (blobPresent != soci::i_ok)
        return {true, {{}, sHash}};

    std::string s;
    convert(sociBlob, s);

    return {true, {s, sHash}};
}

void
updateAcquireDB(
    soci::session& session,
    std::shared_ptr<Ledger const> const& ledger,
    std::uint32_t index,
    std::uint32_t lastSeq,
    std::optional<std::string> const& seqs)
{
    soci::blob sociBlob(session);
    auto const sHash{to_string(ledger->info().hash)};

    if (seqs)
        convert(*seqs, sociBlob);

    if (ledger->info().seq == lastSeq)
    {
        // Store shard's last ledger hash
        session << "UPDATE Shard "
                   "SET LastLedgerHash = :lastLedgerHash,"
                   "StoredLedgerSeqs = :storedLedgerSeqs "
                   "WHERE ShardIndex = :shardIndex;",
            soci::use(sHash), soci::use(sociBlob), soci::use(index);
    }
    else
    {
        session << "UPDATE Shard "
                   "SET StoredLedgerSeqs = :storedLedgerSeqs "
                   "WHERE ShardIndex = :shardIndex;",
            soci::use(sociBlob), soci::use(index);
    }
}

/* Archive DB */

std::unique_ptr<DatabaseCon>
makeArchiveDB(boost::filesystem::path const& dir, std::string const& dbName)
{
    return std::make_unique<DatabaseCon>(
        dir, dbName, DownloaderDBPragma, ShardArchiveHandlerDBInit);
}

void
readArchiveDB(
    DatabaseCon& db,
    std::function<void(std::string const&, int)> const& func)
{
    soci::rowset<soci::row> rs =
        (db.getSession().prepare << "SELECT * FROM State;");

    for (auto it = rs.begin(); it != rs.end(); ++it)
    {
        func(it->get<std::string>(1), it->get<int>(0));
    }
}

void
insertArchiveDB(
    DatabaseCon& db,
    std::uint32_t shardIndex,
    std::string const& url)
{
    db.getSession() << "INSERT INTO State VALUES (:index, :url);",
        soci::use(shardIndex), soci::use(url);
}

void
deleteFromArchiveDB(DatabaseCon& db, std::uint32_t shardIndex)
{
    db.getSession() << "DELETE FROM State WHERE ShardIndex = :index;",
        soci::use(shardIndex);
}

void
dropArchiveDB(DatabaseCon& db)
{
    db.getSession() << "DROP TABLE State;";
}

}  // namespace ripple
