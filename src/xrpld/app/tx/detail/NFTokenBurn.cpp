#include <xrpld/app/tx/detail/NFTokenBurn.h>
#include <xrpld/app/tx/detail/NFTokenUtils.h>

#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/TxFlags.h>

namespace ripple {

NotTEC
NFTokenBurn::preflight(PreflightContext const& ctx)
{
    return tesSUCCESS;
}

TER
NFTokenBurn::preclaim(PreclaimContext const& ctx)
{
    auto const owner = [&ctx]() {
        if (ctx.tx.isFieldPresent(sfOwner))
            return ctx.tx.getAccountID(sfOwner);

        return ctx.tx[sfAccount];
    }();

    if (!nft::findToken(ctx.view, owner, ctx.tx[sfNFTokenID]))
        return tecNO_ENTRY;

    // The owner of a token can always burn it, but the issuer can only
    // do so if the token is marked as burnable.
    if (auto const account = ctx.tx[sfAccount]; owner != account)
    {
        if (!(nft::getFlags(ctx.tx[sfNFTokenID]) & nft::flagBurnable))
            return tecNO_PERMISSION;

        if (auto const issuer = nft::getIssuer(ctx.tx[sfNFTokenID]);
            issuer != account)
        {
            if (auto const sle = ctx.view.read(keylet::account(issuer)); sle)
            {
                if (auto const minter = (*sle)[~sfNFTokenMinter];
                    minter != account)
                    return tecNO_PERMISSION;
            }
        }
    }

    if (!ctx.view.rules().enabled(fixNonFungibleTokensV1_2))
    {
        // If there are too many offers, then burning the token would produce
        // too much metadata.  Disallow burning a token with too many offers.
        return nft::notTooManyOffers(ctx.view, ctx.tx[sfNFTokenID]);
    }

    return tesSUCCESS;
}

TER
NFTokenBurn::doApply()
{
    // Remove the token, effectively burning it:
    auto const ret = nft::removeToken(
        view(),
        ctx_.tx.isFieldPresent(sfOwner) ? ctx_.tx.getAccountID(sfOwner)
                                        : ctx_.tx.getAccountID(sfAccount),
        ctx_.tx[sfNFTokenID]);

    // Should never happen since preclaim() verified the token is present.
    if (!isTesSuccess(ret))
        return ret;

    if (auto issuer =
            view().peek(keylet::account(nft::getIssuer(ctx_.tx[sfNFTokenID]))))
    {
        (*issuer)[~sfBurnedNFTokens] =
            (*issuer)[~sfBurnedNFTokens].value_or(0) + 1;
        view().update(issuer);
    }

    if (ctx_.view().rules().enabled(fixNonFungibleTokensV1_2))
    {
        // Delete up to 500 offers in total.
        // Because the number of sell offers is likely to be less than
        // the number of buy offers, we prioritize the deletion of sell
        // offers in order to clean up sell offer directory
        std::size_t const deletedSellOffers = nft::removeTokenOffersWithLimit(
            view(),
            keylet::nft_sells(ctx_.tx[sfNFTokenID]),
            maxDeletableTokenOfferEntries);

        if (maxDeletableTokenOfferEntries > deletedSellOffers)
        {
            nft::removeTokenOffersWithLimit(
                view(),
                keylet::nft_buys(ctx_.tx[sfNFTokenID]),
                maxDeletableTokenOfferEntries - deletedSellOffers);
        }
    }
    else
    {
        // Deletion of all offers.
        nft::removeTokenOffersWithLimit(
            view(),
            keylet::nft_sells(ctx_.tx[sfNFTokenID]),
            std::numeric_limits<int>::max());

        nft::removeTokenOffersWithLimit(
            view(),
            keylet::nft_buys(ctx_.tx[sfNFTokenID]),
            std::numeric_limits<int>::max());
    }

    return tesSUCCESS;
}

}  // namespace ripple
