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
    bool const isSellOffer = txFlags & tfSellNFToken;

    if (txFlags & tfNFTokenCreateOfferMask)
        return temINVALID_FLAG;

    auto const account = ctx.tx[sfAccount];
    auto const nftFlags = nft::getFlags(ctx.tx[sfNFTokenID]);

    {
        STAmount const amount = ctx.tx[sfAmount];

        if (amount.negative() && ctx.rules.enabled(fixNFTokenNegOffer))
            // An offer for a negative amount makes no sense.
            return temBAD_AMOUNT;

        if (!isXRP(amount))
        {
            if (nftFlags & nft::flagOnlyXRP)
                return temBAD_AMOUNT;

            if (!amount)
                return temBAD_AMOUNT;
        }

        // If this is an offer to buy, you must offer something; if it's an
        // offer to sell, you can ask for nothing.
        if (!isSellOffer && !amount)
            return temBAD_AMOUNT;
    }

    if (auto exp = ctx.tx[~sfExpiration]; exp == 0)
        return temBAD_EXPIRATION;

    auto const owner = ctx.tx[~sfOwner];

    // The 'Owner' field must be present when offering to buy, but can't
    // be present when selling (it's implicit):
    if (owner.has_value() == isSellOffer)
        return temMALFORMED;

    if (owner && owner == account)
        return temMALFORMED;

    if (auto dest = ctx.tx[~sfDestination])
    {
        // Some folks think it makes sense for a buy offer to specify a
        // specific broker using the Destination field.  This change doesn't
        // deserve it's own amendment, so we're piggy-backing on
        // fixNFTokenNegOffer.
        //
        // Prior to fixNFTokenNegOffer any use of the Destination field on
        // a buy offer was malformed.
        if (!isSellOffer && !ctx.rules.enabled(fixNFTokenNegOffer))
            return temMALFORMED;

        // The destination can't be the account executing the transaction.
        if (dest == account)
            return temMALFORMED;
    }

    return preflight2(ctx);
}

TER
NFTokenCreateOffer::preclaim(PreclaimContext const& ctx)
{
    if (hasExpired(ctx.view, ctx.tx[~sfExpiration]))
        return tecEXPIRED;

    auto const nftokenID = ctx.tx[sfNFTokenID];
    bool const isSellOffer = ctx.tx.isFlag(tfSellNFToken);

    if (!nft::findToken(
            ctx.view, ctx.tx[isSellOffer ? sfAccount : sfOwner], nftokenID))
        return tecNO_ENTRY;

    auto const nftFlags = nft::getFlags(nftokenID);
    auto const issuer = nft::getIssuer(nftokenID);
    auto const amount = ctx.tx[sfAmount];

    if (!(nftFlags & nft::flagCreateTrustLines) && !amount.native() &&
        nft::getTransferFee(nftokenID))
    {
        if (!ctx.view.exists(keylet::account(issuer)))
            return tecNO_ISSUER;

        if (!ctx.view.exists(keylet::line(issuer, amount.issue())))
            return tecNO_LINE;

        if (isFrozen(
                ctx.view, issuer, amount.getCurrency(), amount.getIssuer()))
            return tecFROZEN;
    }

    if (issuer != ctx.tx[sfAccount] && !(nftFlags & nft::flagTransferable))
    {
        auto const root = ctx.view.read(keylet::account(issuer));
        assert(root);

        if (auto minter = (*root)[~sfNFTokenMinter];
            minter != ctx.tx[sfAccount])
            return tefNFTOKEN_IS_NOT_TRANSFERABLE;
    }

    if (isFrozen(
            ctx.view,
            ctx.tx[sfAccount],
            amount.getCurrency(),
            amount.getIssuer()))
        return tecFROZEN;

    // If this is an offer to buy the token, the account must have the
    // needed funds at hand; but note that funds aren't reserved and the
    // offer may later become unfunded.
    if (!isSellOffer)
    {
        auto const funds = accountHolds(
            ctx.view,
            ctx.tx[sfAccount],
            amount.getCurrency(),
            amount.getIssuer(),
            FreezeHandling::fhZERO_IF_FROZEN,
            ctx.j);

        if (funds.signum() <= 0)
            return tecUNFUNDED_OFFER;
    }

    if (auto const destination = ctx.tx[~sfDestination])
    {
        // If a destination is specified, the destination must already be in
        // the ledger.
        auto const sleDst = ctx.view.read(keylet::account(*destination));

        if (!sleDst)
            return tecNO_DST;

        // check if the destination has disallowed incoming offers
        if (ctx.view.rules().enabled(featureDisallowIncoming))
        {
            // flag cannot be set unless amendment is enabled but
            // out of an abundance of caution check anyway

            if (sleDst->getFlags() & lsfDisallowIncomingNFTOffer)
                return tecNO_PERMISSION;
        }
    }

    if (auto const owner = ctx.tx[~sfOwner])
    {
        // Check if the owner (buy offer) has disallowed incoming offers
        if (ctx.view.rules().enabled(featureDisallowIncoming))
        {
            auto const sleOwner = ctx.view.read(keylet::account(*owner));

            // defensively check
            // it should not be possible to specify owner that doesn't exist
            if (!sleOwner)
                return tecNO_TARGET;

            if (sleOwner->getFlags() & lsfDisallowIncomingNFTOffer)
                return tecNO_PERMISSION;
        }
    }

    return tesSUCCESS;
}

