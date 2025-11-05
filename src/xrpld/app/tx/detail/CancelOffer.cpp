#include <xrpld/app/tx/detail/CancelOffer.h>

#include <xrpl/basics/Log.h>
#include <xrpl/ledger/View.h>
#include <xrpl/protocol/st.h>

namespace ripple {

NotTEC
CancelOffer::preflight(PreflightContext const& ctx)
{
    if (!ctx.tx[sfOfferSequence])
    {
        JLOG(ctx.j.trace()) << "CancelOffer::preflight: missing sequence";
        return temBAD_SEQUENCE;
    }

    return tesSUCCESS;
}

//------------------------------------------------------------------------------

TER
CancelOffer::preclaim(PreclaimContext const& ctx)
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
CancelOffer::doApply()
{
    auto const offerSequence = ctx_.tx[sfOfferSequence];

    auto const sle = view().read(keylet::account(account_));
    if (!sle)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    if (auto sleOffer = view().peek(keylet::offer(account_, offerSequence)))
    {
        JLOG(j_.debug()) << "Trying to cancel offer #" << offerSequence;
        return offerDelete(view(), sleOffer, ctx_.app.journal("View"));
    }

    JLOG(j_.debug()) << "Offer #" << offerSequence << " can't be found.";
    return tesSUCCESS;
}

}  // namespace ripple
