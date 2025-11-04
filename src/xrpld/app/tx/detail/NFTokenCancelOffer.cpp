#include <xrpld/app/tx/detail/NFTokenCancelOffer.h>
#include <xrpld/app/tx/detail/NFTokenUtils.h>

#include <xrpl/ledger/View.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/TxFlags.h>

#include <boost/endian/conversion.hpp>

namespace ripple {

std::uint32_t
NFTokenCancelOffer::getFlagsMask(PreflightContext const& ctx)
{
    return tfNFTokenCancelOfferMask;
}

NotTEC
NFTokenCancelOffer::preflight(PreflightContext const& ctx)
{
    if (auto const& ids = ctx.tx[sfNFTokenOffers];
        ids.empty() || (ids.size() > maxTokenOfferCancelCount))
        return temMALFORMED;

    // In order to prevent unnecessarily overlarge transactions, we
    // disallow duplicates in the list of offers to cancel.
    STVector256 ids = ctx.tx.getFieldV256(sfNFTokenOffers);
    std::sort(ids.begin(), ids.end());
    if (std::adjacent_find(ids.begin(), ids.end()) != ids.end())
        return temMALFORMED;

    return tesSUCCESS;
}

TER
NFTokenCancelOffer::preclaim(PreclaimContext const& ctx)
{
    auto const account = ctx.tx[sfAccount];

    auto const& ids = ctx.tx[sfNFTokenOffers];

    auto ret = std::find_if(
        ids.begin(), ids.end(), [&ctx, &account](uint256 const& id) {
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
            JLOG(j_.fatal()) << "Unable to delete token offer " << id
                             << " (ledger " << view().seq() << ")";
            return tefBAD_LEDGER;
            // LCOV_EXCL_STOP
        }
    }

    return tesSUCCESS;
}

}  // namespace ripple
