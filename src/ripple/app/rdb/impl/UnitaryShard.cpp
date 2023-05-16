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
    ANY  SPECIAL,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <ripple/app/rdb/UnitaryShard.h>
#include <ripple/basics/StringUtilities.h>
#include <boost/format.hpp>
#include <boost/range/adaptor/transformed.hpp>

namespace ripple {

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
                if (stop.load(std::memory_order_relaxed))
                    return false;

                TxMeta const txMeta{
                    item.first->getTransactionID(),
                    ledger->seq(),
                    *item.second};

                auto const sTxID = to_string(txMeta.getTxID());

                session << "DELETE FROM AccountTransactions "
                           "WHERE TransID = :txID;",
                    soci::use(sTxID);

                auto const& accounts = txMeta.getAffectedAccounts();
                if (!accounts.empty())
                {
                    auto const sTxnSeq{std::to_string(txMeta.getIndex())};
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
                else if (!isPseudoTx(*item.first))
                {
                    // It's okay for pseudo transactions to not affect any
                    // accounts.  But otherwise...
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

}  // namespace ripple
