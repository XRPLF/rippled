#include <xrpl/tx/transactors/dex/OfferCancel.h>

#include <xrpl/basics/Log.h>
#include <xrpl/ledger/helpers/OfferHelpers.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

namespace xrpl {

NotTEC
OfferCancel::preflight(PreflightContext const& ctx)
{
    if (ctx.tx[sfOfferSequence] == 0u)
    {
        JLOG(ctx.j.trace()) << "OfferCancel::preflight: missing sequence";
        return temBAD_SEQUENCE;
    }

    return tesSUCCESS;
}

//------------------------------------------------------------------------------

TER
OfferCancel::preclaim(PreclaimContext const& ctx)
{
    auto const id = ctx.tx[sfAccount];
    auto const offerSequence = ctx.tx[sfOfferSequence];

    auto const sle = ctx.view.read(keylet::account(id));
    if (!sle)
        return terNO_ACCOUNT;

    if ((*sle)[sfSequence] <= offerSequence)
    {
        JLOG(ctx.j.trace()) << "Malformed transaction: "
                            << "Sequence " << offerSequence << " is invalid.";
        return temBAD_SEQUENCE;
    }

    return tesSUCCESS;
}

//------------------------------------------------------------------------------

TER
OfferCancel::doApply()
{
    auto const offerSequence = ctx_.tx[sfOfferSequence];

    auto const sle = view().read(keylet::account(accountID_));
    if (!sle)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    if (auto sleOffer = view().peek(keylet::offer(accountID_, offerSequence)))
    {
        JLOG(j_.debug()) << "Trying to cancel offer #" << offerSequence;
        return offerDelete(view(), sleOffer, ctx_.registry.get().getJournal("View"));
    }

    JLOG(j_.debug()) << "Offer #" << offerSequence << " can't be found.";
    return tesSUCCESS;
}

void
OfferCancel::visitInvariantEntry(bool, SLE::const_ref, SLE::const_ref)
{
    // No transaction-specific invariants yet (future work).
}

bool
OfferCancel::finalizeInvariants(STTx const&, TER, XRPAmount, ReadView const&, beast::Journal const&)
{
    // No transaction-specific invariants yet (future work).
    return true;
}

}  // namespace xrpl
