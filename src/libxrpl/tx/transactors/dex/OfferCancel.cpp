#include <xrpl/basics/Log.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/OfferHelpers.h>
#include <xrpl/protocol/st.h>
#include <xrpl/tx/transactors/dex/OfferCancel.h>

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

    auto const sle = view().read(keylet::account(account_));
    if (!sle)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    if (auto sleOffer = view().peek(keylet::offer(account_, offerSequence)))
    {
        JLOG(j_.debug()) << "Trying to cancel offer #" << offerSequence;
        return offerDelete(view(), sleOffer, ctx_.registry.journal("View"));
    }

    JLOG(j_.debug()) << "Offer #" << offerSequence << " can't be found.";
    return tesSUCCESS;
}

}  // namespace xrpl