TER
NFTokenCreateOffer::doApply()
{
    if (auto const acct = view().read(keylet::account(ctx_.tx[sfAccount]));
        mPriorBalance < view().fees().accountReserve((*acct)[sfOwnerCount] + 1))
        return tecINSUFFICIENT_RESERVE;

    auto const nftokenID = ctx_.tx[sfNFTokenID];

    auto const offerID =
        keylet::nftoffer(account_, ctx_.tx.getSeqProxy().value());

    // Create the offer:
    {
        // Token offers are always added to the owner's owner directory:
        auto const ownerNode = view().dirInsert(
            keylet::ownerDir(account_), offerID, describeOwnerDir(account_));

        if (!ownerNode)
            return tecDIR_FULL;

        bool const isSellOffer = ctx_.tx.isFlag(tfSellNFToken);

        // Token offers are also added to the token's buy or sell offer
        // directory
        auto const offerNode = view().dirInsert(
            isSellOffer ? keylet::nft_sells(nftokenID)
                        : keylet::nft_buys(nftokenID),
            offerID,
            [&nftokenID, isSellOffer](std::shared_ptr<SLE> const& sle) {
                (*sle)[sfFlags] =
                    isSellOffer ? lsfNFTokenSellOffers : lsfNFTokenBuyOffers;
                (*sle)[sfNFTokenID] = nftokenID;
            });

        if (!offerNode)
            return tecDIR_FULL;

        std::uint32_t sleFlags = 0;

        if (isSellOffer)
            sleFlags |= lsfSellNFToken;

        auto offer = std::make_shared<SLE>(offerID);
        (*offer)[sfOwner] = account_;
        (*offer)[sfNFTokenID] = nftokenID;
        (*offer)[sfAmount] = ctx_.tx[sfAmount];
        (*offer)[sfFlags] = sleFlags;
        (*offer)[sfOwnerNode] = *ownerNode;
        (*offer)[sfNFTokenOfferNode] = *offerNode;

        if (auto const expiration = ctx_.tx[~sfExpiration])
            (*offer)[sfExpiration] = *expiration;

        if (auto const destination = ctx_.tx[~sfDestination])
            (*offer)[sfDestination] = *destination;

        view().insert(offer);
    }

    // Update owner count.
    adjustOwnerCount(view(), view().peek(keylet::account(account_)), 1, j_);

    return tesSUCCESS;
}

}  // namespace ripple
