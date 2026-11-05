#include <xrpl/tx/transactors/perp/PerpRefresh.h>

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/PerpHelpers.h>
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
PerpRefresh::getFlagsMask(PreflightContext const& ctx)
{
    return tfPerpRefreshMask;
}

NotTEC
PerpRefresh::preflight(PreflightContext const& ctx)
{
    auto const& tx = ctx.tx;
    auto const marketID = tx[~sfMarketID];
    auto const marginAccountID = tx[~sfMarginAccountID];
    if (!marketID && !marginAccountID)
        return temMALFORMED;

    if ((marketID && *marketID == beast::kZero) ||
        (marginAccountID && *marginAccountID == beast::kZero))
        return temMALFORMED;

    // Settling a stale market needs the market.
    if (tx.isFlag(tfPerpSettleStale) && !marketID)
        return temMALFORMED;

    return tesSUCCESS;
}

TER
PerpRefresh::preclaim(PreclaimContext const& ctx)
{
    auto const& tx = ctx.tx;
    if (auto const marketID = tx[~sfMarketID];
        marketID && !ctx.view.exists(keylet::perpMarket(*marketID)))
        return tecNO_ENTRY;
    if (auto const accountID = tx[~sfMarginAccountID];
        accountID && !ctx.view.exists(keylet::marginAccount(*accountID)))
        return tecNO_ENTRY;
    return tesSUCCESS;
}

TER
PerpRefresh::doApply()
{
    auto const& tx = ctx_.tx;
    if (auto const marketID = tx[~sfMarketID])
    {
        auto const market = view().peek(keylet::perpMarket(*marketID));
        if (!market)
            return tefINTERNAL;  // LCOV_EXCL_LINE
        auto const exchange = view().peek(keylet::perpExchange((*market)[sfExchangeID]));
        if (!exchange)
            return tefINTERNAL;  // LCOV_EXCL_LINE
        perp::refreshMarket(view(), market, exchange, j_);

        // A market Stale for kSettleStaleAfter settles at its last quorum index (spec 10).
        if (tx.isFlag(tfPerpSettleStale))
        {
            std::uint8_t const status = (*market)[sfMarketStatus];
            if (status == perp::kMarketSettled)
                return tecPERP_MARKET_SETTLED;
            if (status != perp::kMarketStale)
                return tecNO_PERMISSION;
            auto const now = perp::refreshTime(view());
            if (std::uint64_t{now} <
                std::uint64_t{(*market)[sfStatusTime]} + perp::kSettleStaleAfter.count())
                return tecTOO_SOON;
            perp::settleMarket(*market, now);
            view().update(market);
        }
    }

    if (auto const accountID = tx[~sfMarginAccountID])
    {
        auto const account = view().peek(keylet::marginAccount(*accountID));
        if (!account)
            return tefINTERNAL;  // LCOV_EXCL_LINE
        auto const exchange = view().peek(keylet::perpExchange((*account)[sfExchangeID]));
        if (!exchange)
            return tefINTERNAL;  // LCOV_EXCL_LINE
        return perp::settleAccount(view(), account, exchange, j_);
    }
    return tesSUCCESS;
}

void
PerpRefresh::visitInvariantEntry(bool, SLE::ConstRef, SLE::ConstRef)
{
}

bool
PerpRefresh::finalizeInvariants(STTx const&, TER, XRPAmount, ReadView const&, beast::Journal const&)
{
    return true;
}

}  // namespace xrpl
