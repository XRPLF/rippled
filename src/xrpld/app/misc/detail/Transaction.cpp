#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/app/misc/HashRouter.h>
#include <xrpld/app/misc/Transaction.h>
#include <xrpld/app/rdb/backend/SQLiteDatabase.h>
#include <xrpld/app/tx/apply.h>
#include <xrpld/rpc/CTID.h>

#include <xrpl/basics/safe_cast.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/jss.h>

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
Transaction::setStatus(
    TransStatus ts,
    std::uint32_t lseq,
    std::optional<std::uint32_t> tseq,
    std::optional<std::uint32_t> netID)
{
    mStatus = ts;
    mLedgerIndex = lseq;
    if (tseq)
        mTxnSeq = tseq;
    if (netID)
        mNetworkID = netID;
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

    XRPL_ASSERT(
        c == txnSqlUnknown,
        "ripple::Transaction::sqlTransactionStatus : unknown transaction "
        "status");
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

std::variant<
    std::pair<std::shared_ptr<Transaction>, std::shared_ptr<TxMeta>>,
    TxSearched>
Transaction::load(
    uint256 const& id,
    Application& app,
    std::optional<ClosedInterval<uint32_t>> const& range,
    error_code_i& ec)
{
    auto const db = dynamic_cast<SQLiteDatabase*>(&app.getRelationalDatabase());

    if (!db)
    {
        Throw<std::runtime_error>("Failed to get relational database");
    }

    return db->getTransaction(id, range, ec);
}

// options 1 to include the date of the transaction
Json::Value
Transaction::getJson(JsonOptions options, bool binary) const
{
    // Note, we explicitly suppress `include_date` option here
    Json::Value ret(
        mTransaction->getJson(options & ~JsonOptions::include_date, binary));

    // NOTE Binary STTx::getJson output might not be a JSON object
    if (ret.isObject() && mLedgerIndex)
    {
        if (!(options & JsonOptions::disable_API_prior_V2))
        {
            // Behaviour before API version 2
            ret[jss::inLedger] = mLedgerIndex;
        }

        // TODO: disable_API_prior_V3 to disable output of both `date` and
        // `ledger_index` elements (taking precedence over include_date)
        ret[jss::ledger_index] = mLedgerIndex;

        if (options & JsonOptions::include_date)
        {
            auto ct = mApp.getLedgerMaster().getCloseTimeBySeq(mLedgerIndex);
            if (ct)
                ret[jss::date] = ct->time_since_epoch().count();
        }

        // compute outgoing CTID
        // override local network id if it's explicitly in the txn
        std::optional netID = mNetworkID;
        if (mTransaction->isFieldPresent(sfNetworkID))
            netID = mTransaction->getFieldU32(sfNetworkID);

        if (mTxnSeq && netID)
        {
            std::optional<std::string> const ctid =
                RPC::encodeCTID(mLedgerIndex, *mTxnSeq, *netID);
            if (ctid)
                ret[jss::ctid] = *ctid;
        }
    }

    return ret;
}

}  // namespace ripple
