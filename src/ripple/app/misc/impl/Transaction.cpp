//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/main/Application.h>
#include <ripple/app/misc/HashRouter.h>
#include <ripple/app/misc/Transaction.h>
#include <ripple/app/tx/apply.h>
#include <ripple/basics/Log.h>
#include <ripple/basics/safe_cast.h>
#include <ripple/core/DatabaseCon.h>
#include <ripple/core/Pg.h>
#include <ripple/json/json_reader.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/jss.h>

namespace ripple {

Transaction::Transaction(
    std::shared_ptr<STTx const> const& stx,
    std::string& reason,
    Application& app) noexcept
    : mTransaction(stx), mApp(app), j_(app.journal("Ledger"))
{
    try
    {
        mTransactionID = mTransaction->getTransactionID();
    }
    catch (std::exception& e)
    {
        reason = e.what();
        return;
    }

    mStatus = NEW;
}

//
// Misc.
//

void
Transaction::setStatus(TransStatus ts, std::uint32_t lseq)
{
    mStatus = ts;
    mInLedger = lseq;
}

TransStatus
Transaction::sqlTransactionStatus(boost::optional<std::string> const& status)
{
    char const c = (status) ? (*status)[0] : safe_cast<char>(txnSqlUnknown);

    switch (c)
    {
        case txnSqlNew:
            return NEW;
        case txnSqlConflict:
            return CONFLICTED;
        case txnSqlHeld:
            return HELD;
        case txnSqlValidated:
            return COMMITTED;
        case txnSqlIncluded:
            return INCLUDED;
    }

    assert(c == txnSqlUnknown);
    return INVALID;
}

Transaction::pointer
Transaction::transactionFromSQL(
    boost::optional<std::uint64_t> const& ledgerSeq,
    boost::optional<std::string> const& status,
    Blob const& rawTxn,
    Application& app)
{
    std::uint32_t const inLedger =
        rangeCheckedCast<std::uint32_t>(ledgerSeq.value_or(0));

    SerialIter it(makeSlice(rawTxn));
    auto txn = std::make_shared<STTx const>(it);
    std::string reason;
    auto tr = std::make_shared<Transaction>(txn, reason, app);

    tr->setStatus(sqlTransactionStatus(status));
    tr->setLedger(inLedger);
    return tr;
}

std::variant<
    std::pair<std::shared_ptr<Transaction>, std::shared_ptr<TxMeta>>,
    TxSearched>
Transaction::load(uint256 const& id, Application& app, error_code_i& ec)
{
    return load(id, app, std::nullopt, ec);
}

std::variant<
    std::pair<std::shared_ptr<Transaction>, std::shared_ptr<TxMeta>>,
    TxSearched>
Transaction::load(
    uint256 const& id,
    Application& app,
    ClosedInterval<uint32_t> const& range,
    error_code_i& ec)
{
    using op = std::optional<ClosedInterval<uint32_t>>;

    return load(id, app, op{range}, ec);
}

Transaction::Locator
Transaction::locate(uint256 const& id, Application& app)
{
#ifdef RIPPLED_REPORTING
    auto baseCmd = boost::format(R"(SELECT tx('%s');)");

    std::string txHash = "\\x" + strHex(id);
    std::string sql = boost::str(baseCmd % txHash);

    auto res = PgQuery(app.getPgPool())(sql.data());

    if (!res)
    {
        JLOG(app.journal("Transaction").error())
            << __func__
            << " : Postgres response is null - tx ID = " << strHex(id);
        assert(false);
        return {};
    }
    else if (res.status() != PGRES_TUPLES_OK)
    {
        JLOG(app.journal("Transaction").error())
            << __func__
            << " : Postgres response should have been "
               "PGRES_TUPLES_OK but instead was "
            << res.status() << " - msg  = " << res.msg()
            << " - tx ID = " << strHex(id);
        assert(false);
        return {};
    }

    JLOG(app.journal("Transaction").trace())
        << __func__ << " Postgres result msg  : " << res.msg();
    if (res.isNull() || res.ntuples() == 0)
    {
        JLOG(app.journal("Transaction").debug())
            << __func__
            << " : No data returned from Postgres : tx ID = " << strHex(id);
        // This shouldn't happen
        assert(false);
        return {};
    }

    char const* resultStr = res.c_str();
    JLOG(app.journal("Transaction").debug())
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
}

std::variant<
    std::pair<std::shared_ptr<Transaction>, std::shared_ptr<TxMeta>>,
    TxSearched>
Transaction::load(
    uint256 const& id,
    Application& app,
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
        auto db = app.getTxnDB().checkoutDb();
        soci::blob sociRawTxnBlob(*db), sociRawMetaBlob(*db);
        soci::indicator txn, meta;

        *db << sql, soci::into(ledgerSeq), soci::into(status),
            soci::into(sociRawTxnBlob, txn), soci::into(sociRawMetaBlob, meta);

        auto const got_data = db->got_data();

        if ((!got_data || txn != soci::i_ok || meta != soci::i_ok) && !range)
            return TxSearched::unknown;

        if (!got_data)
        {
            uint64_t count = 0;
            soci::indicator rti;

            *db << "SELECT COUNT(DISTINCT LedgerSeq) FROM Transactions WHERE "
                   "LedgerSeq BETWEEN "
                << range->first() << " AND " << range->last() << ";",
                soci::into(count, rti);

            if (!db->got_data() || rti != soci::i_ok)
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

// options 1 to include the date of the transaction
Json::Value
Transaction::getJson(JsonOptions options, bool binary) const
{
    Json::Value ret(mTransaction->getJson(JsonOptions::none, binary));

    if (mInLedger)
    {
        ret[jss::inLedger] = mInLedger;  // Deprecated.
        ret[jss::ledger_index] = mInLedger;

        if (options == JsonOptions::include_date)
        {
            auto ct = mApp.getLedgerMaster().getCloseTimeBySeq(mInLedger);
            if (ct)
                ret[jss::date] = ct->time_since_epoch().count();
        }
    }

    return ret;
}

}  // namespace ripple
