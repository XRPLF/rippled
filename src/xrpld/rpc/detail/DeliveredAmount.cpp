#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/ledger/OpenLedger.h>
#include <xrpld/app/misc/Transaction.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/DeliveredAmount.h>

#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/RPCErr.h>

namespace ripple {
namespace RPC {

/*
  GetLedgerIndex and GetCloseTime are lambdas that allow the close time and
  ledger index to be lazily calculated. Without these lambdas, these values
  would be calculated even when not needed, and in some circumstances they are
  not trivial to compute.

  GetLedgerIndex is a callable that returns a LedgerIndex
  GetCloseTime is a callable that returns a
               std::optional<NetClock::time_point>
 */
template <class GetLedgerIndex, class GetCloseTime>
std::optional<STAmount>
getDeliveredAmount(
    GetLedgerIndex const& getLedgerIndex,
    GetCloseTime const& getCloseTime,
    std::shared_ptr<STTx const> const& serializedTx,
    TxMeta const& transactionMeta)
{
    if (!serializedTx)
        return {};

    if (auto const& deliveredAmount = transactionMeta.getDeliveredAmount();
        deliveredAmount.has_value())
    {
        return *deliveredAmount;
    }

    if (serializedTx->isFieldPresent(sfAmount))
    {
        using namespace std::chrono_literals;

        // Ledger 4594095 is the first ledger in which the DeliveredAmount field
        // was present when a partial payment was made and its absence indicates
        // that the amount delivered is listed in the Amount field.
        //
        // If the ledger closed long after the DeliveredAmount code was deployed
        // then its absence indicates that the amount delivered is listed in the
        // Amount field. DeliveredAmount went live January 24, 2014.
        // 446000000 is in Feb 2014, well after DeliveredAmount went live
        if (getLedgerIndex() >= 4594095 ||
            getCloseTime() > NetClock::time_point{446000000s})
        {
            return serializedTx->getFieldAmount(sfAmount);
        }
    }

    return {};
}

// Returns true if transaction meta could contain a delivered amount field,
// based on transaction type and transaction result
bool
canHaveDeliveredAmount(
    std::shared_ptr<STTx const> const& serializedTx,
    TxMeta const& transactionMeta)
{
    if (!serializedTx)
        return false;

    TxType const tt{serializedTx->getTxnType()};
    // Transaction type should be ttPAYMENT, ttACCOUNT_DELETE or ttCHECK_CASH
    // and if the transaction failed nothing could have been delivered.
    if ((tt == ttPAYMENT || tt == ttCHECK_CASH || tt == ttACCOUNT_DELETE) &&
        transactionMeta.getResultTER() == tesSUCCESS)
    {
        return true;
    }

    return false;
}

void
insertDeliveredAmount(
    Json::Value& meta,
    ReadView const& ledger,
    std::shared_ptr<STTx const> const& serializedTx,
    TxMeta const& transactionMeta)
{
    auto const info = ledger.info();

    if (canHaveDeliveredAmount(serializedTx, transactionMeta))
    {
        auto const getLedgerIndex = [&info] { return info.seq; };
        auto const getCloseTime = [&info] { return info.closeTime; };

        auto amt = getDeliveredAmount(
            getLedgerIndex, getCloseTime, serializedTx, transactionMeta);
        if (amt)
        {
            meta[jss::delivered_amount] =
                amt->getJson(JsonOptions::include_date);
        }
        else
        {
            // report "unavailable" which cannot be parsed into a sensible
            // amount.
            meta[jss::delivered_amount] = Json::Value("unavailable");
        }
    }
}

template <class GetLedgerIndex>
static std::optional<STAmount>
getDeliveredAmount(
    RPC::Context const& context,
    std::shared_ptr<STTx const> const& serializedTx,
    TxMeta const& transactionMeta,
    GetLedgerIndex const& getLedgerIndex)
{
    if (canHaveDeliveredAmount(serializedTx, transactionMeta))
    {
        auto const getCloseTime =
            [&context,
             &getLedgerIndex]() -> std::optional<NetClock::time_point> {
            return context.ledgerMaster.getCloseTimeBySeq(getLedgerIndex());
        };
        return getDeliveredAmount(
            getLedgerIndex, getCloseTime, serializedTx, transactionMeta);
    }

    return {};
}

std::optional<STAmount>
getDeliveredAmount(
    RPC::Context const& context,
    std::shared_ptr<STTx const> const& serializedTx,
    TxMeta const& transactionMeta,
    LedgerIndex const& ledgerIndex)
{
    return getDeliveredAmount(
        context, serializedTx, transactionMeta, [&ledgerIndex]() {
            return ledgerIndex;
        });
}

void
insertDeliveredAmount(
    Json::Value& meta,
    RPC::JsonContext const& context,
    std::shared_ptr<Transaction> const& transaction,
    TxMeta const& transactionMeta)
{
    insertDeliveredAmount(
        meta, context, transaction->getSTransaction(), transactionMeta);
}

void
insertDeliveredAmount(
    Json::Value& meta,
    RPC::JsonContext const& context,
    std::shared_ptr<STTx const> const& transaction,
    TxMeta const& transactionMeta)
{
    if (canHaveDeliveredAmount(transaction, transactionMeta))
    {
        auto amt = getDeliveredAmount(
            context, transaction, transactionMeta, [&transactionMeta]() {
                return transactionMeta.getLgrSeq();
            });

        if (amt)
        {
            meta[jss::delivered_amount] =
                amt->getJson(JsonOptions::include_date);
        }
        else
        {
            // report "unavailable" which cannot be parsed into a sensible
            // amount.
            meta[jss::delivered_amount] = Json::Value("unavailable");
        }
    }
}

}  // namespace RPC
}  // namespace ripple
