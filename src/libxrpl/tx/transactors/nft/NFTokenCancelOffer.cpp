#include <xrpl/tx/transactors/nft/NFTokenCancelOffer.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/NFTokenHelpers.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/STVector256.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

#include <algorithm>
namespace xrpl {

NotTEC
NFTokenCancelOffer::preflight(PreflightContext const& ctx)
{
    auto const& offerIds = ctx.tx[sfNFTokenOffers];

    if (offerIds.empty() || (offerIds.size() > kMaxTokenOfferCancelCount))
        return temMALFORMED;

    // Zero offer IDs cannot be passed as ledger entry keys.
    if (ctx.rules.enabled(fixCleanup3_2_0) &&
        std::ranges::any_of(offerIds, [](uint256 const& id) { return id.isZero(); }))
        return temMALFORMED;

    // In order to prevent unnecessarily overlarge transactions, we
    // disallow duplicates in the list of offers to cancel.
    STVector256 ids = ctx.tx.getFieldV256(sfNFTokenOffers);
    std::ranges::sort(ids);
    if (std::ranges::adjacent_find(ids) != ids.end())
        return temMALFORMED;

    return tesSUCCESS;
}

TER
NFTokenCancelOffer::preclaim(PreclaimContext const& ctx)
{
    auto const account = ctx.tx[sfAccount];

    auto const& ids = ctx.tx[sfNFTokenOffers];

    auto ret = std::ranges::find_if(ids, [&ctx, &account](uint256 const& id) {
        auto const offer = ctx.view.read(keylet::child(id));

        // If id is not in the ledger we assume the offer was consumed
        // before we got here.
        if (!offer)
            return false;

        // If id is in the ledger but is not an NFTokenOffer, then
        // they have no permission.
        if (offer->getType() != ltNFTOKEN_OFFER)
            return true;

        // Anyone can cancel, if expired
        if (hasExpired(ctx.view, (*offer)[~sfExpiration]))
            return false;

        // The owner can always cancel
        if ((*offer)[sfOwner] == account)
            return false;

        // The recipient can always cancel
        if (auto const dest = (*offer)[~sfDestination]; dest == account)
            return false;

        return true;
    });

    if (ret != ids.end())
        return tecNO_PERMISSION;

    return tesSUCCESS;
}

TER
NFTokenCancelOffer::doApply()
{
    for (auto const& id : ctx_.tx[sfNFTokenOffers])
    {
        if (auto offer = view().peek(keylet::nftoffer(id));
            offer && !nft::deleteTokenOffer(view(), offer))
        {
            // LCOV_EXCL_START
            JLOG(j_.fatal()) << "Unable to delete token offer " << id << " (ledger " << view().seq()
                             << ")";
            return tefBAD_LEDGER;
            // LCOV_EXCL_STOP
        }
    }

    return tesSUCCESS;
}

void
NFTokenCancelOffer::visitInvariantEntry(bool, SLE::const_ref, SLE::const_ref)
{
    // No transaction-specific invariants yet (future work).
}

bool
NFTokenCancelOffer::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    // No transaction-specific invariants yet (future work).
    return true;
}

}  // namespace xrpl
