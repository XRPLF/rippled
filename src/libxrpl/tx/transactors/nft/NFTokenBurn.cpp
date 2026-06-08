#include <xrpl/tx/transactors/nft/NFTokenBurn.h>

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/helpers/NFTokenHelpers.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/protocol/nft.h>
#include <xrpl/tx/Transactor.h>

#include <cstddef>
namespace xrpl {

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
        if ((nft::getFlags(ctx.tx[sfNFTokenID]) & nft::kFlagBurnable) == 0)
            return tecNO_PERMISSION;

        if (auto const issuer = nft::getIssuer(ctx.tx[sfNFTokenID]); issuer != account)
        {
            if (auto const sle = ctx.view.read(keylet::account(issuer)); sle)
            {
                if (auto const minter = (*sle)[~sfNFTokenMinter]; minter != account)
                    return tecNO_PERMISSION;
            }
        }
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

    if (auto issuer = view().peek(keylet::account(nft::getIssuer(ctx_.tx[sfNFTokenID]))))
    {
        (*issuer)[~sfBurnedNFTokens] = (*issuer)[~sfBurnedNFTokens].valueOr(0) + 1;
        view().update(issuer);
    }

    // Delete up to 500 offers in total.
    // Because the number of sell offers is likely to be less than
    // the number of buy offers, we prioritize the deletion of sell
    // offers in order to clean up sell offer directory
    std::size_t const deletedSellOffers = nft::removeTokenOffersWithLimit(
        view(), keylet::nftSells(ctx_.tx[sfNFTokenID]), kMaxDeletableTokenOfferEntries);

    if (kMaxDeletableTokenOfferEntries > deletedSellOffers)
    {
        nft::removeTokenOffersWithLimit(
            view(),
            keylet::nftBuys(ctx_.tx[sfNFTokenID]),
            kMaxDeletableTokenOfferEntries - deletedSellOffers);
    }

    return tesSUCCESS;
}

void
NFTokenBurn::visitInvariantEntry(bool, SLE::const_ref, SLE::const_ref)
{
    // No transaction-specific invariants yet (future work).
}

bool
NFTokenBurn::finalizeInvariants(STTx const&, TER, XRPAmount, ReadView const&, beast::Journal const&)
{
    // No transaction-specific invariants yet (future work).
    return true;
}

}  // namespace xrpl
