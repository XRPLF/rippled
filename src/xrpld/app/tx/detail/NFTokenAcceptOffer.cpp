#include <xrpld/app/tx/detail/NFTokenAcceptOffer.h>
#include <xrpld/app/tx/detail/NFTokenUtils.h>

#include <xrpl/ledger/View.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Rate.h>
#include <xrpl/protocol/TxFlags.h>

namespace ripple {

std::uint32_t
NFTokenAcceptOffer::getFlagsMask(PreflightContext const& ctx)
{
    return tfNFTokenAcceptOfferMask;
}

NotTEC
NFTokenAcceptOffer::preflight(PreflightContext const& ctx)
{
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

    return tesSUCCESS;
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

            if ((*offerSLE)[sfAmount].negative())
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
        if (((*bo)[sfOwner] == (*so)[sfOwner]))
            return tecCANT_ACCEPT_OWN_NFTOKEN_OFFER;

        // Ensure that the buyer is willing to pay at least as much as the
        // seller is requesting:
        if ((*so)[sfAmount] > (*bo)[sfAmount])
            return tecINSUFFICIENT_PAYMENT;

        // The destination must be whoever is submitting the tx if the buyer
        // specified it
        if (auto const dest = bo->at(~sfDestination);
            dest && *dest != ctx.tx[sfAccount])
        {
            return tecNO_PERMISSION;
        }

        // The destination must be whoever is submitting the tx if the seller
        // specified it
        if (auto const dest = so->at(~sfDestination);
            dest && *dest != ctx.tx[sfAccount])
        {
            return tecNO_PERMISSION;
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

            // Check if broker is allowed to receive the fee with these IOUs.
            if (!brokerFee->native() &&
                ctx.view.rules().enabled(fixEnforceNFTokenTrustlineV2))
            {
                auto res = nft::checkTrustlineAuthorized(
                    ctx.view,
                    ctx.tx[sfAccount],
                    ctx.j,
                    brokerFee->asset().get<Issue>());
                if (res != tesSUCCESS)
                    return res;

                res = nft::checkTrustlineDeepFrozen(
                    ctx.view,
                    ctx.tx[sfAccount],
                    ctx.j,
                    brokerFee->asset().get<Issue>());
                if (res != tesSUCCESS)
                    return res;
            }
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

        if (accountFunds(
                ctx.view, (*bo)[sfOwner], needed, fhZERO_IF_FROZEN, ctx.j) <
            needed)
            return tecINSUFFICIENT_FUNDS;

        // Check that the account accepting the buy offer (he's selling the NFT)
        // is allowed to receive IOUs. Also check that this offer's creator is
        // authorized. But we need to exclude the case when the transaction is
        // created by the broker.
        if (ctx.view.rules().enabled(fixEnforceNFTokenTrustlineV2) &&
            !needed.native())
        {
            auto res = nft::checkTrustlineAuthorized(
                ctx.view, bo->at(sfOwner), ctx.j, needed.asset().get<Issue>());
            if (res != tesSUCCESS)
                return res;

            if (!so)
            {
                res = nft::checkTrustlineAuthorized(
                    ctx.view,
                    ctx.tx[sfAccount],
                    ctx.j,
                    needed.asset().get<Issue>());
                if (res != tesSUCCESS)
                    return res;

                res = nft::checkTrustlineDeepFrozen(
                    ctx.view,
                    ctx.tx[sfAccount],
                    ctx.j,
                    needed.asset().get<Issue>());
                if (res != tesSUCCESS)
                    return res;
            }
        }
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
        if (!bo)
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
            // an unnecessary tec, and is also resolved with this amendment.
            if (accountFunds(
                    ctx.view,
                    ctx.tx[sfAccount],
                    needed,
                    fhZERO_IF_FROZEN,
                    ctx.j) < needed)
                return tecINSUFFICIENT_FUNDS;
        }

        // Make sure that we are allowed to hold what the taker will pay us.
        if (!needed.native())
        {
            if (ctx.view.rules().enabled(fixEnforceNFTokenTrustlineV2))
            {
                auto res = nft::checkTrustlineAuthorized(
                    ctx.view,
                    (*so)[sfOwner],
                    ctx.j,
                    needed.asset().get<Issue>());
                if (res != tesSUCCESS)
                    return res;

                if (!bo)
                {
                    res = nft::checkTrustlineAuthorized(
                        ctx.view,
                        ctx.tx[sfAccount],
                        ctx.j,
                        needed.asset().get<Issue>());
                    if (res != tesSUCCESS)
                        return res;
                }
            }

            auto const res = nft::checkTrustlineDeepFrozen(
                ctx.view, (*so)[sfOwner], ctx.j, needed.asset().get<Issue>());
            if (res != tesSUCCESS)
                return res;
        }
    }

    // Additional checks are required in case a minter set a transfer fee for
    // this nftoken
    auto const& offer = bo ? bo : so;
    if (!offer)
        // Purely defensive, should be caught in preflight.
        return tecINTERNAL;  // LCOV_EXCL_LINE

