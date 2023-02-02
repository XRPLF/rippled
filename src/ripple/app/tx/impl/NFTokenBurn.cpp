//------------------------------------------------------------------------------
/*
  This file is part of rippled: https://github.com/ripple/rippled
  Copyright (c) 2021 Ripple Labs Inc.

  Permission to use, copy, modify, and/or distribute this software for any
  purpose  with  or without fee is hereby granted, provided that the above
  copyright notice and this permission notice appear in all copies.

  THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
  WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
  MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
  ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
  WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
  ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <ripple/app/tx/impl/NFTokenBurn.h>
#include <ripple/app/tx/impl/details/NFTokenUtils.h>
#include <ripple/ledger/Directory.h>
#include <ripple/ledger/View.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/Protocol.h>
#include <ripple/protocol/TxFlags.h>
#include <ripple/protocol/st.h>
#include <boost/endian/conversion.hpp>
#include <array>

namespace ripple {

NotTEC
NFTokenBurn::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureNonFungibleTokensV1))
        return temDISABLED;

    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    if (ctx.tx.getFlags() & tfUniversalMask)
        return temINVALID_FLAG;

    return preflight2(ctx);
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

    if (!ctx.view.rules().enabled(fixUnburnableNFToken))
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

    if (ctx_.view().rules().enabled(fixUnburnableNFToken))
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
