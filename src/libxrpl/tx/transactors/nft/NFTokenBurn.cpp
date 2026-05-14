/** @file
 *  Implementation of the `NFTokenBurn` transactor.
 *
 *  Permanently destroys an NFToken by removing it from the owner's
 *  NFTokenPage, incrementing the issuer's `sfBurnedNFTokens` counter, and
 *  deleting associated buy/sell offers up to `kMAX_DELETABLE_TOKEN_OFFER_ENTRIES`.
 *
 *  Permission model (enforced in `preclaim`):
 *  - The **token owner** may always burn their own token.
 *  - A **non-owner** requires the token's `nft::kFLAG_BURNABLE` bit to be set
 *    and must be either the issuer or the issuer's `sfNFTokenMinter` delegate.
 *
 *  The issuer is read directly from the packed 256-bit `sfNFTokenID` via
 *  `nft::getIssuer()` — no secondary ledger lookup is needed to resolve it.
 */
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
#include <memory>

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

    if (auto const account = ctx.tx[sfAccount]; owner != account)
    {
        if ((nft::getFlags(ctx.tx[sfNFTokenID]) & nft::kFLAG_BURNABLE) == 0)
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

    // Sell offers are processed first: their directory is typically smaller,
    // so clearing them first maximises the chance of a complete cleanup
    // within the single-transaction 500-offer budget.
    std::size_t const deletedSellOffers = nft::removeTokenOffersWithLimit(
        view(), keylet::nftSells(ctx_.tx[sfNFTokenID]), kMAX_DELETABLE_TOKEN_OFFER_ENTRIES);

    if (kMAX_DELETABLE_TOKEN_OFFER_ENTRIES > deletedSellOffers)
    {
        nft::removeTokenOffersWithLimit(
            view(),
            keylet::nftBuys(ctx_.tx[sfNFTokenID]),
            kMAX_DELETABLE_TOKEN_OFFER_ENTRIES - deletedSellOffers);
    }

    return tesSUCCESS;
}

void
NFTokenBurn::visitInvariantEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const&)
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
