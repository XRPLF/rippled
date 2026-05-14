/** @file
 *  Implementation of the `NFTokenAcceptOffer` transactor.
 *
 *  This is the most financially complex NFT transaction: up to four parties
 *  (buyer, seller, royalty issuer, broker) may receive funds in a single
 *  atomic apply.  See the class and method documentation in
 *  `NFTokenAcceptOffer.h` for the full behavioral contract.
 */
#include <xrpl/tx/transactors/nft/NFTokenAcceptOffer.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/NFTokenHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Rate.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/protocol/nft.h>
#include <xrpl/tx/Transactor.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

namespace xrpl {

NotTEC
NFTokenAcceptOffer::preflight(PreflightContext const& ctx)
{
    auto const bo = ctx.tx[~sfNFTokenBuyOffer];
    auto const so = ctx.tx[~sfNFTokenSellOffer];

    if (!bo && !so)
        return temMALFORMED;

    if (auto const bf = ctx.tx[~sfNFTokenBrokerFee])
    {
        if (!bo || !so)
            return temMALFORMED;

        if (*bf <= beast::kZERO)
            return temMALFORMED;
    }

    return tesSUCCESS;
}

TER
NFTokenAcceptOffer::preclaim(PreclaimContext const& ctx)
{
    auto const checkOffer =
        [&ctx](std::optional<uint256> id) -> std::pair<std::shared_ptr<const SLE>, TER> {
        if (id)
        {
            if (id->isZero())
                return {nullptr, tecOBJECT_NOT_FOUND};

            auto offerSLE = ctx.view.read(keylet::nftoffer(*id));

            if (!offerSLE)
                return {nullptr, tecOBJECT_NOT_FOUND};

            if (hasExpired(ctx.view, (*offerSLE)[~sfExpiration]))
            {
                // Before fixSecurity3_1_3, returning tecEXPIRED here left the
                // expired offer stranded on the ledger (doApply was never reached).
                // After the amendment, we pass the expired SLE through so doApply
                // can delete it before returning tecEXPIRED.
                if (!ctx.view.rules().enabled(fixSecurity3_1_3))
                    return {nullptr, tecEXPIRED};
            }

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
        if ((*bo)[sfNFTokenID] != (*so)[sfNFTokenID])
            return tecNFTOKEN_BUY_SELL_MISMATCH;

        if ((*bo)[sfAmount].asset() != (*so)[sfAmount].asset())
            return tecNFTOKEN_BUY_SELL_MISMATCH;

        // Reject self-trades: a broker may not facilitate a sale from an
        // account to itself.
        if (((*bo)[sfOwner] == (*so)[sfOwner]))
            return tecCANT_ACCEPT_OWN_NFTOKEN_OFFER;

        if ((*so)[sfAmount] > (*bo)[sfAmount])
            return tecINSUFFICIENT_PAYMENT;

        // In brokered mode the submitter is the broker; both offers'
        // Destination (if set) must name the broker.
        if (auto const dest = bo->at(~sfDestination); dest && *dest != ctx.tx[sfAccount])
            return tecNO_PERMISSION;

        if (auto const dest = so->at(~sfDestination); dest && *dest != ctx.tx[sfAccount])
            return tecNO_PERMISSION;

        // The broker fee is deducted from the buyer's amount first; what
        // remains must still satisfy the seller's ask (issuer royalty is
        // computed on the post-broker remainder in doApply).
        if (auto const brokerFee = ctx.tx[~sfNFTokenBrokerFee])
        {
            if (brokerFee->asset() != (*bo)[sfAmount].asset())
                return tecNFTOKEN_BUY_SELL_MISMATCH;

            if (brokerFee >= (*bo)[sfAmount])
                return tecINSUFFICIENT_PAYMENT;

            if ((*so)[sfAmount] > (*bo)[sfAmount] - *brokerFee)
                return tecINSUFFICIENT_PAYMENT;

            // Check if broker is allowed to receive the fee with these IOUs.
            if (!brokerFee->native() && ctx.view.rules().enabled(fixEnforceNFTokenTrustlineV2))
            {
                auto res = nft::checkTrustlineAuthorized(
                    ctx.view, ctx.tx[sfAccount], ctx.j, brokerFee->asset().get<Issue>());
                if (!isTesSuccess(res))
                    return res;

                res = nft::checkTrustlineDeepFrozen(
                    ctx.view, ctx.tx[sfAccount], ctx.j, brokerFee->asset().get<Issue>());
                if (!isTesSuccess(res))
                    return res;
            }
        }
    }

    if (bo)
    {
        if (((*bo)[sfFlags] & lsfSellNFToken) == lsfSellNFToken)
            return tecNFTOKEN_OFFER_TYPE_MISMATCH;

        if ((*bo)[sfOwner] == ctx.tx[sfAccount])
            return tecCANT_ACCEPT_OWN_NFTOKEN_OFFER;

        // In direct mode the submitter must own the token they are selling.
        if (!so && !nft::findToken(ctx.view, ctx.tx[sfAccount], (*bo)[sfNFTokenID]))
            return tecNO_PERMISSION;

        if (!so)
        {
            if (auto const dest = bo->at(~sfDestination);
                dest.has_value() && *dest != ctx.tx[sfAccount])
                return tecNO_PERMISSION;
        }

        // An IOU issuer may buy an NFT with their own currency; accountFunds
        // handles this by treating the issuer's own IOU as zero-cost.
        auto const needed = bo->at(sfAmount);

        if (accountFunds(ctx.view, (*bo)[sfOwner], needed, FreezeHandling::ZeroIfFrozen, ctx.j) <
            needed)
            return tecINSUFFICIENT_FUNDS;

        // Trust-line checks for the IOU payment: buyer must be authorized and
        // the seller (direct mode submitter) must be authorized and not deep-
        // frozen. Skipped in brokered mode — the broker is not the IOU recipient.
        if (ctx.view.rules().enabled(fixEnforceNFTokenTrustlineV2) && !needed.native())
        {
            auto res = nft::checkTrustlineAuthorized(
                ctx.view, bo->at(sfOwner), ctx.j, needed.asset().get<Issue>());
            if (!isTesSuccess(res))
                return res;

            if (!so)
            {
                res = nft::checkTrustlineAuthorized(
                    ctx.view, ctx.tx[sfAccount], ctx.j, needed.asset().get<Issue>());
                if (!isTesSuccess(res))
                    return res;

                res = nft::checkTrustlineDeepFrozen(
                    ctx.view, ctx.tx[sfAccount], ctx.j, needed.asset().get<Issue>());
                if (!isTesSuccess(res))
                    return res;
            }
        }
    }

    if (so)
    {
        if (((*so)[sfFlags] & lsfSellNFToken) != lsfSellNFToken)
            return tecNFTOKEN_OFFER_TYPE_MISMATCH;

        if ((*so)[sfOwner] == ctx.tx[sfAccount])
            return tecCANT_ACCEPT_OWN_NFTOKEN_OFFER;

        if (!nft::findToken(ctx.view, (*so)[sfOwner], (*so)[sfNFTokenID]))
            return tecNO_PERMISSION;

        if (!bo)
        {
            if (auto const dest = so->at(~sfDestination);
                dest.has_value() && *dest != ctx.tx[sfAccount])
                return tecNO_PERMISSION;
        }

        auto const needed = so->at(sfAmount);
        if (!bo)
        {
            // In brokered mode this check is skipped: the buyer's solvency is
            // already confirmed via the buy-offer check, and checking the
            // broker's balance here would produce a spurious tecINSUFFICIENT_FUNDS.
            if (accountFunds(
                    ctx.view, ctx.tx[sfAccount], needed, FreezeHandling::ZeroIfFrozen, ctx.j) <
                needed)
                return tecINSUFFICIENT_FUNDS;
        }

        if (!needed.native())
        {
            if (ctx.view.rules().enabled(fixEnforceNFTokenTrustlineV2))
            {
                auto res = nft::checkTrustlineAuthorized(
                    ctx.view, (*so)[sfOwner], ctx.j, needed.asset().get<Issue>());
                if (!isTesSuccess(res))
                    return res;

                if (!bo)
                {
                    res = nft::checkTrustlineAuthorized(
                        ctx.view, ctx.tx[sfAccount], ctx.j, needed.asset().get<Issue>());
                    if (!isTesSuccess(res))
                        return res;
                }
            }

            auto const res = nft::checkTrustlineDeepFrozen(
                ctx.view, (*so)[sfOwner], ctx.j, needed.asset().get<Issue>());
            if (!isTesSuccess(res))
                return res;
        }
    }

    // When a transfer fee is set on the token, the issuer is an additional
    // IOU payment recipient and must pass trust-line hygiene checks.
    auto const& offer = bo ? bo : so;
    if (!offer)
    {
        // Purely defensive, should be caught in preflight.
        return tecINTERNAL;  // LCOV_EXCL_LINE
    }

    auto const& tokenID = offer->at(sfNFTokenID);
    auto const& amount = offer->at(sfAmount);
    auto const nftMinter = nft::getIssuer(tokenID);

    if (nft::getTransferFee(tokenID) != 0 && !amount.native())
    {
        // fixEnforceNFTokenTrustline: if the token lacks kFLAG_CREATE_TRUST_LINES
        // and no trust line already exists between the issuer and the IOU issuer,
        // the transfer would silently create an unintended trust line for the royalty
        // payment. Reject unless the NFT issuer is also the IOU issuer (self-payment).
        if (ctx.view.rules().enabled(fixEnforceNFTokenTrustline) &&
            (nft::getFlags(tokenID) & nft::kFLAG_CREATE_TRUST_LINES) == 0 &&
            nftMinter != amount.getIssuer() &&
            !ctx.view.read(keylet::line(nftMinter, amount.get<Issue>())))
            return tecNO_LINE;

        if (ctx.view.rules().enabled(fixEnforceNFTokenTrustlineV2))
        {
            auto res = nft::checkTrustlineAuthorized(
                ctx.view, nftMinter, ctx.j, amount.asset().get<Issue>());
            if (!isTesSuccess(res))
                return res;

            res = nft::checkTrustlineDeepFrozen(
                ctx.view, nftMinter, ctx.j, amount.asset().get<Issue>());
            if (!isTesSuccess(res))
                return res;
        }
    }

    return tesSUCCESS;
}

TER
NFTokenAcceptOffer::pay(AccountID const& from, AccountID const& to, STAmount const& amount)
{
    if (amount < beast::kZERO)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    auto const result = accountSend(view(), from, to, amount, j_);

    // IOU transfer fees can leave the sender with a small negative or the
    // receiver (when they are the IOU issuer) with a positive balance in their
    // own currency. Check both parties after every payment to catch these
    // edge cases before they corrupt the ledger.
    if (!isTesSuccess(result))
        return result;
    if (accountFunds(view(), from, amount, FreezeHandling::ZeroIfFrozen, j_).signum() < 0)
        return tecINSUFFICIENT_FUNDS;
    if (accountFunds(view(), to, amount, FreezeHandling::ZeroIfFrozen, j_).signum() < 0)
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

    if (auto const ret = nft::removeToken(view(), seller, nftokenID, tokenAndPage->page);
        !isTesSuccess(ret))
        return ret;

    auto const sleBuyer = view().read(keylet::account(buyer));
    if (!sleBuyer)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    std::uint32_t const buyerOwnerCountBefore = sleBuyer->getFieldU32(sfOwnerCount);

    auto const insertRet = nft::insertToken(view(), buyer, std::move(tokenAndPage->token));

    if (view().rules().enabled(fixNFTokenReserve))
    {
        // Use the buyer's current post-purchase balance, not preFeeBalance_.
        // The tx fee has already been deducted, so the effective reserve bar
        // is a few drops higher than the owner-count formula alone would imply.
        auto const buyerBalance = sleBuyer->getFieldAmount(sfBalance);

        auto const buyerOwnerCountAfter = sleBuyer->getFieldU32(sfOwnerCount);
        if (buyerOwnerCountAfter > buyerOwnerCountBefore)
        {
            if (auto const reserve = view().fees().accountReserve(buyerOwnerCountAfter);
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

    if (auto amount = offer->getFieldAmount(sfAmount); amount != beast::kZERO)
    {
        if (auto const fee = nft::getTransferFee(nftokenID); fee != 0)
        {
            auto const cut = multiply(amount, nft::transferFeeAsRate(fee));

            // Skip the royalty when either party is the issuer: paying the
            // issuer their own cut would be a no-op or nonsensical self-transfer.
            if (auto const issuer = nft::getIssuer(nftokenID);
                cut != beast::kZERO && seller != issuer && buyer != issuer)
            {
                if (auto const r = pay(buyer, issuer, cut); !isTesSuccess(r))
                    return r;
                amount -= cut;
            }
        }

        if (auto const r = pay(buyer, seller, amount); !isTesSuccess(r))
            return r;
    }

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

    // Under fixSecurity3_1_3, expired offers are passed through preclaim so we
    // can delete them here, preventing them from becoming permanently stranded.
    if (view().rules().enabled(fixSecurity3_1_3))
    {
        bool foundExpired = false;

        auto const deleteOfferIfExpired =
            [this, &foundExpired](std::shared_ptr<SLE> const& offer) -> TER {
            if (offer && hasExpired(view(), (*offer)[~sfExpiration]))
            {
                JLOG(j_.trace()) << "Offer is expired, deleting: " << offer->key();
                if (!nft::deleteTokenOffer(view(), offer))
                {
                    // LCOV_EXCL_START
                    JLOG(j_.fatal())
                        << "Unable to delete expired offer '" << offer->key() << "': ignoring";
                    return tecINTERNAL;
                    // LCOV_EXCL_STOP
                }
                JLOG(j_.trace()) << "Deleted offer " << offer->key();
                foundExpired = true;
            }
            return tesSUCCESS;
        };

        if (auto const r = deleteOfferIfExpired(bo); !isTesSuccess(r))
            return r;
        if (auto const r = deleteOfferIfExpired(so); !isTesSuccess(r))
            return r;

        if (foundExpired)
            return tecEXPIRED;
    }

    if (bo && !nft::deleteTokenOffer(view(), bo))
    {
        // LCOV_EXCL_START
        JLOG(j_.fatal()) << "Unable to delete buy offer '" << to_string(bo->key()) << "': ignoring";
        return tecINTERNAL;
        // LCOV_EXCL_STOP
    }

    if (so && !nft::deleteTokenOffer(view(), so))
    {
        // LCOV_EXCL_START
        JLOG(j_.fatal()) << "Unable to delete sell offer '" << to_string(so->key())
                         << "': ignoring";
        return tecINTERNAL;
        // LCOV_EXCL_STOP
    }

    if (bo && so)
    {
        AccountID const buyer = (*bo)[sfOwner];
        AccountID const seller = (*so)[sfOwner];

        auto const nftokenID = (*so)[sfNFTokenID];

        STAmount amount = (*bo)[sfAmount];

        // Payment ordering in brokered mode is strict and critical to correctness:
        // 1. Broker receives their fee from the buyer's full amount.
        // 2. Issuer royalty is computed on what remains after the broker cut.
        // 3. Seller receives the final remainder.
        //
        // Computing the issuer's royalty before removing the broker fee would
        // allow total disbursements to exceed what the buyer authorised.
        if (auto const cut = ctx_.tx[~sfNFTokenBrokerFee]; cut && cut.value() != beast::kZERO)
        {
            if (auto const r = pay(buyer, account_, cut.value()); !isTesSuccess(r))
                return r;

            amount -= cut.value();
        }

        if (auto const fee = nft::getTransferFee(nftokenID); amount != beast::kZERO && fee != 0)
        {
            auto cut = multiply(amount, nft::transferFeeAsRate(fee));

            if (auto const issuer = nft::getIssuer(nftokenID); seller != issuer && buyer != issuer)
            {
                if (auto const r = pay(buyer, issuer, cut); !isTesSuccess(r))
                    return r;

                amount -= cut;
            }
        }

        if (amount > beast::kZERO)
        {
            if (auto const r = pay(buyer, seller, amount); !isTesSuccess(r))
                return r;
        }

        return transferNFToken(buyer, seller, nftokenID);
    }

    if (bo)
        return acceptOffer(bo);

    if (so)
        return acceptOffer(so);

    return tecINTERNAL;  // LCOV_EXCL_LINE
}

void
NFTokenAcceptOffer::visitInvariantEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const&)
{
    // No transaction-specific invariants yet (future work).
}

bool
NFTokenAcceptOffer::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    // No transaction-specific invariants yet (future work).
    return true;
}

}  // namespace xrpl
