#include <xrpl/tx/transactors/perp/PerpOrderCancel.h>

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/PerpHelpers.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

#include <cstdint>

namespace xrpl {

std::uint32_t
PerpOrderCancel::getFlagsMask(PreflightContext const& ctx)
{
    return tfUniversalMask;
}

NotTEC
PerpOrderCancel::preflight(PreflightContext const& ctx)
{
    if (ctx.tx[sfOfferSequence] == 0)
        return temBAD_SEQUENCE;

    if (auto const owner = ctx.tx[~sfOwner]; owner && *owner == beast::kZero)
        return temMALFORMED;

    return tesSUCCESS;
}

TER
PerpOrderCancel::preclaim(PreclaimContext const& ctx)
{
    auto const& tx = ctx.tx;
    auto const account = tx[sfAccount];
    auto const owner = tx[~sfOwner].value_or(account);
    auto const order =
        ctx.view.read(keylet::perpOrder(owner, SeqProxy::rawSequence(tx[sfOfferSequence])));
    if (!order)
        return tecNO_ENTRY;

    // Another owner's order may be cancelled once expired or once its market is Settled.
    if (owner != account && !hasExpired(ctx.view, (*order)[~sfExpiration]))
    {
        auto const market = ctx.view.read(keylet::perpMarket((*order)[sfMarketID]));
        if (!market || (*market)[sfMarketStatus] != perp::kMarketSettled)
            return tecNO_PERMISSION;
    }
    return tesSUCCESS;
}

TER
PerpOrderCancel::doApply()
{
    auto const& tx = ctx_.tx;
    auto const order = view().peek(
        keylet::perpOrder(
            tx[~sfOwner].value_or(accountID_), SeqProxy::rawSequence(tx[sfOfferSequence])));
    if (!order)
        return tefINTERNAL;  // LCOV_EXCL_LINE
    return perp::removeOrder(view(), order, j_);
}

void
PerpOrderCancel::visitInvariantEntry(bool, SLE::ConstRef, SLE::ConstRef)
{
}

bool
PerpOrderCancel::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    return true;
}

}  // namespace xrpl
