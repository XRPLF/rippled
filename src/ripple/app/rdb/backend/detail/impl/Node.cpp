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
#include <ripple/app/ledger/PendingSaves.h>
#include <ripple/app/ledger/TransactionMaster.h>
#include <ripple/app/misc/Manifest.h>
#include <ripple/app/rdb/RelationalDatabase.h>
#include <ripple/app/rdb/backend/detail/Node.h>
#include <ripple/basics/BasicConfig.h>
#include <ripple/basics/StringUtilities.h>
#include <ripple/core/DatabaseCon.h>
#include <ripple/core/SociDB.h>
#include <ripple/json/to_string.h>
#include <boost/algorithm/string.hpp>
#include <boost/range/adaptor/transformed.hpp>
#include <soci/sqlite3/soci-sqlite3.h>

namespace ripple {
namespace detail {

/**
 * @brief to_string Returns the name of a table according to its TableType.
 * @param type An enum denoting the table's type.
 * @return Name of the table.
 */
static std::string
to_string(TableType type)
{
    static_assert(
        TableTypeCount == 3,
        "Need to modify switch statement if enum is modified");

    switch (type)
    {
        case TableType::Ledgers:
            return "Ledgers";
        case TableType::Transactions:
            return "Transactions";
        case TableType::AccountTransactions:
            return "AccountTransactions";
        default:
            assert(false);
            return "Unknown";
    }
}

DatabasePairValid
makeLedgerDBs(
    Config const& config,
    DatabaseCon::Setup const& setup,
    DatabaseCon::CheckpointerSetup const& checkpointerSetup)
{
    // ledger database
    auto lgr{std::make_unique<DatabaseCon>(
        setup, LgrDBName, LgrDBPragma, LgrDBInit, checkpointerSetup)};
    lgr->getSession() << boost::str(
        boost::format("PRAGMA cache_size=-%d;") %
        kilobytes(config.getValueFor(SizedItem::lgrDBCache)));

    if (config.useTxTables())
    {
        // transaction database
        auto tx{std::make_unique<DatabaseCon>(
            setup, TxDBName, TxDBPragma, TxDBInit, checkpointerSetup)};
        tx->getSession() << boost::str(
            boost::format("PRAGMA cache_size=-%d;") %
            kilobytes(config.getValueFor(SizedItem::txnDBCache)));

        if (!setup.standAlone || setup.startUp == Config::LOAD ||
            setup.startUp == Config::LOAD_FILE ||
            setup.startUp == Config::REPLAY)
        {
            // Check if AccountTransactions has primary key
            std::string cid, name, type;
            std::size_t notnull, dflt_value, pk;
            soci::indicator ind;
            soci::statement st =
                (tx->getSession().prepare
                     << ("PRAGMA table_info(AccountTransactions);"),
                 soci::into(cid),
                 soci::into(name),
                 soci::into(type),
                 soci::into(notnull),
                 soci::into(dflt_value, ind),
                 soci::into(pk));

            st.execute();
            while (st.fetch())
            {
                if (pk == 1)
                {
                    return {std::move(lgr), std::move(tx), false};
                }
            }
        }

        return {std::move(lgr), std::move(tx), true};
    }
    else
        return {std::move(lgr), {}, true};
}

std::optional<LedgerIndex>
getMinLedgerSeq(soci::session& session, TableType type)
{
    std::string query = "SELECT MIN(LedgerSeq) FROM " + to_string(type) + ";";
    // SOCI requires boost::optional (not std::optional) as the parameter.
    boost::optional<LedgerIndex> m;
    session << query, soci::into(m);
    return m ? *m : std::optional<LedgerIndex>();
}

std::optional<LedgerIndex>
getMaxLedgerSeq(soci::session& session, TableType type)
{
    std::string query = "SELECT MAX(LedgerSeq) FROM " + to_string(type) + ";";
    // SOCI requires boost::optional (not std::optional) as the parameter.
    boost::optional<LedgerIndex> m;
    session << query, soci::into(m);
    return m ? *m : std::optional<LedgerIndex>();
}

void
deleteByLedgerSeq(soci::session& session, TableType type, LedgerIndex ledgerSeq)
{
    session << "DELETE FROM " << to_string(type)
            << " WHERE LedgerSeq == " << ledgerSeq << ";";
}

void
deleteBeforeLedgerSeq(
    soci::session& session,
    TableType type,
    LedgerIndex ledgerSeq)
{
    session << "DELETE FROM " << to_string(type) << " WHERE LedgerSeq < "
            << ledgerSeq << ";";
}

std::size_t
getRows(soci::session& session, TableType type)
{
    std::size_t rows;
    session << "SELECT COUNT(*) AS rows "
               "FROM "
            << to_string(type) << ";",
        soci::into(rows);

    return rows;
}

RelationalDatabase::CountMinMax
getRowsMinMax(soci::session& session, TableType type)
{
    RelationalDatabase::CountMinMax res;
    session << "SELECT COUNT(*) AS rows, "
               "MIN(LedgerSeq) AS first, "
               "MAX(LedgerSeq) AS last "
               "FROM "
            << to_string(type) << ";",
        soci::into(res.numberOfRows), soci::into(res.minLedgerSequence),
        soci::into(res.maxLedgerSequence);

    return res;
}

bool
saveValidatedLedger(
    DatabaseCon& ldgDB,
    DatabaseCon& txnDB,
    Application& app,
    std::shared_ptr<Ledger const> const& ledger,
    bool current)
{
    auto j = app.journal("Ledger");
    auto seq = ledger->info().seq;

    // TODO(tom): Fix this hard-coded SQL!
    JLOG(j.trace()) << "saveValidatedLedger " << (current ? "" : "fromAcquire ")
                    << seq;

    if (!ledger->info().accountHash.isNonZero())
    {
        JLOG(j.fatal()) << "AH is zero: " << getJson({*ledger, {}});
        assert(false);
    }

    if (ledger->info().accountHash != ledger->stateMap().getHash().as_uint256())
    {
        JLOG(j.fatal()) << "sAL: " << ledger->info().accountHash
                        << " != " << ledger->stateMap().getHash();
        JLOG(j.fatal()) << "saveAcceptedLedger: seq=" << seq
                        << ", current=" << current;
        assert(false);
    }

    assert(ledger->info().txHash == ledger->txMap().getHash().as_uint256());

    // Save the ledger header in the hashed object store
    {
        Serializer s(128);
        s.add32(HashPrefix::ledgerMaster);
        addRaw(ledger->info(), s);
        app.getNodeStore().store(
            hotLEDGER, std::move(s.modData()), ledger->info().hash, seq);
    }

    std::shared_ptr<AcceptedLedger> aLedger;
    try
    {
        aLedger = app.getAcceptedLedgerCache().fetch(ledger->info().hash);
        if (!aLedger)
        {
            aLedger = std::make_shared<AcceptedLedger>(ledger, app);
            app.getAcceptedLedgerCache().canonicalize_replace_client(
                ledger->info().hash, aLedger);
        }
    }
    catch (std::exception const&)
    {
        JLOG(j.warn()) << "An accepted ledger was missing nodes";
        app.getLedgerMaster().failedSave(seq, ledger->info().hash);
        // Clients can now trust the database for information about this
        // ledger sequence.
        app.pendingSaves().finishWork(seq);
        return false;
    }

    {
        static boost::format deleteLedger(
            "DELETE FROM Ledgers WHERE LedgerSeq = %u;");
        static boost::format deleteTrans1(
            "DELETE FROM Transactions WHERE LedgerSeq = %u;");
        static boost::format deleteTrans2(
            "DELETE FROM AccountTransactions WHERE LedgerSeq = %u;");
        static boost::format deleteAcctTrans(
            "DELETE FROM AccountTransactions WHERE TransID = '%s';");

        {
            auto db = ldgDB.checkoutDb();
            *db << boost::str(deleteLedger % seq);
        }

        if (app.config().useTxTables())
        {
            auto db = txnDB.checkoutDb();

            soci::transaction tr(*db);

            *db << boost::str(deleteTrans1 % seq);
            *db << boost::str(deleteTrans2 % seq);

            std::string const ledgerSeq(std::to_string(seq));

            for (auto const& acceptedLedgerTx : *aLedger)
            {
                uint256 transactionID = acceptedLedgerTx->getTransactionID();

                std::string const txnId(to_string(transactionID));
                std::string const txnSeq(
                    std::to_string(acceptedLedgerTx->getTxnSeq()));

                *db << boost::str(deleteAcctTrans % transactionID);

                auto const& accts = acceptedLedgerTx->getAffected();

                if (!accts.empty())
                {
                    std::string sql(
                        "INSERT INTO AccountTransactions "
                        "(TransID, Account, LedgerSeq, TxnSeq) VALUES ");

                    // Try to make an educated guess on how much space we'll
                    // need for our arguments. In argument order we have: 64
                    // + 34 + 10 + 10 = 118 + 10 extra = 128 bytes
                    sql.reserve(sql.length() + (accts.size() * 128));

                    bool first = true;
                    for (auto const& account : accts)
                    {
                        if (!first)
                            sql += ", ('";
                        else
                        {
                            sql += "('";
                            first = false;
                        }

                        sql += txnId;
                        sql += "','";
                        sql += toBase58(account);
                        sql += "',";
                        sql += ledgerSeq;
                        sql += ",";
                        sql += txnSeq;
                        sql += ")";
                    }
                    sql += ";";
                    JLOG(j.trace()) << "ActTx: " << sql;
                    *db << sql;
                }
                else if (auto const& sleTxn = acceptedLedgerTx->getTxn();
                         !isPseudoTx(*sleTxn))
                {
                    // It's okay for pseudo transactions to not affect any
                    // accounts.  But otherwise...
                    JLOG(j.warn()) << "Transaction in ledger " << seq
                                   << " affects no accounts";
                    JLOG(j.warn()) << sleTxn->getJson(JsonOptions::none);
                }

                *db
                    << (STTx::getMetaSQLInsertReplaceHeader() +
                        acceptedLedgerTx->getTxn()->getMetaSQL(
                            seq, acceptedLedgerTx->getEscMeta()) +
                        ";");

                app.getMasterTransaction().inLedger(transactionID, seq);
            }

            tr.commit();
        }

        {
            static std::string addLedger(
                R"sql(INSERT OR REPLACE INTO Ledgers
                (LedgerHash,LedgerSeq,PrevHash,TotalCoins,ClosingTime,PrevClosingTime,
                CloseTimeRes,CloseFlags,AccountSetHash,TransSetHash)
            VALUES
                (:ledgerHash,:ledgerSeq,:prevHash,:totalCoins,:closingTime,:prevClosingTime,
                :closeTimeRes,:closeFlags,:accountSetHash,:transSetHash);)sql");

            auto db(ldgDB.checkoutDb());

            soci::transaction tr(*db);

            auto const hash = to_string(ledger->info().hash);
            auto const parentHash = to_string(ledger->info().parentHash);
            auto const drops = to_string(ledger->info().drops);
            auto const closeTime =
                ledger->info().closeTime.time_since_epoch().count();
            auto const parentCloseTime =
                ledger->info().parentCloseTime.time_since_epoch().count();
            auto const closeTimeResolution =
                ledger->info().closeTimeResolution.count();
            auto const closeFlags = ledger->info().closeFlags;
            auto const accountHash = to_string(ledger->info().accountHash);
            auto const txHash = to_string(ledger->info().txHash);

            *db << addLedger, soci::use(hash), soci::use(seq),
                soci::use(parentHash), soci::use(drops), soci::use(closeTime),
                soci::use(parentCloseTime), soci::use(closeTimeResolution),
                soci::use(closeFlags), soci::use(accountHash),
                soci::use(txHash);

            tr.commit();
        }
    }

    return true;
}

/**
 * @brief getLedgerInfo Returns the info of the ledger retrieved from the
 *        database by using the provided SQL query suffix.
 * @param session Session with the database.
 * @param sqlSuffix SQL string used to specify the sought ledger.
 * @param j Journal.
 * @return Ledger info or no value if the ledger was not found.
 */
static std::optional<LedgerInfo>
getLedgerInfo(
    soci::session& session,
    std::string const& sqlSuffix,
    beast::Journal j)
{
    // SOCI requires boost::optional (not std::optional) as parameters.
    boost::optional<std::string> hash, parentHash, accountHash, txHash;
    boost::optional<std::uint64_t> seq, drops, closeTime, parentCloseTime,
        closeTimeResolution, closeFlags;

    std::string const sql =
        "SELECT "
        "LedgerHash, PrevHash, AccountSetHash, TransSetHash, "
        "TotalCoins,"
        "ClosingTime, PrevClosingTime, CloseTimeRes, CloseFlags,"
        "LedgerSeq FROM Ledgers " +
        sqlSuffix + ";";

    session << sql, soci::into(hash), soci::into(parentHash),
        soci::into(accountHash), soci::into(txHash), soci::into(drops),
        soci::into(closeTime), soci::into(parentCloseTime),
        soci::into(closeTimeResolution), soci::into(closeFlags),
        soci::into(seq);

    if (!session.got_data())
    {
        JLOG(j.debug()) << "Ledger not found: " << sqlSuffix;
        return {};
    }

    using time_point = NetClock::time_point;
    using duration = NetClock::duration;

    LedgerInfo info;

    if (hash && !info.hash.parseHex(*hash))
    {
        JLOG(j.debug()) << "Hash parse error for ledger: " << sqlSuffix;
        return {};
    }

    if (parentHash && !info.parentHash.parseHex(*parentHash))
    {
        JLOG(j.debug()) << "parentHash parse error for ledger: " << sqlSuffix;
        return {};
    }

    if (accountHash && !info.accountHash.parseHex(*accountHash))
    {
        JLOG(j.debug()) << "accountHash parse error for ledger: " << sqlSuffix;
        return {};
    }

    if (txHash && !info.txHash.parseHex(*txHash))
    {
        JLOG(j.debug()) << "txHash parse error for ledger: " << sqlSuffix;
        return {};
    }

    info.seq = rangeCheckedCast<std::uint32_t>(seq.value_or(0));
    info.drops = drops.value_or(0);
    info.closeTime = time_point{duration{closeTime.value_or(0)}};
    info.parentCloseTime = time_point{duration{parentCloseTime.value_or(0)}};
    info.closeFlags = closeFlags.value_or(0);
    info.closeTimeResolution = duration{closeTimeResolution.value_or(0)};

    return info;
}

std::optional<LedgerInfo>
getLedgerInfoByIndex(
    soci::session& session,
    LedgerIndex ledgerSeq,
    beast::Journal j)
{
    std::ostringstream s;
    s << "WHERE LedgerSeq = " << ledgerSeq;
    return getLedgerInfo(session, s.str(), j);
}

std::optional<LedgerInfo>
getNewestLedgerInfo(soci::session& session, beast::Journal j)
{
    std::ostringstream s;
    s << "ORDER BY LedgerSeq DESC LIMIT 1";
    return getLedgerInfo(session, s.str(), j);
}

std::optional<LedgerInfo>
getLimitedOldestLedgerInfo(
    soci::session& session,
    LedgerIndex ledgerFirstIndex,
    beast::Journal j)
{
    std::ostringstream s;
    s << "WHERE LedgerSeq >= " + std::to_string(ledgerFirstIndex) +
            " ORDER BY LedgerSeq ASC LIMIT 1";
    return getLedgerInfo(session, s.str(), j);
}

std::optional<LedgerInfo>
getLimitedNewestLedgerInfo(
    soci::session& session,
    LedgerIndex ledgerFirstIndex,
    beast::Journal j)
{
    std::ostringstream s;
    s << "WHERE LedgerSeq >= " + std::to_string(ledgerFirstIndex) +
            " ORDER BY LedgerSeq DESC LIMIT 1";
    return getLedgerInfo(session, s.str(), j);
}

std::optional<LedgerInfo>
getLedgerInfoByHash(
    soci::session& session,
    uint256 const& ledgerHash,
    beast::Journal j)
{
    std::ostringstream s;
    s << "WHERE LedgerHash = '" << ledgerHash << "'";
    return getLedgerInfo(session, s.str(), j);
}

uint256
getHashByIndex(soci::session& session, LedgerIndex ledgerIndex)
{
    uint256 ret;

    std::string sql =
        "SELECT LedgerHash FROM Ledgers INDEXED BY SeqLedger WHERE LedgerSeq='";
    sql.append(beast::lexicalCastThrow<std::string>(ledgerIndex));
    sql.append("';");

    std::string hash;
    {
        // SOCI requires boost::optional (not std::optional) as the parameter.
        boost::optional<std::string> lh;
        session << sql, soci::into(lh);

        if (!session.got_data() || !lh)
            return ret;

        hash = *lh;
        if (hash.empty())
            return ret;
    }

    if (!ret.parseHex(hash))
        return ret;

    return ret;
}

std::optional<LedgerHashPair>
getHashesByIndex(
    soci::session& session,
    LedgerIndex ledgerIndex,
    beast::Journal j)
{
    // SOCI requires boost::optional (not std::optional) as the parameter.
    boost::optional<std::string> lhO, phO;

    session << "SELECT LedgerHash,PrevHash FROM Ledgers "
               "INDEXED BY SeqLedger WHERE LedgerSeq = :ls;",
        soci::into(lhO), soci::into(phO), soci::use(ledgerIndex);

    if (!lhO || !phO)
    {
        auto stream = j.trace();
        JLOG(stream) << "Don't have ledger " << ledgerIndex;
        return {};
    }

    LedgerHashPair hashes;
    if (!hashes.ledgerHash.parseHex(*lhO) || !hashes.parentHash.parseHex(*phO))
    {
        auto stream = j.trace();
        JLOG(stream) << "Error parse hashes for ledger " << ledgerIndex;
        return {};
    }

    return hashes;
}

std::map<LedgerIndex, LedgerHashPair>
getHashesByIndex(
    soci::session& session,
    LedgerIndex minSeq,
    LedgerIndex maxSeq,
    beast::Journal j)
{
    std::string sql =
        "SELECT LedgerSeq,LedgerHash,PrevHash FROM Ledgers WHERE LedgerSeq >= ";
    sql.append(beast::lexicalCastThrow<std::string>(minSeq));
    sql.append(" AND LedgerSeq <= ");
    sql.append(beast::lexicalCastThrow<std::string>(maxSeq));
    sql.append(";");

    std::uint64_t ls;
    std::string lh;
    // SOCI requires boost::optional (not std::optional) as the parameter.
    boost::optional<std::string> ph;
    soci::statement st =
        (session.prepare << sql,
         soci::into(ls),
         soci::into(lh),
         soci::into(ph));

    st.execute();
    std::map<LedgerIndex, LedgerHashPair> res;
    while (st.fetch())
    {
        LedgerHashPair& hashes = res[rangeCheckedCast<LedgerIndex>(ls)];
        if (!hashes.ledgerHash.parseHex(lh))
        {
            JLOG(j.warn()) << "Error parsed hash for ledger seq: " << ls;
        }
        if (!ph)
        {
            JLOG(j.warn()) << "Null prev hash for ledger seq: " << ls;
        }
        else if (!hashes.parentHash.parseHex(*ph))
        {
            JLOG(j.warn()) << "Error parsed prev hash for ledger seq: " << ls;
        }
    }
    return res;
}

std::pair<std::vector<std::shared_ptr<Transaction>>, int>
getTxHistory(
    soci::session& session,
    Application& app,
    LedgerIndex startIndex,
    int quantity,
    bool count)
{
    std::string sql = boost::str(
        boost::format(
            "SELECT LedgerSeq, Status, RawTxn "
            "FROM Transactions ORDER BY LedgerSeq DESC LIMIT %u,%u;") %
        startIndex % quantity);

    std::vector<std::shared_ptr<Transaction>> txs;
    int total = 0;

    {
        // SOCI requires boost::optional (not std::optional) as parameters.
        boost::optional<std::uint64_t> ledgerSeq;
        boost::optional<std::string> status;
        soci::blob sociRawTxnBlob(session);
        soci::indicator rti;
        Blob rawTxn;

        soci::statement st =
            (session.prepare << sql,
             soci::into(ledgerSeq),
             soci::into(status),
             soci::into(sociRawTxnBlob, rti));

        st.execute();
        while (st.fetch())
        {
            if (soci::i_ok == rti)
                convert(sociRawTxnBlob, rawTxn);
            else
                rawTxn.clear();

            if (auto trans = Transaction::transactionFromSQL(
                    ledgerSeq, status, rawTxn, app))
            {
                total++;
                txs.push_back(trans);
            }
        }

        if (!total && count)
        {
            session << "SELECT COUNT(*) FROM Transactions;", soci::into(total);

            total = -total;
        }
    }

    return {txs, total};
}

/**
 * @brief transactionsSQL Returns a SQL query for selecting the oldest or newest
 *        transactions in decoded or binary form for the account that matches
 *        the given criteria starting from the provided offset.
 * @param app Application object.
 * @param selection List of table fields to select from the database.
 * @param options Struct AccountTxOptions which contains the criteria to match:
 *        the account, the ledger search range, the offset of the first entry to
 *        return, the number of transactions to return, and a flag if this
 *        number is unlimited.
 * @param limit_used Number of transactions already returned in calls
 *        to other shard databases, if shard databases are used.
 *        No value if the node database is used.
 * @param descending True for descending order, false for ascending.
 * @param binary True for binary form, false for decoded.
 * @param count True for counting the number of transactions, false for
 *        selecting them.
 * @param j Journal.
 * @return SQL query string.
 */
static std::string
transactionsSQL(
    Application& app,
    std::string selection,
    RelationalDatabase::AccountTxOptions const& options,
    std::optional<int> const& limit_used,
    bool descending,
    bool binary,
    bool count,
    beast::Journal j)
{
    constexpr std::uint32_t NONBINARY_PAGE_LENGTH = 200;
    constexpr std::uint32_t BINARY_PAGE_LENGTH = 500;

    std::uint32_t numberOfResults;

    if (count)
    {
        numberOfResults = std::numeric_limits<std::uint32_t>::max();
    }
    else if (options.limit == UINT32_MAX)
    {
        numberOfResults = binary ? BINARY_PAGE_LENGTH : NONBINARY_PAGE_LENGTH;
    }
    else if (!options.bUnlimited)
    {
        numberOfResults = std::min(
            binary ? BINARY_PAGE_LENGTH : NONBINARY_PAGE_LENGTH, options.limit);
    }
    else
    {
        numberOfResults = options.limit;
    }

    if (limit_used)
    {
        if (numberOfResults <= *limit_used)
            return "";
        else
            numberOfResults -= *limit_used;
    }

    std::string maxClause = "";
    std::string minClause = "";

    if (options.maxLedger)
    {
        maxClause = boost::str(
            boost::format("AND AccountTransactions.LedgerSeq <= '%u'") %
            options.maxLedger);
    }

    if (options.minLedger)
    {
        minClause = boost::str(
            boost::format("AND AccountTransactions.LedgerSeq >= '%u'") %
            options.minLedger);
    }

    std::string sql;

    if (count)
        sql = boost::str(
            boost::format("SELECT %s FROM AccountTransactions "
                          "WHERE Account = '%s' %s %s LIMIT %u, %u;") %
            selection % toBase58(options.account) % maxClause % minClause %
            beast::lexicalCastThrow<std::string>(options.offset) %
            beast::lexicalCastThrow<std::string>(numberOfResults));
    else
        sql = boost::str(
            boost::format(
                "SELECT %s FROM "
                "AccountTransactions INNER JOIN Transactions "
                "ON Transactions.TransID = AccountTransactions.TransID "
                "WHERE Account = '%s' %s %s "
                "ORDER BY AccountTransactions.LedgerSeq %s, "
                "AccountTransactions.TxnSeq %s, AccountTransactions.TransID %s "
                "LIMIT %u, %u;") %
            selection % toBase58(options.account) % maxClause % minClause %
            (descending ? "DESC" : "ASC") % (descending ? "DESC" : "ASC") %
            (descending ? "DESC" : "ASC") %
            beast::lexicalCastThrow<std::string>(options.offset) %
            beast::lexicalCastThrow<std::string>(numberOfResults));
    JLOG(j.trace()) << "txSQL query: " << sql;
    return sql;
}

/**
 * @brief getAccountTxs Returns the oldest or newest transactions for the
 *        account that matches the given criteria starting from the provided
 *        offset.
 * @param session Session with the database.
 * @param app Application object.
 * @param ledgerMaster LedgerMaster object.
 * @param options Struct AccountTxOptions which contains the criteria to match:
 *        the account, the ledger search range, the offset of the first entry to
 *        return, the number of transactions to return, and a flag if this
 *        number is unlimited.
 * @param limit_used Number of transactions already returned in calls
 *        to other shard databases, if shard databases are used.
 *        No value if the node database is used.
 * @param descending True for descending order, false for ascending.
 * @param j Journal.
 * @return Vector of pairs of found transactions and their metadata sorted by
 *         account sequence in the specified order along with the number of
 *         transactions processed or skipped. If this number is >= 0, then it
 *         represents the number of transactions processed, if it is < 0, then
 *         -number represents the number of transactions skipped. We need to
 *         skip some number of transactions if option offset is > 0 in the
 *         options structure.
 */
static std::pair<RelationalDatabase::AccountTxs, int>
getAccountTxs(
    soci::session& session,
    Application& app,
    LedgerMaster& ledgerMaster,
    RelationalDatabase::AccountTxOptions const& options,
    std::optional<int> const& limit_used,
    bool descending,
    beast::Journal j)
{
    RelationalDatabase::AccountTxs ret;

    std::string sql = transactionsSQL(
        app,
        "AccountTransactions.LedgerSeq,Status,RawTxn,TxnMeta",
        options,
        limit_used,
        descending,
        false,
        false,
        j);
    if (sql == "")
        return {ret, 0};

    int total = 0;
    {
        // SOCI requires boost::optional (not std::optional) as parameters.
        boost::optional<std::uint64_t> ledgerSeq;
        boost::optional<std::string> status;
        soci::blob sociTxnBlob(session), sociTxnMetaBlob(session);
        soci::indicator rti, tmi;
        Blob rawTxn, txnMeta;

        soci::statement st =
            (session.prepare << sql,
             soci::into(ledgerSeq),
             soci::into(status),
             soci::into(sociTxnBlob, rti),
             soci::into(sociTxnMetaBlob, tmi));

        st.execute();
        while (st.fetch())
        {
            if (soci::i_ok == rti)
                convert(sociTxnBlob, rawTxn);
            else
                rawTxn.clear();

            if (soci::i_ok == tmi)
                convert(sociTxnMetaBlob, txnMeta);
            else
                txnMeta.clear();

            auto txn =
                Transaction::transactionFromSQL(ledgerSeq, status, rawTxn, app);

            if (txnMeta.empty())
            {  // Work around a bug that could leave the metadata missing
                auto const seq =
                    rangeCheckedCast<std::uint32_t>(ledgerSeq.value_or(0));

                JLOG(j.warn())
                    << "Recovering ledger " << seq << ", txn " << txn->getID();

                if (auto l = ledgerMaster.getLedgerBySeq(seq))
                    pendSaveValidated(app, l, false, false);
            }

            if (txn)
            {
                ret.emplace_back(
                    txn,
                    std::make_shared<TxMeta>(
                        txn->getID(), txn->getLedger(), txnMeta));
                total++;
            }
        }

        if (!total && limit_used)
        {
            RelationalDatabase::AccountTxOptions opt = options;
            opt.offset = 0;
            std::string sql1 = transactionsSQL(
                app, "COUNT(*)", opt, limit_used, descending, false, false, j);

            session << sql1, soci::into(total);

            total = -total;
        }
    }

    return {ret, total};
}

std::pair<RelationalDatabase::AccountTxs, int>
getOldestAccountTxs(
    soci::session& session,
    Application& app,
    LedgerMaster& ledgerMaster,
    RelationalDatabase::AccountTxOptions const& options,
    std::optional<int> const& limit_used,
    beast::Journal j)
{
    return getAccountTxs(
        session, app, ledgerMaster, options, limit_used, false, j);
}

std::pair<RelationalDatabase::AccountTxs, int>
getNewestAccountTxs(
    soci::session& session,
    Application& app,
    LedgerMaster& ledgerMaster,
    RelationalDatabase::AccountTxOptions const& options,
    std::optional<int> const& limit_used,
    beast::Journal j)
{
    return getAccountTxs(
        session, app, ledgerMaster, options, limit_used, true, j);
}

/**
 * @brief getAccountTxsB Returns the oldest or newest transactions in binary
 *        form for the account that matches given criteria starting from
 *        the provided offset.
 * @param session Session with the database.
 * @param app Application object.
 * @param options Struct AccountTxOptions which contains the criteria to match:
 *        the account, the ledger search range, the offset of the first entry to
 *        return, the number of transactions to return, and a flag if this
 *        number is unlimited.
 * @param limit_used Number of transactions already returned in calls to other
 *        shard databases, if shard databases are used. No value if the node
 *        database is used.
 * @param descending True for descending order, false for ascending.
 * @param j Journal.
 * @return Vector of tuples each containing (the found transactions, their
 *         metadata, and their account sequences) sorted by account sequence in
 *         the specified order along with the number of transactions processed
 *         or skipped. If this number is >= 0, then it represents the number of
 *         transactions processed, if it is < 0, then -number represents the
 *         number of transactions skipped. We need to skip some number of
 *         transactions if option offset is > 0 in the options structure.
 */
static std::pair<std::vector<RelationalDatabase::txnMetaLedgerType>, int>
getAccountTxsB(
    soci::session& session,
    Application& app,
    RelationalDatabase::AccountTxOptions const& options,
    std::optional<int> const& limit_used,
    bool descending,
    beast::Journal j)
{
    std::vector<RelationalDatabase::txnMetaLedgerType> ret;

    std::string sql = transactionsSQL(
        app,
        "AccountTransactions.LedgerSeq,Status,RawTxn,TxnMeta",
        options,
        limit_used,
        descending,
        true /*binary*/,
        false,
        j);
    if (sql == "")
        return {ret, 0};

    int total = 0;

    {
        // SOCI requires boost::optional (not std::optional) as parameters.
        boost::optional<std::uint64_t> ledgerSeq;
        boost::optional<std::string> status;
        soci::blob sociTxnBlob(session), sociTxnMetaBlob(session);
        soci::indicator rti, tmi;

        soci::statement st =
            (session.prepare << sql,
             soci::into(ledgerSeq),
             soci::into(status),
             soci::into(sociTxnBlob, rti),
             soci::into(sociTxnMetaBlob, tmi));

        st.execute();
        while (st.fetch())
        {
            Blob rawTxn;
            if (soci::i_ok == rti)
                convert(sociTxnBlob, rawTxn);
            Blob txnMeta;
            if (soci::i_ok == tmi)
                convert(sociTxnMetaBlob, txnMeta);

            auto const seq =
                rangeCheckedCast<std::uint32_t>(ledgerSeq.value_or(0));

            ret.emplace_back(std::move(rawTxn), std::move(txnMeta), seq);
            total++;
        }

        if (!total && limit_used)
        {
            RelationalDatabase::AccountTxOptions opt = options;
            opt.offset = 0;
            std::string sql1 = transactionsSQL(
                app, "COUNT(*)", opt, limit_used, descending, true, false, j);

            session << sql1, soci::into(total);

            total = -total;
        }
    }

    return {ret, total};
}

std::pair<std::vector<RelationalDatabase::txnMetaLedgerType>, int>
getOldestAccountTxsB(
    soci::session& session,
    Application& app,
    RelationalDatabase::AccountTxOptions const& options,
    std::optional<int> const& limit_used,
    beast::Journal j)
{
    return getAccountTxsB(session, app, options, limit_used, false, j);
}

std::pair<std::vector<RelationalDatabase::txnMetaLedgerType>, int>
getNewestAccountTxsB(
    soci::session& session,
    Application& app,
    RelationalDatabase::AccountTxOptions const& options,
    std::optional<int> const& limit_used,
    beast::Journal j)
{
    return getAccountTxsB(session, app, options, limit_used, true, j);
}

/**
 * @brief accountTxPage Searches for the oldest or newest transactions for the
 *        account that matches the given criteria starting from the provided
 *        marker and invokes the callback parameter for each found transaction.
 * @param session Session with the database.
 * @param onUnsavedLedger Callback function to call on each found unsaved
 *        ledger within the given range.
 * @param onTransaction Callback function to call on each found transaction.
 * @param options Struct AccountTxPageOptions which contains the criteria to
 *        match: the account, the ledger search range, the marker of the first
 *        returned entry, the number of transactions to return, and a flag if
 *        this number unlimited.
 * @param limit_used Number of transactions already returned in calls
 *        to other shard databases.
 * @param page_length Total number of transactions to return.
 * @param forward True for ascending order, false for descending.
 * @return Vector of tuples of found transactions, their metadata and account
 *         sequences sorted in the specified order by account sequence, a marker
 *         for the next search if the search was not finished and the number of
 *         transactions processed during this call.
 */
static std::pair<std::optional<RelationalDatabase::AccountTxMarker>, int>
accountTxPage(
    soci::session& session,
    std::function<void(std::uint32_t)> const& onUnsavedLedger,
    std::function<
        void(std::uint32_t, std::string const&, Blob&&, Blob&&)> const&
        onTransaction,
    RelationalDatabase::AccountTxPageOptions const& options,
    int limit_used,
    std::uint32_t page_length,
    bool forward)
{
    int total = 0;

    bool lookingForMarker = options.marker.has_value();

    std::uint32_t numberOfResults;

    if (options.limit == 0 || options.limit == UINT32_MAX ||
        (options.limit > page_length && !options.bAdmin))
        numberOfResults = page_length;
    else
        numberOfResults = options.limit;

    if (numberOfResults < limit_used)
        return {options.marker, -1};
    numberOfResults -= limit_used;

    // As an account can have many thousands of transactions, there is a limit
    // placed on the amount of transactions returned. If the limit is reached
    // before the result set has been exhausted (we always query for one more
    // than the limit), then we return an opaque marker that can be supplied in
    // a subsequent query.
    std::uint32_t queryLimit = numberOfResults + 1;
    std::uint32_t findLedger = 0, findSeq = 0;

    if (lookingForMarker)
    {
        findLedger = options.marker->ledgerSeq;
        findSeq = options.marker->txnSeq;
    }

    std::optional<RelationalDatabase::AccountTxMarker> newmarker;
    if (limit_used > 0)
        newmarker = options.marker;

    static std::string const prefix(
        R"(SELECT AccountTransactions.LedgerSeq,AccountTransactions.TxnSeq,
          Status,RawTxn,TxnMeta
          FROM AccountTransactions INNER JOIN Transactions
          ON Transactions.TransID = AccountTransactions.TransID
          AND AccountTransactions.Account = '%s' WHERE
          )");

    std::string sql;

    // SQL's BETWEEN uses a closed interval ([a,b])

    const char* const order = forward ? "ASC" : "DESC";

    if (findLedger == 0)
    {
        sql = boost::str(
            boost::format(
                prefix + (R"(AccountTransactions.LedgerSeq BETWEEN '%u' AND '%u'
             ORDER BY AccountTransactions.LedgerSeq %s,
             AccountTransactions.TxnSeq %s
             LIMIT %u;)")) %
            toBase58(options.account) % options.minLedger % options.maxLedger %
            order % order % queryLimit);
    }
    else
    {
        const char* const compare = forward ? ">=" : "<=";
        const std::uint32_t minLedger =
            forward ? findLedger + 1 : options.minLedger;
        const std::uint32_t maxLedger =
            forward ? options.maxLedger : findLedger - 1;

        auto b58acct = toBase58(options.account);
        sql = boost::str(
            boost::format((
                R"(SELECT AccountTransactions.LedgerSeq,AccountTransactions.TxnSeq,
            Status,RawTxn,TxnMeta
            FROM AccountTransactions, Transactions WHERE
            (AccountTransactions.TransID = Transactions.TransID AND
            AccountTransactions.Account = '%s' AND
            AccountTransactions.LedgerSeq BETWEEN '%u' AND '%u')
            OR
            (AccountTransactions.TransID = Transactions.TransID AND
            AccountTransactions.Account = '%s' AND
            AccountTransactions.LedgerSeq = '%u' AND
            AccountTransactions.TxnSeq %s '%u')
            ORDER BY AccountTransactions.LedgerSeq %s,
            AccountTransactions.TxnSeq %s
            LIMIT %u;
            )")) %
            b58acct % minLedger % maxLedger % b58acct % findLedger % compare %
            findSeq % order % order % queryLimit);
    }

    {
        Blob rawData;
        Blob rawMeta;

        // SOCI requires boost::optional (not std::optional) as parameters.
        boost::optional<std::uint64_t> ledgerSeq;
        boost::optional<std::uint32_t> txnSeq;
        boost::optional<std::string> status;
        soci::blob txnData(session);
        soci::blob txnMeta(session);
        soci::indicator dataPresent, metaPresent;

        soci::statement st =
            (session.prepare << sql,
             soci::into(ledgerSeq),
             soci::into(txnSeq),
             soci::into(status),
             soci::into(txnData, dataPresent),
             soci::into(txnMeta, metaPresent));

        st.execute();

        while (st.fetch())
        {
            if (lookingForMarker)
            {
                if (findLedger == ledgerSeq.value_or(0) &&
                    findSeq == txnSeq.value_or(0))
                {
                    lookingForMarker = false;
                }
                else
                    continue;
            }
            else if (numberOfResults == 0)
            {
                newmarker = {
                    rangeCheckedCast<std::uint32_t>(ledgerSeq.value_or(0)),
                    txnSeq.value_or(0)};
                break;
            }

            if (dataPresent == soci::i_ok)
                convert(txnData, rawData);
            else
                rawData.clear();

            if (metaPresent == soci::i_ok)
                convert(txnMeta, rawMeta);
            else
                rawMeta.clear();

            // Work around a bug that could leave the metadata missing
            if (rawMeta.size() == 0)
                onUnsavedLedger(ledgerSeq.value_or(0));

            // `rawData` and `rawMeta` will be used after they are moved.
            // That's OK.
            onTransaction(
                rangeCheckedCast<std::uint32_t>(ledgerSeq.value_or(0)),
                *status,
                std::move(rawData),
                std::move(rawMeta));
            // Note some callbacks will move the data, some will not. Clear
            // them so code doesn't depend on if the data was actually moved
            // or not. The code will be more efficient if `rawData` and
            // `rawMeta` don't have to allocate in `convert`, so don't
            // refactor my moving these variables into loop scope.
            rawData.clear();
            rawMeta.clear();

            --numberOfResults;
            total++;
        }
    }

    return {newmarker, total};
}

std::pair<std::optional<RelationalDatabase::AccountTxMarker>, int>
oldestAccountTxPage(
    soci::session& session,
    std::function<void(std::uint32_t)> const& onUnsavedLedger,
    std::function<
        void(std::uint32_t, std::string const&, Blob&&, Blob&&)> const&
        onTransaction,
    RelationalDatabase::AccountTxPageOptions const& options,
    int limit_used,
    std::uint32_t page_length)
{
    return accountTxPage(
        session,
        onUnsavedLedger,
        onTransaction,
        options,
        limit_used,
        page_length,
        true);
}

std::pair<std::optional<RelationalDatabase::AccountTxMarker>, int>
newestAccountTxPage(
    soci::session& session,
    std::function<void(std::uint32_t)> const& onUnsavedLedger,
    std::function<
        void(std::uint32_t, std::string const&, Blob&&, Blob&&)> const&
        onTransaction,
    RelationalDatabase::AccountTxPageOptions const& options,
    int limit_used,
    std::uint32_t page_length)
{
    return accountTxPage(
        session,
        onUnsavedLedger,
        onTransaction,
        options,
        limit_used,
        page_length,
        false);
}

std::variant<RelationalDatabase::AccountTx, TxSearched>
getTransaction(
    soci::session& session,
    Application& app,
    uint256 const& id,
    std::optional<ClosedInterval<uint32_t>> const& range,
    error_code_i& ec)
{
    std::string sql =
        "SELECT LedgerSeq,Status,RawTxn,TxnMeta "
        "FROM Transactions WHERE TransID='";

    sql.append(to_string(id));
    sql.append("';");

    // SOCI requires boost::optional (not std::optional) as parameters.
    boost::optional<std::uint64_t> ledgerSeq;
    boost::optional<std::string> status;
    Blob rawTxn, rawMeta;
    {
        soci::blob sociRawTxnBlob(session), sociRawMetaBlob(session);
        soci::indicator txn, meta;

        session << sql, soci::into(ledgerSeq), soci::into(status),
            soci::into(sociRawTxnBlob, txn), soci::into(sociRawMetaBlob, meta);

        auto const got_data = session.got_data();

        if ((!got_data || txn != soci::i_ok || meta != soci::i_ok) && !range)
            return TxSearched::unknown;

        if (!got_data)
        {
            uint64_t count = 0;
            soci::indicator rti;

            session
                << "SELECT COUNT(DISTINCT LedgerSeq) FROM Transactions WHERE "
                   "LedgerSeq BETWEEN "
                << range->first() << " AND " << range->last() << ";",
                soci::into(count, rti);

            if (!session.got_data() || rti != soci::i_ok)
                return TxSearched::some;

            return count == (range->last() - range->first() + 1)
                ? TxSearched::all
                : TxSearched::some;
        }

        convert(sociRawTxnBlob, rawTxn);
        convert(sociRawMetaBlob, rawMeta);
    }

    try
    {
        auto txn =
            Transaction::transactionFromSQL(ledgerSeq, status, rawTxn, app);

        if (!ledgerSeq)
            return std::pair{std::move(txn), nullptr};

        std::uint32_t inLedger =
            rangeCheckedCast<std::uint32_t>(ledgerSeq.value());

        auto txMeta = std::make_shared<TxMeta>(id, inLedger, rawMeta);

        return std::pair{std::move(txn), std::move(txMeta)};
    }
    catch (std::exception& e)
    {
        JLOG(app.journal("Ledger").warn())
            << "Unable to deserialize transaction from raw SQL value. Error: "
            << e.what();

        ec = rpcDB_DESERIALIZATION;
    }

    return TxSearched::unknown;
}

bool
dbHasSpace(soci::session& session, Config const& config, beast::Journal j)
{
    boost::filesystem::space_info space =
        boost::filesystem::space(config.legacy("database_path"));

    if (space.available < megabytes(512))
    {
        JLOG(j.fatal()) << "Remaining free disk space is less than 512MB";
        return false;
    }

    if (config.useTxTables())
    {
        DatabaseCon::Setup dbSetup = setup_DatabaseCon(config);
        boost::filesystem::path dbPath = dbSetup.dataDir / TxDBName;
        boost::system::error_code ec;
        std::optional<std::uint64_t> dbSize =
            boost::filesystem::file_size(dbPath, ec);
        if (ec)
        {
            JLOG(j.error())
                << "Error checking transaction db file size: " << ec.message();
            dbSize.reset();
        }

        static auto const pageSize = [&] {
            std::uint32_t ps;
            session << "PRAGMA page_size;", soci::into(ps);
            return ps;
        }();
        static auto const maxPages = [&] {
            std::uint32_t mp;
            session << "PRAGMA max_page_count;", soci::into(mp);
            return mp;
        }();
        std::uint32_t pageCount;
        session << "PRAGMA page_count;", soci::into(pageCount);
        std::uint32_t freePages = maxPages - pageCount;
        std::uint64_t freeSpace =
            safe_cast<std::uint64_t>(freePages) * pageSize;
        JLOG(j.info())
            << "Transaction DB pathname: " << dbPath.string()
            << "; file size: " << dbSize.value_or(-1) << " bytes"
            << "; SQLite page size: " << pageSize << " bytes"
            << "; Free pages: " << freePages << "; Free space: " << freeSpace
            << " bytes; "
            << "Note that this does not take into account available disk "
               "space.";

        if (freeSpace < megabytes(512))
        {
            JLOG(j.fatal())
                << "Free SQLite space for transaction db is less than "
                   "512MB. To fix this, rippled must be executed with the "
                   "vacuum parameter before restarting. "
                   "Note that this activity can take multiple days, "
                   "depending on database size.";
            return false;
        }
    }

    return true;
}

}  // namespace detail
}  // namespace ripple
