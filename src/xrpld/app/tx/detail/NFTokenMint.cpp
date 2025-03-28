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

#include <xrpld/app/tx/detail/NFTokenMint.h>
#include <xrpld/ledger/View.h>

#include <xrpl/basics/Expected.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/InnerObjectFormats.h>
#include <xrpl/protocol/Rate.h>
#include <xrpl/protocol/TxFlags.h>

#include <boost/endian/conversion.hpp>

#include <array>

namespace ripple {

static std::uint16_t
extractNFTokenFlagsFromTxFlags(std::uint32_t txFlags)
{
    return static_cast<std::uint16_t>(txFlags & 0x0000FFFF);
}

NotTEC
NFTokenMint::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureNonFungibleTokensV1))
        return temDISABLED;

    bool const hasOfferFields = ctx.tx.isFieldPresent(sfAmount) ||
        ctx.tx.isFieldPresent(sfDestination) ||
        ctx.tx.isFieldPresent(sfExpiration);

    if (!ctx.rules.enabled(featureNFTokenMintOffer) && hasOfferFields)
        return temDISABLED;

    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    // Prior to fixRemoveNFTokenAutoTrustLine, transfer of an NFToken between
    // accounts allowed a TrustLine to be added to the issuer of that token
    // without explicit permission from that issuer.  This was enabled by
    // minting the NFToken with the tfTrustLine flag set.
    //
    // That capability could be used to attack the NFToken issuer.  It
    // would be possible for two accounts to trade the NFToken back and forth
    // building up any number of TrustLines on the issuer, increasing the
    // issuer's reserve without bound.
    //
    // The fixRemoveNFTokenAutoTrustLine amendment disables minting with the
    // tfTrustLine flag as a way to prevent the attack.  But until the
    // amendment passes we still need to keep the old behavior available.
    std::uint32_t const NFTokenMintMask =
        ctx.rules.enabled(fixRemoveNFTokenAutoTrustLine)
        // if featureDynamicNFT enabled then new flag allowing mutable URI
        // available
        ? ctx.rules.enabled(featureDynamicNFT) ? tfNFTokenMintMaskWithMutable
                                               : tfNFTokenMintMask
        : ctx.rules.enabled(featureDynamicNFT) ? tfNFTokenMintOldMaskWithMutable
                                               : tfNFTokenMintOldMask;

    if (ctx.tx.getFlags() & NFTokenMintMask)
        return temINVALID_FLAG;

    if (auto const f = ctx.tx[~sfTransferFee])
    {
        if (f > maxTransferFee)
            return temBAD_NFTOKEN_TRANSFER_FEE;

        // If a non-zero TransferFee is set then the tfTransferable flag
        // must also be set.
        if (f > 0u && !ctx.tx.isFlag(tfTransferable))
            return temMALFORMED;
    }

    // An issuer must only be set if the tx is executed by the minter
    if (auto iss = ctx.tx[~sfIssuer]; iss == ctx.tx[sfAccount])
        return temMALFORMED;

    if (auto uri = ctx.tx[~sfURI])
    {
        if (uri->length() == 0 || uri->length() > maxTokenURILength)
            return temMALFORMED;
    }

    if (hasOfferFields)
    {
        // The Amount field must be present if either the Destination or
        // Expiration fields are present.
        if (!ctx.tx.isFieldPresent(sfAmount))
            return temMALFORMED;

        // Rely on the common code shared with NFTokenCreateOffer to
        // do the validation.  We pass tfSellNFToken as the transaction flags
        // because a Mint is only allowed to create a sell offer.
        if (NotTEC notTec = nft::tokenOfferCreatePreflight(
                ctx.tx[sfAccount],
                ctx.tx[sfAmount],
                ctx.tx[~sfDestination],
                ctx.tx[~sfExpiration],
                extractNFTokenFlagsFromTxFlags(ctx.tx.getFlags()),
                ctx.rules);
            !isTesSuccess(notTec))
        {
            return notTec;
        }
    }

    return preflight2(ctx);
}

