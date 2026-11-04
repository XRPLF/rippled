#include <xrpl/tx/transactors/perp/PerpMarketDelete.h>

#include <xrpl/basics/Number.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>  // IWYU pragma: keep
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

#include <cstdint>

namespace xrpl {

std::uint32_t
PerpMarketDelete::getFlagsMask(PreflightContext const& ctx)
{
    return tfUniversalMask;
}

NotTEC
PerpMarketDelete::preflight(PreflightContext const& ctx)
{
    if (ctx.tx.isFlag(tfInnerBatchTxn))
        return temINVALID_INNER_BATCH;

    if (ctx.tx[sfMarketID] == beast::kZero)
        return temMALFORMED;

    return tesSUCCESS;
}

TER
PerpMarketDelete::preclaim(PreclaimContext const& ctx)
{
    auto const market = ctx.view.read(keylet::perpMarket(ctx.tx[sfMarketID]));
    if (!market)
        return tecNO_ENTRY;
    auto const exchange = ctx.view.read(keylet::perpExchange((*market)[sfExchangeID]));
    if (!exchange)
        return tefINTERNAL;  // LCOV_EXCL_LINE
    if ((*exchange)[sfOwner] != ctx.tx[sfAccount])
        return tecNO_PERMISSION;

    // Positions are deleted once flat with no orders, so no open interest and no orders means
    // no position references the market.
    if ((*market)[sfOpenInterestLong] != Number{} || (*market)[sfOpenInterestShort] != Number{} ||
        (*market)[sfOrderCount] != 0)
        return tecHAS_OBLIGATIONS;

    return tesSUCCESS;
}

TER
PerpMarketDelete::doApply()
{
    auto const market = view().peek(keylet::perpMarket(ctx_.tx[sfMarketID]));
    if (!market)
        return tefINTERNAL;  // LCOV_EXCL_LINE
    auto const exchange = view().peek(keylet::perpExchange((*market)[sfExchangeID]));
    auto const owner = view().peek(keylet::account(accountID_));
    if (!exchange || !owner)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    if (!view().dirRemove(
            keylet::ownerDir(accountID_), (*market)[sfOwnerNode], market->key(), false))
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE
    decreaseOwnerCountForObject(view(), owner, market, 1, j_);

    (*exchange)[sfMarketCount] = (*exchange)[sfMarketCount] - 1;
    view().update(exchange);
    view().erase(market);
    return tesSUCCESS;
}

void
PerpMarketDelete::visitInvariantEntry(bool, SLE::ConstRef, SLE::ConstRef)
{
}

bool
PerpMarketDelete::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    return true;
}

}  // namespace xrpl
