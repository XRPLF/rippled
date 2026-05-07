#include <xrpld/app/misc/Transaction.h>

#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/rpc/CTID.h>

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/RangeSet.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/safe_cast.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/TxMeta.h>
#include <xrpl/protocol/TxSearched.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/rdb/RelationalDatabase.h>

#include <boost/optional/optional.hpp>  // IWYU pragma: keep

#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>

namespace xrpl {

Transaction::Transaction(
    std::shared_ptr<STTx const> const& stx,
    std::string& reason,
    Application& app) noexcept
    : transaction_(stx), app_(app), j_(app.getJournal("Ledger"))
{
    try
    {
        transactionID_ = transaction_->getTransactionID();
    }
    catch (std::exception& e)
    {
        reason = e.what();
        return;
    }

    status_ = TransStatus::NEW;
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
    status_ = ts;
    ledgerIndex_ = lseq;
    if (tseq)
        txnSeq_ = tseq;
    if (netID)
        networkID_ = netID;
}

TransStatus
Transaction::sqlTransactionStatus(boost::optional<std::string> const& status)
{
    auto const c = (status) ? safeCast<TxnSql>((*status)[0]) : TxnSql::Unknown;

    switch (static_cast<TxnSql>(c))
    {
        case TxnSql::New:
            return TransStatus::NEW;
        case TxnSql::Conflict:
            return TransStatus::CONFLICTED;
        case TxnSql::Held:
            return TransStatus::HELD;
        case TxnSql::Validated:
            return TransStatus::COMMITTED;
        case TxnSql::Included:
            return TransStatus::INCLUDED;
        default:
            XRPL_ASSERT(
                c == TxnSql::Unknown,
                "xrpl::Transaction::sqlTransactionStatus : unknown transaction status");
    }

    return TransStatus::INVALID;
}

Transaction::pointer
Transaction::transactionFromSQL(
    boost::optional<std::uint64_t> const& ledgerSeq,
    boost::optional<std::string> const& status,
    Blob const& rawTxn,
    Application& app)
{
    std::uint32_t const inLedger = rangeCheckedCast<std::uint32_t>(ledgerSeq.value_or(0));

    SerialIter it(makeSlice(rawTxn));
    auto txn = std::make_shared<STTx const>(it);
    std::string reason;
    auto tr = std::make_shared<Transaction>(txn, reason, app);

    tr->setStatus(sqlTransactionStatus(status));
    tr->setLedger(inLedger);
    return tr;
}

std::variant<std::pair<std::shared_ptr<Transaction>, std::shared_ptr<TxMeta>>, TxSearched>
Transaction::load(uint256 const& id, Application& app, ErrorCodeI& ec)
{
    return load(id, app, std::nullopt, ec);
}

std::variant<std::pair<std::shared_ptr<Transaction>, std::shared_ptr<TxMeta>>, TxSearched>
Transaction::load(
    uint256 const& id,
    Application& app,
    ClosedInterval<uint32_t> const& range,
    ErrorCodeI& ec)
{
    using op = std::optional<ClosedInterval<uint32_t>>;

    return load(id, app, op{range}, ec);
}

std::variant<std::pair<std::shared_ptr<Transaction>, std::shared_ptr<TxMeta>>, TxSearched>
Transaction::load(
    uint256 const& id,
    Application& app,
    std::optional<ClosedInterval<uint32_t>> const& range,
    ErrorCodeI& ec)
{
    auto& db = app.getRelationalDatabase();

    return db.getTransaction(id, range, ec);
}

// options 1 to include the date of the transaction
json::Value
Transaction::getJson(JsonOptions options, bool binary) const
{
    // Note, we explicitly suppress `include_date` option here
    json::Value ret(transaction_->getJson(options & ~JsonOptions::KIncludeDate, binary));

    // NOTE Binary STTx::getJson output might not be a JSON object
    if (ret.isObject() && (ledgerIndex_ != 0u))
    {
        if (!(options & JsonOptions::KDisableApiPriorV2))
        {
            // Behaviour before API version 2
            ret[jss::inLedger] = ledgerIndex_;
        }

        // TODO: disable_API_prior_V3 to disable output of both `date` and
        // `ledger_index` elements (taking precedence over include_date)
        ret[jss::ledger_index] = ledgerIndex_;

        if (options & JsonOptions::KIncludeDate)
        {
            auto ct = app_.getLedgerMaster().getCloseTimeBySeq(ledgerIndex_);
            if (ct)
                ret[jss::date] = ct->time_since_epoch().count();
        }

        // compute outgoing CTID
        // override local network id if it's explicitly in the txn
        std::optional netID = networkID_;
        if (transaction_->isFieldPresent(sfNetworkID))
            netID = transaction_->getFieldU32(sfNetworkID);

        if (txnSeq_ && netID)
        {
            std::optional<std::string> const ctid = RPC::encodeCTID(ledgerIndex_, *txnSeq_, *netID);
            if (ctid)
                ret[jss::ctid] = *ctid;
        }
    }

    return ret;
}

}  // namespace xrpl
