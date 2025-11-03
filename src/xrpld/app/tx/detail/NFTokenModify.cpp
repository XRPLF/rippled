//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2024 Ripple Labs Inc.

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

#include <xrpld/app/tx/detail/NFTokenModify.h>
#include <xrpld/app/tx/detail/NFTokenUtils.h>

#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/TxFlags.h>

namespace ripple {

NotTEC
NFTokenModify::preflight(PreflightContext const& ctx)
{
    if (auto owner = ctx.tx[~sfOwner]; owner == ctx.tx[sfAccount])
        return temMALFORMED;

    if (auto uri = ctx.tx[~sfURI])
    {
        if (uri->length() == 0 || uri->length() > maxTokenURILength)
            return temMALFORMED;
    }

    return tesSUCCESS;
}

TER
NFTokenModify::preclaim(PreclaimContext const& ctx)
{
    AccountID const account = ctx.tx[sfAccount];
    AccountID const owner =
        ctx.tx[ctx.tx.isFieldPresent(sfOwner) ? sfOwner : sfAccount];

    if (!nft::findToken(ctx.view, owner, ctx.tx[sfNFTokenID]))
        return tecNO_ENTRY;

    // Check if the NFT is mutable
    if (!(nft::getFlags(ctx.tx[sfNFTokenID]) & nft::flagMutable))
        return tecNO_PERMISSION;

    // Verify permissions for the issuer
    if (AccountID const issuer = nft::getIssuer(ctx.tx[sfNFTokenID]);
        issuer != account)
    {
        auto const sle = ctx.view.read(keylet::account(issuer));
        if (!sle)
            return tecINTERNAL;  // LCOV_EXCL_LINE
        if (auto const minter = (*sle)[~sfNFTokenMinter]; minter != account)
            return tecNO_PERMISSION;
    }

    return tesSUCCESS;
}

TER
NFTokenModify::doApply()
{
    uint256 const nftokenID = ctx_.tx[sfNFTokenID];
    AccountID const owner =
        ctx_.tx[ctx_.tx.isFieldPresent(sfOwner) ? sfOwner : sfAccount];

    return nft::changeTokenURI(view(), owner, nftokenID, ctx_.tx[~sfURI]);
}

}  // namespace ripple
