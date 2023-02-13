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

#include <ripple/app/tx/impl/NFTokenAcceptOffer.h>
#include <ripple/app/tx/impl/details/NFTokenUtils.h>
#include <ripple/ledger/View.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/Rate.h>
#include <ripple/protocol/TxFlags.h>
#include <ripple/protocol/st.h>

namespace ripple {

NotTEC
NFTokenAcceptOffer::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureNonFungibleTokensV1))
        return temDISABLED;

    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    if (ctx.tx.getFlags() & tfNFTokenAcceptOfferMask)
        return temINVALID_FLAG;

    auto const bo = ctx.tx[~sfNFTokenBuyOffer];
    auto const so = ctx.tx[~sfNFTokenSellOffer];

    // At least one of these MUST be specified
    if (!bo && !so)
        return temMALFORMED;

    // The `BrokerFee` field must not be present in direct mode but may be
    // present and greater than zero in brokered mode.
    if (auto const bf = ctx.tx[~sfNFTokenBrokerFee])
    {
        if (!bo || !so)
            return temMALFORMED;

        if (*bf <= beast::zero)
            return temMALFORMED;
    }

    return preflight2(ctx);
}

TER
NFTokenAcceptOffer::preclaim(PreclaimContext const& ctx)
{
    auto const checkOffer = [&ctx](std::optional<uint256> id)
        -> std::pair<std::shared_ptr<const SLE>, TER> {
        if (id)
        {
            if (id->isZero())
                return {nullptr, tecOBJECT_NOT_FOUND};

            auto offerSLE = ctx.view.read(keylet::nftoffer(*id));

            if (!offerSLE)
                return {nullptr, tecOBJECT_NOT_FOUND};

            if (hasExpired(ctx.view, (*offerSLE)[~sfExpiration]))
                return {nullptr, tecEXPIRED};

            // The initial implementation had a bug that allowed a negative
            // amount.  The fixNFTokenNegOffer amendment fixes that.
            if ((*offerSLE)[sfAmount].negative() &&
                ctx.view.rules().enabled(fixNFTokenNegOffer))
                return {nullptr, temBAD_OFFER};

            return {std::move(offerSLE), tesSUCCESS};
        }
        return {nullptr, tesSUCCESS};
    };

    auto const [bo, err1] = checkOffer(ctx.tx[~sfNFTokenBuyOffer]);
    if (!isTesSuccess(err1))
        return err1;
    auto const [so, err2] = checkOffer(ctx.tx[~sfNFTokenSellOffer]);
    if (!isTesSuccess(err2))
        return err2;

    if (bo && so)
    {
        // Brokered mode:
        // The two offers being brokered must be for the same token:
        if ((*bo)[sfNFTokenID] != (*so)[sfNFTokenID])
            return tecNFTOKEN_BUY_SELL_MISMATCH;

        // The two offers being brokered must be for the same asset:
        if ((*bo)[sfAmount].issue() != (*so)[sfAmount].issue())
            return tecNFTOKEN_BUY_SELL_MISMATCH;

        // The two offers may not form a loop.  A broker may not sell the
        // token to the current owner of the token.
        if (ctx.view.rules().enabled(fixNonFungibleTokensV1_2) &&
            ((*bo)[sfOwner] == (*so)[sfOwner]))
            return tecCANT_ACCEPT_OWN_NFTOKEN_OFFER;

        // Ensure that the buyer is willing to pay at least as much as the
        // seller is requesting:
        if ((*so)[sfAmount] > (*bo)[sfAmount])
            return tecINSUFFICIENT_PAYMENT;

        // If the buyer specified a destination
        if (auto const dest = bo->at(~sfDestination))
        {
            // Before this fix the destination could be either the seller or
            // a broker. After, it must be whoever is submitting the tx.
            if (ctx.view.rules().enabled(fixNonFungibleTokensV1_2))
            {
                if (*dest != ctx.tx[sfAccount])
                    return tecNO_PERMISSION;
            }
            else if (*dest != so->at(sfOwner) && *dest != ctx.tx[sfAccount])
                return tecNFTOKEN_BUY_SELL_MISMATCH;
        }

        // If the seller specified a destination
        if (auto const dest = so->at(~sfDestination))
        {
            // Before this fix the destination could be either the seller or
            // a broker. After, it must be whoever is submitting the tx.
            if (ctx.view.rules().enabled(fixNonFungibleTokensV1_2))
            {
                if (*dest != ctx.tx[sfAccount])
                    return tecNO_PERMISSION;
            }
            else if (*dest != bo->at(sfOwner) && *dest != ctx.tx[sfAccount])
                return tecNFTOKEN_BUY_SELL_MISMATCH;
        }

        // The broker can specify an amount that represents their cut; if they
        // have, ensure that the seller will get at least as much as they want
        // to get *after* this fee is accounted for (but before the issuer's
        // cut, if any).
        if (auto const brokerFee = ctx.tx[~sfNFTokenBrokerFee])
        {
            if (brokerFee->issue() != (*bo)[sfAmount].issue())
                return tecNFTOKEN_BUY_SELL_MISMATCH;

            if (brokerFee >= (*bo)[sfAmount])
                return tecINSUFFICIENT_PAYMENT;

            if ((*so)[sfAmount] > (*bo)[sfAmount] - *brokerFee)
                return tecINSUFFICIENT_PAYMENT;
        }
    }

    if (bo)
    {
        if (((*bo)[sfFlags] & lsfSellNFToken) == lsfSellNFToken)
            return tecNFTOKEN_OFFER_TYPE_MISMATCH;

        // An account can't accept an offer it placed:
        if ((*bo)[sfOwner] == ctx.tx[sfAccount])
            return tecCANT_ACCEPT_OWN_NFTOKEN_OFFER;

        // If not in bridged mode, the account must own the token:
        if (!so &&
            !nft::findToken(ctx.view, ctx.tx[sfAccount], (*bo)[sfNFTokenID]))
            return tecNO_PERMISSION;

        // If not in bridged mode...
        if (!so)
        {
            // If the offer has a Destination field, the acceptor must be the
            // Destination.
            if (auto const dest = bo->at(~sfDestination);
                dest.has_value() && *dest != ctx.tx[sfAccount])
                return tecNO_PERMISSION;
        }

        // The account offering to buy must have funds:
        //
        // After this amendment, we allow an IOU issuer to buy an NFT with their
        // own currency
        auto const needed = bo->at(sfAmount);
        if (ctx.view.rules().enabled(fixNonFungibleTokensV1_2))
        {
            if (accountFunds(
                    ctx.view, (*bo)[sfOwner], needed, fhZERO_IF_FROZEN, ctx.j) <
                needed)
                return tecINSUFFICIENT_FUNDS;
        }
        else if (
            accountHolds(
                ctx.view,
                (*bo)[sfOwner],
                needed.getCurrency(),
                needed.getIssuer(),
                fhZERO_IF_FROZEN,
                ctx.j) < needed)
            return tecINSUFFICIENT_FUNDS;
    }

    if (so)
    {
        if (((*so)[sfFlags] & lsfSellNFToken) != lsfSellNFToken)
            return tecNFTOKEN_OFFER_TYPE_MISMATCH;

        // An account can't accept an offer it placed:
        if ((*so)[sfOwner] == ctx.tx[sfAccount])
            return tecCANT_ACCEPT_OWN_NFTOKEN_OFFER;

        // The seller must own the token.
        if (!nft::findToken(ctx.view, (*so)[sfOwner], (*so)[sfNFTokenID]))
            return tecNO_PERMISSION;

        // If not in bridged mode...
        if (!bo)
        {
            // If the offer has a Destination field, the acceptor must be the
            // Destination.
            if (auto const dest = so->at(~sfDestination);
                dest.has_value() && *dest != ctx.tx[sfAccount])
                return tecNO_PERMISSION;
        }

        // The account offering to buy must have funds:
        auto const needed = so->at(sfAmount);
        if (!ctx.view.rules().enabled(fixNonFungibleTokensV1_2))
        {
            if (accountHolds(
                    ctx.view,
                    ctx.tx[sfAccount],
                    needed.getCurrency(),
                    needed.getIssuer(),
                    fhZERO_IF_FROZEN,
                    ctx.j) < needed)
                return tecINSUFFICIENT_FUNDS;
        }
        else if (!bo)
        {
            // After this amendment, we allow buyers to buy with their own
            // issued currency.
            //
            // In the case of brokered mode, this check is essentially
            // redundant, since we have already confirmed that buy offer is >
            // than the sell offer, and that the buyer can cover the buy
            // offer.
            //
            // We also _must not_ check the tx submitter in brokered
            // mode, because then we are confirming that the broker can
            // cover what the buyer will pay, which doesn't make sense, causes
            // an unncessary tec, and is also resolved with this amendment.
            if (accountFunds(
                    ctx.view,
                    ctx.tx[sfAccount],
                    needed,
                    fhZERO_IF_FROZEN,
                    ctx.j) < needed)
                return tecINSUFFICIENT_FUNDS;
        }
    }

    return tesSUCCESS;
}