uint256
NFTokenMint::createNFTokenID(
    std::uint16_t flags,
    std::uint16_t fee,
    AccountID const& issuer,
    nft::Taxon taxon,
    std::uint32_t tokenSeq)
{
    // An issuer may issue several NFTs with the same taxon; to ensure that NFTs
    // are spread across multiple pages we lightly mix the taxon up by using the
    // sequence (which is not under the issuer's direct control) as the seed for
    // a simple linear congruential generator.  cipheredTaxon() does this work.
    taxon = nft::cipheredTaxon(tokenSeq, taxon);

    // The values are packed inside a 32-byte buffer, so we need to make sure
    // that the endianess is fixed.
    flags = boost::endian::native_to_big(flags);
    fee = boost::endian::native_to_big(fee);
    taxon = nft::toTaxon(boost::endian::native_to_big(nft::toUInt32(taxon)));
    tokenSeq = boost::endian::native_to_big(tokenSeq);

    std::array<std::uint8_t, 32> buf{};

    auto ptr = buf.data();

    // This code is awkward but the idea is to pack these values into a single
    // 256-bit value that uniquely identifies this NFT.
    std::memcpy(ptr, &flags, sizeof(flags));
    ptr += sizeof(flags);

    std::memcpy(ptr, &fee, sizeof(fee));
    ptr += sizeof(fee);

    std::memcpy(ptr, issuer.data(), issuer.size());
    ptr += issuer.size();

    std::memcpy(ptr, &taxon, sizeof(taxon));
    ptr += sizeof(taxon);

    std::memcpy(ptr, &tokenSeq, sizeof(tokenSeq));
    ptr += sizeof(tokenSeq);
    XRPL_ASSERT(
        std::distance(buf.data(), ptr) == buf.size(),
        "ripple::NFTokenMint::createNFTokenID : data size matches the buffer");

    return uint256::fromVoid(buf.data());
}

TER
NFTokenMint::preclaim(PreclaimContext const& ctx)
{
    // The issuer of the NFT may or may not be the account executing this
    // transaction. Check that and verify that this is allowed:
    if (auto issuer = ctx.tx[~sfIssuer])
    {
        auto const sle = ctx.view.read(keylet::account(*issuer));

        if (!sle)
            return tecNO_ISSUER;

        if (auto const minter = (*sle)[~sfNFTokenMinter];
            minter != ctx.tx[sfAccount])
            return tecNO_PERMISSION;
    }

    if (ctx.tx.isFieldPresent(sfAmount))
    {
        // The Amount field says create an offer for the minted token.
        if (hasExpired(ctx.view, ctx.tx[~sfExpiration]))
            return tecEXPIRED;

        // Rely on the common code shared with NFTokenCreateOffer to
        // do the validation.  We pass tfSellNFToken as the transaction flags
        // because a Mint is only allowed to create a sell offer.
        if (TER const ter = nft::tokenOfferCreatePreclaim(
                ctx.view,
                ctx.tx[sfAccount],
                ctx.tx[~sfIssuer].value_or(ctx.tx[sfAccount]),
                ctx.tx[sfAmount],
                ctx.tx[~sfDestination],
                extractNFTokenFlagsFromTxFlags(ctx.tx.getFlags()),
                ctx.tx[~sfTransferFee].value_or(0),
                ctx.j);
            !isTesSuccess(ter))
            return ter;
    }
    return tesSUCCESS;
}

