/** @file
 *  Implements `delivered_amount` resolution and injection for RPC transaction
 *  metadata responses.
 *
 *  The `GetLedgerIndex` and `GetCloseTime` callables used throughout this file
 *  are intentionally lazy: ledger index and close time are only evaluated when
 *  the historical fallback branch is reached. In the common case — a modern
 *  ledger whose `TxMeta` already contains `sfDeliveredAmount` — neither value
 *  is ever computed, avoiding a potentially expensive `LedgerMaster` lookup by
 *  sequence number.
 */

#include <xrpld/rpc/DeliveredAmount.h>

#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/misc/Transaction.h>
#include <xrpld/rpc/Context.h>

#include <xrpl/basics/chrono.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/jss.h>

#include <chrono>
#include <memory>
#include <optional>

namespace xrpl::RPC {

/** Resolve the actual delivered amount for a transaction using lazy accessors.
 *
 *  Implements the three-tier resolution strategy:
 *  1. Return `TxMeta::getDeliveredAmount()` directly if present — authoritative
 *     for all ledgers from sequence 4594095 onward.
 *  2. If the metadata field is absent but the ledger postdates the
 *     `DeliveredAmount` deployment (index >= 4594095 **or** close time >
 *     446000000s ≈ February 2014), return the transaction's `sfAmount` — a
 *     missing field in a post-deployment ledger means full delivery.
 *  3. Return `std::nullopt` for pre-deployment ledgers where the delivered
 *     amount cannot be determined; callers emit the `"unavailable"` sentinel.
 *
 *  @tparam GetLedgerIndex Callable with signature `LedgerIndex ()`.
 *  @tparam GetCloseTime   Callable with signature
 *      `std::optional<NetClock::time_point> ()`.
 *  @param getLedgerIndex Lazy accessor for the ledger sequence number; only
 *      called when `transactionMeta` lacks `sfDeliveredAmount`.
 *  @param getCloseTime   Lazy accessor for the ledger close time; only called
 *      when `transactionMeta` lacks `sfDeliveredAmount` and the ledger index
 *      alone does not cross the historical threshold.
 *  @param serializedTx   The transaction to inspect; returns `std::nullopt`
 *      immediately if null.
 *  @param transactionMeta Metadata for `serializedTx`; the primary source for
 *      `sfDeliveredAmount`.
 *  @return The resolved delivered amount, or `std::nullopt` if the ledger
 *      predates the `DeliveredAmount` feature and no authoritative value exists.
 *  @note Both threshold conditions (`getLedgerIndex() >= 4594095` and
 *      `getCloseTime() > 446000000s`) identify the same era; the close-time
 *      check is a fallback for ledgers whose sequence alone is ambiguous.
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
        return deliveredAmount;
    }

    if (serializedTx->isFieldPresent(sfAmount))
    {
        using namespace std::chrono_literals;

        if (getLedgerIndex() >= 4594095 || getCloseTime() > NetClock::time_point{446000000s})
        {
            return serializedTx->getFieldAmount(sfAmount);
        }
    }

    return {};
}

/** Determine whether a transaction can carry a `delivered_amount` field.
 *
 *  Acts as a cheap type-and-result gate before the more expensive amount
 *  resolution logic runs. Only `ttPAYMENT`, `ttCHECK_CASH`, and
 *  `ttACCOUNT_DELETE` transactions that completed with `tesSUCCESS` are
 *  eligible; failed transactions deliver nothing and must not have the field.
 *
 *  @param serializedTx   The transaction to test; returns `false` if null.
 *  @param transactionMeta Metadata carrying the transaction result code.
 *  @return `true` if the transaction type and result allow a
 *      `delivered_amount` field to be present in the metadata.
 */
bool
canHaveDeliveredAmount(
    std::shared_ptr<STTx const> const& serializedTx,
    TxMeta const& transactionMeta)
{
    if (!serializedTx)
        return false;

    TxType const tt{serializedTx->getTxnType()};
    return (tt == ttPAYMENT || tt == ttCHECK_CASH || tt == ttACCOUNT_DELETE) &&
        transactionMeta.getResultTER() == tesSUCCESS;
}

void
insertDeliveredAmount(
    json::Value& meta,
    ReadView const& ledger,
    std::shared_ptr<STTx const> const& serializedTx,
    TxMeta const& transactionMeta)
{
    auto const info = ledger.header();

    if (canHaveDeliveredAmount(serializedTx, transactionMeta))
    {
        auto const getLedgerIndex = [&info] { return info.seq; };
        auto const getCloseTime = [&info] { return info.closeTime; };

        auto amt = getDeliveredAmount(getLedgerIndex, getCloseTime, serializedTx, transactionMeta);
        if (amt)
        {
            meta[jss::delivered_amount] = amt->getJson(JsonOptions::Values::IncludeDate);
        }
        else
        {
            // report "unavailable" which cannot be parsed into a sensible
            // amount.
            meta[jss::delivered_amount] = json::Value("unavailable");
        }
    }
}

/** Internal bridge: gates on eligibility, then calls the lazy-accessor overload.
 *
 *  Constructs a `getCloseTime` lambda that delegates to
 *  `context.ledgerMaster.getCloseTimeBySeq()`, keeping the potentially
 *  expensive DB lookup deferred until the historical-fallback branch is
 *  actually reached. Returns `std::nullopt` immediately for ineligible
 *  transactions without touching `LedgerMaster` at all.
 *
 *  @tparam GetLedgerIndex Callable with signature `LedgerIndex ()`.
 *  @param context       RPC context; `context.ledgerMaster` is used only
 *      when the close-time lazy lambda is invoked.
 *  @param serializedTx  The transaction to resolve.
 *  @param transactionMeta Metadata for `serializedTx`.
 *  @param getLedgerIndex Lazy accessor for the ledger sequence number.
 *  @return The resolved delivered amount, or `std::nullopt` if ineligible or
 *      the ledger predates the `DeliveredAmount` feature.
 */
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
        auto const getCloseTime = [&context,
                                   &getLedgerIndex]() -> std::optional<NetClock::time_point> {
            return context.ledgerMaster.getCloseTimeBySeq(getLedgerIndex());
        };
        return getDeliveredAmount(getLedgerIndex, getCloseTime, serializedTx, transactionMeta);
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
        context, serializedTx, transactionMeta, [&ledgerIndex]() { return ledgerIndex; });
}

void
insertDeliveredAmount(
    json::Value& meta,
    RPC::JsonContext const& context,
    std::shared_ptr<Transaction> const& transaction,
    TxMeta const& transactionMeta)
{
    insertDeliveredAmount(meta, context, transaction->getSTransaction(), transactionMeta);
}

void
insertDeliveredAmount(
    json::Value& meta,
    RPC::JsonContext const& context,
    std::shared_ptr<STTx const> const& transaction,
    TxMeta const& transactionMeta)
{
    if (canHaveDeliveredAmount(transaction, transactionMeta))
    {
        auto amt = getDeliveredAmount(context, transaction, transactionMeta, [&transactionMeta]() {
            return transactionMeta.getLgrSeq();
        });

        if (amt)
        {
            meta[jss::delivered_amount] = amt->getJson(JsonOptions::Values::IncludeDate);
        }
        else
        {
            // report "unavailable" which cannot be parsed into a sensible
            // amount.
            meta[jss::delivered_amount] = json::Value("unavailable");
        }
    }
}

}  // namespace xrpl::RPC
