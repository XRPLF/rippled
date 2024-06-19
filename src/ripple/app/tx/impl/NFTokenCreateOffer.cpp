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

#include <ripple/app/tx/impl/NFTokenCreateOffer.h>
#include <ripple/app/tx/impl/details/NFTokenUtils.h>
#include <ripple/ledger/View.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/TxFlags.h>
#include <ripple/protocol/st.h>
#include <boost/endian/conversion.hpp>

namespace ripple {

NotTEC
NFTokenCreateOffer::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureNonFungibleTokensV1))
        return temDISABLED;

    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    auto const txFlags = ctx.tx.getFlags();

    if (txFlags & tfNFTokenCreateOfferMask)
        return temINVALID_FLAG;

    auto const nftFlags = nft::getFlags(ctx.tx[sfNFTokenID]);

    // Use implementation shared with NFTokenMint
    if (NotTEC notTec = nft::tokenOfferCreatePreflight(
            ctx.tx[sfAccount],
            ctx.tx[sfAmount],
            ctx.tx[~sfDestination],
            ctx.tx[~sfExpiration],
            nftFlags,
            ctx.rules,
            ctx.tx[~sfOwner],
            txFlags);
        !isTesSuccess(notTec))
        return notTec;

    return preflight2(ctx);
}

TER
NFTokenCreateOffer::preclaim(PreclaimContext const& ctx)
{
    if (hasExpired(ctx.view, ctx.tx[~sfExpiration]))
        return tecEXPIRED;

    uint256 const nftokenID = ctx.tx[sfNFTokenID];
    std::uint32_t const txFlags = {ctx.tx.getFlags()};

    if (!nft::findToken(
            ctx.view,
            ctx.tx[(txFlags & tfSellNFToken) ? sfAccount : sfOwner],
            nftokenID))
        return tecNO_ENTRY;

    // Use implementation shared with NFTokenMint
    return nft::tokenOfferCreatePreclaim(
        ctx.view,
        ctx.tx[sfAccount],
        nft::getIssuer(nftokenID),
        ctx.tx[sfAmount],
        ctx.tx[~sfDestination],
        nft::getFlags(nftokenID),
        nft::getTransferFee(nftokenID),
        ctx.j,
        ctx.tx[~sfOwner],
        txFlags);
}

TER
NFTokenCreateOffer::doApply()
{
    // Use implementation shared with NFTokenMint
    return nft::tokenOfferCreateApply(
        view(),
        ctx_.tx[sfAccount],
        ctx_.tx[sfAmount],
        ctx_.tx[~sfDestination],
        ctx_.tx[~sfExpiration],
        ctx_.tx.getSeqProxy(),
        ctx_.tx[sfNFTokenID],
        mPriorBalance,
        j_,
        ctx_.tx.getFlags());
}

}  // namespace ripple