    auto const& tokenID = offer->at(sfNFTokenID);
    auto const& amount = offer->at(sfAmount);
    auto const nftMinter = nft::getIssuer(tokenID);

    if (nft::getTransferFee(tokenID) != 0 && !amount.native())
    {
        // Fix a bug where the transfer of an NFToken with a transfer fee could
        // give the NFToken issuer an undesired trust line.
        // Issuer doesn't need a trust line to accept their own currency.
        if (ctx.view.rules().enabled(fixEnforceNFTokenTrustline) &&
            (nft::getFlags(tokenID) & nft::flagCreateTrustLines) == 0 &&
            nftMinter != amount.getIssuer() &&
            !ctx.view.read(keylet::line(nftMinter, amount.issue())))
            return tecNO_LINE;

        // Check that the issuer is allowed to receive IOUs.
        if (ctx.view.rules().enabled(fixEnforceNFTokenTrustlineV2))
        {
            auto res = nft::checkTrustlineAuthorized(
                ctx.view, nftMinter, ctx.j, amount.asset().get<Issue>());
            if (res != tesSUCCESS)
                return res;

            res = nft::checkTrustlineDeepFrozen(
                ctx.view, nftMinter, ctx.j, amount.asset().get<Issue>());
            if (res != tesSUCCESS)
                return res;
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

    // If any payment causes a non-IOU-issuer to have a negative balance,
    // or an IOU-issuer to have a positive balance in their own currency,
    // we know that something went wrong. This was originally found in the
    // context of IOU transfer fees. Since there are several payouts in this tx,
    // just confirm that the end state is OK.
    if (result != tesSUCCESS)
        return result;
    if (accountFunds(view(), from, amount, fhZERO_IF_FROZEN, j_).signum() < 0)
        return tecINSUFFICIENT_FUNDS;
    if (accountFunds(view(), to, amount, fhZERO_IF_FROZEN, j_).signum() < 0)
        return tecINSUFFICIENT_FUNDS;
    return tesSUCCESS;
}

TER
NFTokenAcceptOffer::transferNFToken(
    AccountID const& buyer,
    AccountID const& seller,
    uint256 const& nftokenID)
{
    auto tokenAndPage = nft::findTokenAndPage(view(), seller, nftokenID);

    if (!tokenAndPage)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    if (auto const ret = nft::removeToken(
            view(), seller, nftokenID, std::move(tokenAndPage->page));
        !isTesSuccess(ret))
        return ret;

    auto const sleBuyer = view().read(keylet::account(buyer));
    if (!sleBuyer)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    std::uint32_t const buyerOwnerCountBefore =
        sleBuyer->getFieldU32(sfOwnerCount);

    auto const insertRet =
        nft::insertToken(view(), buyer, std::move(tokenAndPage->token));

    // if fixNFTokenReserve is enabled, check if the buyer has sufficient
    // reserve to own a new object, if their OwnerCount changed.
    //
    // There was an issue where the buyer accepts a sell offer, the ledger
    // didn't check if the buyer has enough reserve, meaning that buyer can get
    // NFTs free of reserve.
    if (view().rules().enabled(fixNFTokenReserve))
    {
        // To check if there is sufficient reserve, we cannot use mPriorBalance
        // because NFT is sold for a price. So we must use the balance after
        // the deduction of the potential offer price. A small caveat here is
        // that the balance has already deducted the transaction fee, meaning
        // that the reserve requirement is a few drops higher.
        auto const buyerBalance = sleBuyer->getFieldAmount(sfBalance);

        auto const buyerOwnerCountAfter = sleBuyer->getFieldU32(sfOwnerCount);
        if (buyerOwnerCountAfter > buyerOwnerCountBefore)
        {
            if (auto const reserve =
                    view().fees().accountReserve(buyerOwnerCountAfter);
                buyerBalance < reserve)
                return tecINSUFFICIENT_RESERVE;
        }
    }

    return insertRet;
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
    return transferNFToken(buyer, seller, nftokenID);
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
        // LCOV_EXCL_START
        JLOG(j_.fatal()) << "Unable to delete buy offer '"
                         << to_string(bo->key()) << "': ignoring";
        return tecINTERNAL;
        // LCOV_EXCL_STOP
    }

    if (so && !nft::deleteTokenOffer(view(), so))
    {
        // LCOV_EXCL_START
        JLOG(j_.fatal()) << "Unable to delete sell offer '"
                         << to_string(so->key()) << "': ignoring";
        return tecINTERNAL;
        // LCOV_EXCL_STOP
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

        // Now transfer the NFT:
        return transferNFToken(buyer, seller, nftokenID);
    }

    if (bo)
        return acceptOffer(bo);

    if (so)
        return acceptOffer(so);

    return tecINTERNAL;  // LCOV_EXCL_LINE
}

}  // namespace ripple