TER
NFTokenAcceptOffer::pay(
    AccountID const& from,
    AccountID const& to,
    STAmount const& amount)
{
    // This should never happen, but it's easy and quick to check.
    if (amount < beast::zero)
        return tecINTERNAL;

    auto const result = accountSend(view(), from, to, amount, j_);

    // After this amendment, if any payment would cause a non-IOU-issuer to
    // have a negative balance, or an IOU-issuer to have a positive balance in
    // their own currency, we know that something went wrong. This was
    // originally found in the context of IOU transfer fees. Since there are
    // several payouts in this tx, just confirm that the end state is OK.
    if (!view().rules().enabled(fixNonFungibleTokensV1_2))
        return result;
    if (result != tesSUCCESS)
        return result;
    if (accountFunds(view(), from, amount, fhZERO_IF_FROZEN, j_).signum() < 0)
        return tecINSUFFICIENT_FUNDS;
    if (accountFunds(view(), to, amount, fhZERO_IF_FROZEN, j_).signum() < 0)
        return tecINSUFFICIENT_FUNDS;
    return tesSUCCESS;
}

TER
NFTokenAcceptOffer::acceptOffer(std::shared_ptr<SLE> const& offer)
{
    bool const isSell = offer->isFlag(lsfSellNFToken);
    AccountID const owner = (*offer)[sfOwner];
    AccountID const& seller = isSell ? owner : account_;
    AccountID const& buyer = isSell ? account_ : owner;

    auto const nftokenID = (*offer)[sfNFTokenID];

    if (auto amount = offer->getFieldAmount(sfAmount); amount != beast::zero)
    {
        // Calculate the issuer's cut from this sale, if any:
        if (auto const fee = nft::getTransferFee(nftokenID); fee != 0)
        {
            auto const cut = multiply(amount, nft::transferFeeAsRate(fee));

            if (auto const issuer = nft::getIssuer(nftokenID);
                cut != beast::zero && seller != issuer && buyer != issuer)
            {
                if (auto const r = pay(buyer, issuer, cut); !isTesSuccess(r))
                    return r;
                amount -= cut;
            }
        }

        // Send the remaining funds to the seller of the NFT
        if (auto const r = pay(buyer, seller, amount); !isTesSuccess(r))
            return r;
    }

    // Now transfer the NFT:
    auto tokenAndPage = nft::findTokenAndPage(view(), seller, nftokenID);

    if (!tokenAndPage)
        return tecINTERNAL;

    if (auto const ret = nft::removeToken(
            view(), seller, nftokenID, std::move(tokenAndPage->page));
        !isTesSuccess(ret))
        return ret;

    return nft::insertToken(view(), buyer, std::move(tokenAndPage->token));
}

