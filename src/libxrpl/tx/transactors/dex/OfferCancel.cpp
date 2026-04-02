#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
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

    AccountRoot const acct(id, ctx.view);
    if (!acct)
        return terNO_ACCOUNT;

    if (acct->getFieldU32(sfSequence) <= offerSequence)
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

    if (AccountRoot const acct(accountID_, view()); !acct)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    if (auto sleOffer = view().peek(keylet::offer(accountID_, offerSequence)))
    {
        JLOG(j_.debug()) << "Trying to cancel offer #" << offerSequence;
        return offerDelete(view(), sleOffer, ctx_.registry.get().getJournal("View"));
    }

    JLOG(j_.debug()) << "Offer #" << offerSequence << " can't be found.";
    return tesSUCCESS;
}

}  // namespace xrpl