TER
NFTokenMint::doApply()
{
    auto const issuer = ctx_.tx[~sfIssuer].value_or(account_);

    auto const tokenSeq = [this, &issuer]() -> Expected<std::uint32_t, TER> {
        auto const root = view().peek(keylet::account(issuer));
        if (root == nullptr)
            // Should not happen.  Checked in preclaim.
            return Unexpected(tecNO_ISSUER);

        if (!ctx_.view().rules().enabled(fixNFTokenRemint))
        {
            // Get the unique sequence number for this token:
            std::uint32_t const tokenSeq =
                (*root)[~sfMintedNFTokens].value_or(0);
            {
                std::uint32_t const nextTokenSeq = tokenSeq + 1;
                if (nextTokenSeq < tokenSeq)
                    return Unexpected(tecMAX_SEQUENCE_REACHED);

                (*root)[sfMintedNFTokens] = nextTokenSeq;
            }
            ctx_.view().update(root);
            return tokenSeq;
        }

        // With fixNFTokenRemint amendment enabled:
        //
        // If the issuer hasn't minted an NFToken before we must add a
        // FirstNFTokenSequence field to the issuer's AccountRoot.  The
        // value of the FirstNFTokenSequence must equal the issuer's
        // current account sequence.
        //
        // There are three situations:
        //  o If the first token is being minted by the issuer and
        //     * If the transaction consumes a Sequence number, then the
        //       Sequence has been pre-incremented by the time we get here in
        //       doApply.  We must decrement the value in the Sequence field.
        //     * Otherwise the transaction uses a Ticket so the Sequence has
        //       not been pre-incremented.  We use the Sequence value as is.
        //  o The first token is being minted by an authorized minter.  In
        //    this case the issuer's Sequence field has been left untouched.
        //    We use the issuer's Sequence value as is.
        if (!root->isFieldPresent(sfFirstNFTokenSequence))
        {
            std::uint32_t const acctSeq = root->at(sfSequence);

            root->at(sfFirstNFTokenSequence) =
                ctx_.tx.isFieldPresent(sfIssuer) ||
                    ctx_.tx.getSeqProxy().isTicket()
                ? acctSeq
                : acctSeq - 1;
        }

        std::uint32_t const mintedNftCnt =
            (*root)[~sfMintedNFTokens].value_or(0u);

        (*root)[sfMintedNFTokens] = mintedNftCnt + 1u;
        if ((*root)[sfMintedNFTokens] == 0u)
            return Unexpected(tecMAX_SEQUENCE_REACHED);

        // Get the unique sequence number of this token by
        // sfFirstNFTokenSequence + sfMintedNFTokens
        std::uint32_t const offset = (*root)[sfFirstNFTokenSequence];
        std::uint32_t const tokenSeq = offset + mintedNftCnt;

        // Check for more overflow cases
        if (tokenSeq + 1u == 0u || tokenSeq < offset)
            return Unexpected(tecMAX_SEQUENCE_REACHED);

        ctx_.view().update(root);
        return tokenSeq;
    }();

    if (!tokenSeq.has_value())
        return (tokenSeq.error());

    std::uint32_t const ownerCountBefore =
        view().read(keylet::account(account_))->getFieldU32(sfOwnerCount);

    // Assemble the new NFToken.
    SOTemplate const* nfTokenTemplate =
        InnerObjectFormats::getInstance().findSOTemplateBySField(sfNFToken);

    if (nfTokenTemplate == nullptr)
        // Should never happen.
        return tecINTERNAL;

    auto const nftokenID = createNFTokenID(
        extractNFTokenFlagsFromTxFlags(ctx_.tx.getFlags()),
        ctx_.tx[~sfTransferFee].value_or(0),
        issuer,
        nft::toTaxon(ctx_.tx[sfNFTokenTaxon]),
        tokenSeq.value());

    STObject newToken(
        *nfTokenTemplate, sfNFToken, [this, &nftokenID](STObject& object) {
            object.setFieldH256(sfNFTokenID, nftokenID);

            if (auto const uri = ctx_.tx[~sfURI])
                object.setFieldVL(sfURI, *uri);
        });

    if (TER const ret =
            nft::insertToken(ctx_.view(), account_, std::move(newToken));
        ret != tesSUCCESS)
        return ret;

    if (ctx_.tx.isFieldPresent(sfAmount))
    {
        // Rely on the common code shared with NFTokenCreateOffer to create
        // the offer.  We pass tfSellNFToken as the transaction flags
        // because a Mint is only allowed to create a sell offer.
        if (TER const ter = nft::tokenOfferCreateApply(
                view(),
                ctx_.tx[sfAccount],
                ctx_.tx[sfAmount],
                ctx_.tx[~sfDestination],
                ctx_.tx[~sfExpiration],
                ctx_.tx.getSeqProxy(),
                nftokenID,
                mPriorBalance,
                j_);
            !isTesSuccess(ter))
            return ter;
    }

    // Only check the reserve if the owner count actually changed.  This
    // allows NFTs to be added to the page (and burn fees) without
    // requiring the reserve to be met each time.  The reserve is
    // only managed when a new NFT page or sell offer is added.
    if (auto const ownerCountAfter =
            view().read(keylet::account(account_))->getFieldU32(sfOwnerCount);
        ownerCountAfter > ownerCountBefore)
    {
        if (auto const reserve = view().fees().accountReserve(ownerCountAfter);
            mPriorBalance < reserve)
            return tecINSUFFICIENT_RESERVE;
    }
    return tesSUCCESS;
}

}  // namespace ripple