TER
NFTokenAcceptOffer::doApply()
{
    auto const loadToken = [this](std::optional<uint256> const& id) {
        std::shared_ptr<SLE> sle;
        if (id)
            sle = view().peek(keylet::nftoffer(*id));
        return sle;
    };

    auto bo = loadToken(ctx_.tx[~sfNFTokenBuyOffer]);
    auto so = loadToken(ctx_.tx[~sfNFTokenSellOffer]);

    if (bo && !nft::deleteTokenOffer(view(), bo))
    {
        JLOG(j_.fatal()) << "Unable to delete buy offer '"
                         << to_string(bo->key()) << "': ignoring";
        return tecINTERNAL;
    }

    if (so && !nft::deleteTokenOffer(view(), so))
    {
        JLOG(j_.fatal()) << "Unable to delete sell offer '"
                         << to_string(so->key()) << "': ignoring";
        return tecINTERNAL;
    }

    // Bridging two different offers
    if (bo && so)
    {
        AccountID const buyer = (*bo)[sfOwner];
        AccountID const seller = (*so)[sfOwner];

        auto const nftokenID = (*so)[sfNFTokenID];

        // The amount is what the buyer of the NFT pays:
        STAmount amount = (*bo)[sfAmount];

        // Three different folks may be paid.  The order of operations is
        // important.
        //
        // o The broker is paid the cut they requested.
        // o The issuer's cut is calculated from what remains after the
        //   broker is paid.  The issuer can take up to 50% of the remainder.
        // o Finally, the seller gets whatever is left.
        //
        // It is important that the issuer's cut be calculated after the
        // broker's portion is already removed.  Calculating the issuer's
        // cut before the broker's cut is removed can result in more money
        // being paid out than the seller authorized.  That would be bad!

        // Send the broker the amount they requested.
        if (auto const cut = ctx_.tx[~sfNFTokenBrokerFee];
            cut && cut.value() != beast::zero)
        {
            if (auto const r = pay(buyer, account_, cut.value());
                !isTesSuccess(r))
                return r;

            amount -= cut.value();
        }

        // Calculate the issuer's cut, if any.
        if (auto const fee = nft::getTransferFee(nftokenID);
            amount != beast::zero && fee != 0)
        {
            auto cut = multiply(amount, nft::transferFeeAsRate(fee));

            if (auto const issuer = nft::getIssuer(nftokenID);
                seller != issuer && buyer != issuer)
            {
                if (auto const r = pay(buyer, issuer, cut); !isTesSuccess(r))
                    return r;

                amount -= cut;
            }
        }

        // And send whatever remains to the seller.
        if (amount > beast::zero)
        {
            if (auto const r = pay(buyer, seller, amount); !isTesSuccess(r))
                return r;
        }

        auto tokenAndPage = nft::findTokenAndPage(view(), seller, nftokenID);

        if (!tokenAndPage)
            return tecINTERNAL;

        if (auto const ret = nft::removeToken(
                view(), seller, nftokenID, std::move(tokenAndPage->page));
            !isTesSuccess(ret))
            return ret;

        return nft::insertToken(view(), buyer, std::move(tokenAndPage->token));
    }

    if (bo)
        return acceptOffer(bo);

    if (so)
        return acceptOffer(so);

    return tecINTERNAL;
}

}  // namespace ripple
