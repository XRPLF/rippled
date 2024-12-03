//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#include <xrpld/app/ledger/OrderBookDB.h>
#include <xrpld/app/paths/Flow.h>
#include <xrpld/app/tx/detail/CreateOffer.h>
#include <xrpld/ledger/PaymentSandbox.h>
#include <xrpl/beast/utility/WrappedSink.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Quality.h>
#include <xrpl/protocol/st.h>

namespace ripple {

TxConsequences
CreateOffer::makeTxConsequences(PreflightContext const& ctx)
{
    auto calculateMaxXRPSpend = [](STTx const& tx) -> XRPAmount {
        auto const& amount{tx[sfTakerGets]};
        return amount.native() ? amount.xrp() : beast::zero;
    };

    return TxConsequences{ctx.tx, calculateMaxXRPSpend(ctx.tx)};
}

NotTEC
CreateOffer::preflight(PreflightContext const& ctx)
{
    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    auto& tx = ctx.tx;
    auto& j = ctx.j;

    std::uint32_t const uTxFlags = tx.getFlags();

    if (uTxFlags & tfOfferCreateMask)
    {
        JLOG(j.debug()) << "Malformed transaction: Invalid flags set.";
        return temINVALID_FLAG;
    }

    bool const bImmediateOrCancel(uTxFlags & tfImmediateOrCancel);
    bool const bFillOrKill(uTxFlags & tfFillOrKill);

    if (bImmediateOrCancel && bFillOrKill)
    {
        JLOG(j.debug()) << "Malformed transaction: both IoC and FoK set.";
        return temINVALID_FLAG;
    }

    bool const bHaveExpiration(tx.isFieldPresent(sfExpiration));

    if (bHaveExpiration && (tx.getFieldU32(sfExpiration) == 0))
    {
        JLOG(j.debug()) << "Malformed offer: bad expiration";
        return temBAD_EXPIRATION;
    }

    if (auto const cancelSequence = tx[~sfOfferSequence];
        cancelSequence && *cancelSequence == 0)
    {
        JLOG(j.debug()) << "Malformed offer: bad cancel sequence";
        return temBAD_SEQUENCE;
    }

    STAmount saTakerPays = tx[sfTakerPays];
    STAmount saTakerGets = tx[sfTakerGets];

    if (!isLegalNet(saTakerPays) || !isLegalNet(saTakerGets))
        return temBAD_AMOUNT;

    if (saTakerPays.native() && saTakerGets.native())
    {
        JLOG(j.debug()) << "Malformed offer: redundant (XRP for XRP)";
        return temBAD_OFFER;
    }
    if (saTakerPays <= beast::zero || saTakerGets <= beast::zero)
    {
        JLOG(j.debug()) << "Malformed offer: bad amount";
        return temBAD_OFFER;
    }

    auto const& uPaysIssuerID = saTakerPays.getIssuer();
    auto const& uPaysCurrency = saTakerPays.getCurrency();

    auto const& uGetsIssuerID = saTakerGets.getIssuer();
    auto const& uGetsCurrency = saTakerGets.getCurrency();

    if (uPaysCurrency == uGetsCurrency && uPaysIssuerID == uGetsIssuerID)
    {
        JLOG(j.debug()) << "Malformed offer: redundant (IOU for IOU)";
        return temREDUNDANT;
    }
    // We don't allow a non-native currency to use the currency code XRP.
    if (badCurrency() == uPaysCurrency || badCurrency() == uGetsCurrency)
    {
        JLOG(j.debug()) << "Malformed offer: bad currency";
        return temBAD_CURRENCY;
    }

    if (saTakerPays.native() != !uPaysIssuerID ||
        saTakerGets.native() != !uGetsIssuerID)
    {
        JLOG(j.debug()) << "Malformed offer: bad issuer";
        return temBAD_ISSUER;
    }

    return preflight2(ctx);
}

TER
CreateOffer::preclaim(PreclaimContext const& ctx)
{
    auto const id = ctx.tx[sfAccount];

    auto saTakerPays = ctx.tx[sfTakerPays];
    auto saTakerGets = ctx.tx[sfTakerGets];

    auto const& uPaysIssuerID = saTakerPays.getIssuer();
    auto const& uPaysCurrency = saTakerPays.getCurrency();

    auto const& uGetsIssuerID = saTakerGets.getIssuer();

    auto const cancelSequence = ctx.tx[~sfOfferSequence];

    auto const sleCreator = ctx.view.read(keylet::account(id));
    if (!sleCreator)
        return terNO_ACCOUNT;

    std::uint32_t const uAccountSequence = sleCreator->getFieldU32(sfSequence);

    auto viewJ = ctx.app.journal("View");

    if (isGlobalFrozen(ctx.view, uPaysIssuerID) ||
        isGlobalFrozen(ctx.view, uGetsIssuerID))
    {
        JLOG(ctx.j.debug()) << "Offer involves frozen asset";
        return tecFROZEN;
    }

    if (accountFunds(ctx.view, id, saTakerGets, fhZERO_IF_FROZEN, viewJ) <=
        beast::zero)
    {
        JLOG(ctx.j.debug())
            << "delay: Offers must be at least partially funded.";
        return tecUNFUNDED_OFFER;
    }

    // This can probably be simplified to make sure that you cancel sequences
    // before the transaction sequence number.
    if (cancelSequence && (uAccountSequence <= *cancelSequence))
    {
        JLOG(ctx.j.debug()) << "uAccountSequenceNext=" << uAccountSequence
                            << " uOfferSequence=" << *cancelSequence;
        return temBAD_SEQUENCE;
    }

    if (hasExpired(ctx.view, ctx.tx[~sfExpiration]))
    {
        // Note that this will get checked again in applyGuts, but it saves
        // us a call to checkAcceptAsset and possible false negative.
        //
        // The return code change is attached to featureDepositPreauth as a
        // convenience, as the change is not big enough to deserve its own
        // amendment.
        return ctx.view.rules().enabled(featureDepositPreauth)
            ? TER{tecEXPIRED}
            : TER{tesSUCCESS};
    }

    // Make sure that we are authorized to hold what the taker will pay us.
    if (!saTakerPays.native())
    {
        auto result = checkAcceptAsset(
            ctx.view,
            ctx.flags,
            id,
            ctx.j,
            Issue(uPaysCurrency, uPaysIssuerID));
        if (result != tesSUCCESS)
            return result;
    }

    return tesSUCCESS;
}

TER
CreateOffer::checkAcceptAsset(
    ReadView const& view,
    ApplyFlags const flags,
    AccountID const id,
    beast::Journal const j,
    Issue const& issue)
{
    // Only valid for custom currencies
    ASSERT(
        !isXRP(issue.currency),
        "ripple::CreateOffer::checkAcceptAsset : input is not XRP");

    auto const issuerAccount = view.read(keylet::account(issue.account));

    if (!issuerAccount)
    {
        JLOG(j.debug())
            << "delay: can't receive IOUs from non-existent issuer: "
            << to_string(issue.account);

        return (flags & tapRETRY) ? TER{terNO_ACCOUNT} : TER{tecNO_ISSUER};
    }

    // This code is attached to the DepositPreauth amendment as a matter of
    // convenience.  The change is not significant enough to deserve its
    // own amendment.
    if (view.rules().enabled(featureDepositPreauth) && (issue.account == id))
        // An account can always accept its own issuance.
        return tesSUCCESS;

    if ((*issuerAccount)[sfFlags] & lsfRequireAuth)
    {
        auto const trustLine =
            view.read(keylet::line(id, issue.account, issue.currency));

        if (!trustLine)
        {
            return (flags & tapRETRY) ? TER{terNO_LINE} : TER{tecNO_LINE};
        }

        // Entries have a canonical representation, determined by a
        // lexicographical "greater than" comparison employing strict weak
        // ordering. Determine which entry we need to access.
        bool const canonical_gt(id > issue.account);

        bool const is_authorized(
            (*trustLine)[sfFlags] & (canonical_gt ? lsfLowAuth : lsfHighAuth));

        if (!is_authorized)
        {
            JLOG(j.debug())
                << "delay: can't receive IOUs from issuer without auth.";

            return (flags & tapRETRY) ? TER{terNO_AUTH} : TER{tecNO_AUTH};
        }
    }

    return tesSUCCESS;
}

bool
CreateOffer::dry_offer(ApplyView& view, Offer const& offer)
{
    if (offer.fully_consumed())
        return true;
    auto const amount = accountFunds(
        view,
        offer.owner(),
        offer.amount().out,
        fhZERO_IF_FROZEN,
        ctx_.app.journal("View"));
    return (amount <= beast::zero);
}

std::pair<bool, Quality>
CreateOffer::select_path(
    bool have_direct,
    OfferStream const& direct,
    bool have_bridge,
    OfferStream const& leg1,
    OfferStream const& leg2)
{
    // If we don't have any viable path, why are we here?!
    ASSERT(
        have_direct || have_bridge,
        "ripple::CreateOffer::select_path : valid inputs");

    // If there's no bridged path, the direct is the best by default.
    if (!have_bridge)
        return std::make_pair(true, direct.tip().quality());

    Quality const bridged_quality(
        composed_quality(leg1.tip().quality(), leg2.tip().quality()));

    if (have_direct)
    {
        // We compare the quality of the composed quality of the bridged
        // offers and compare it against the direct offer to pick the best.
        Quality const direct_quality(direct.tip().quality());

        if (bridged_quality < direct_quality)
            return std::make_pair(true, direct_quality);
    }

    // Either there was no direct offer, or it didn't have a better quality
    // than the bridge.
    return std::make_pair(false, bridged_quality);
}

bool
CreateOffer::reachedOfferCrossingLimit(Taker const& taker) const
{
    auto const crossings =
        taker.get_direct_crossings() + (2 * taker.get_bridge_crossings());

    // The crossing limit is part of the Ripple protocol and
    // changing it is a transaction-processing change.
    return crossings >= 850;
}

std::pair<TER, Amounts>
CreateOffer::bridged_cross(
    Taker& taker,
    ApplyView& view,
    ApplyView& view_cancel,
    NetClock::time_point const when)
{
    auto const& takerAmount = taker.original_offer();

    ASSERT(
        !isXRP(takerAmount.in) && !isXRP(takerAmount.out),
        "ripple::CreateOffer::bridged_cross : neither is XRP");

    if (isXRP(takerAmount.in) || isXRP(takerAmount.out))
        Throw<std::logic_error>("Bridging with XRP and an endpoint.");

    OfferStream offers_direct(
        view,
        view_cancel,
        Book(taker.issue_in(), taker.issue_out()),
        when,
        stepCounter_,
        j_);

    OfferStream offers_leg1(
        view,
        view_cancel,
        Book(taker.issue_in(), xrpIssue()),
        when,
        stepCounter_,
        j_);

    OfferStream offers_leg2(
        view,
        view_cancel,
        Book(xrpIssue(), taker.issue_out()),
        when,
        stepCounter_,
        j_);

    TER cross_result = tesSUCCESS;

    // Note the subtle distinction here: self-offers encountered in the
    // bridge are taken, but self-offers encountered in the direct book
    // are not.
    bool have_bridge = offers_leg1.step() && offers_leg2.step();
    bool have_direct = step_account(offers_direct, taker);
    int count = 0;

    auto viewJ = ctx_.app.journal("View");

    // Modifying the order or logic of the operations in the loop will cause
    // a protocol breaking change.
    while (have_direct || have_bridge)
    {
        bool leg1_consumed = false;
        bool leg2_consumed = false;
        bool direct_consumed = false;

        auto const [use_direct, quality] = select_path(
            have_direct, offers_direct, have_bridge, offers_leg1, offers_leg2);

        // We are always looking at the best quality; we are done with
        // crossing as soon as we cross the quality boundary.
        if (taker.reject(quality))
            break;

        count++;

        if (use_direct)
        {
            if (auto stream = j_.debug())
            {
                stream << count << " Direct:";
                stream << "  offer: " << offers_direct.tip();
                stream << "     in: " << offers_direct.tip().amount().in;
                stream << "    out: " << offers_direct.tip().amount().out;
                stream << "  owner: " << offers_direct.tip().owner();
                stream << "  funds: "
                       << accountFunds(
                              view,
                              offers_direct.tip().owner(),
                              offers_direct.tip().amount().out,
                              fhIGNORE_FREEZE,
                              viewJ);
            }

            cross_result = taker.cross(offers_direct.tip());

            JLOG(j_.debug()) << "Direct Result: " << transToken(cross_result);

            if (dry_offer(view, offers_direct.tip()))
            {
                direct_consumed = true;
                have_direct = step_account(offers_direct, taker);
            }
        }
        else
        {
            if (auto stream = j_.debug())
            {
                auto const owner1_funds_before = accountFunds(
                    view,
                    offers_leg1.tip().owner(),
                    offers_leg1.tip().amount().out,
                    fhIGNORE_FREEZE,
                    viewJ);

                auto const owner2_funds_before = accountFunds(
                    view,
                    offers_leg2.tip().owner(),
                    offers_leg2.tip().amount().out,
                    fhIGNORE_FREEZE,
                    viewJ);

                stream << count << " Bridge:";
                stream << " offer1: " << offers_leg1.tip();
                stream << "     in: " << offers_leg1.tip().amount().in;
                stream << "    out: " << offers_leg1.tip().amount().out;
                stream << "  owner: " << offers_leg1.tip().owner();
                stream << "  funds: " << owner1_funds_before;
                stream << " offer2: " << offers_leg2.tip();
                stream << "     in: " << offers_leg2.tip().amount().in;
                stream << "    out: " << offers_leg2.tip().amount().out;
                stream << "  owner: " << offers_leg2.tip().owner();
                stream << "  funds: " << owner2_funds_before;
            }

            cross_result = taker.cross(offers_leg1.tip(), offers_leg2.tip());

            JLOG(j_.debug()) << "Bridge Result: " << transToken(cross_result);

            if (view.rules().enabled(fixTakerDryOfferRemoval))
            {
                // have_bridge can be true the next time 'round only if
                // neither of the OfferStreams are dry.
                leg1_consumed = dry_offer(view, offers_leg1.tip());
                if (leg1_consumed)
                    have_bridge &= offers_leg1.step();

                leg2_consumed = dry_offer(view, offers_leg2.tip());
                if (leg2_consumed)
                    have_bridge &= offers_leg2.step();
            }
            else
            {
                // This old behavior may leave an empty offer in the book for
                // the second leg.
                if (dry_offer(view, offers_leg1.tip()))
                {
                    leg1_consumed = true;
                    have_bridge = (have_bridge && offers_leg1.step());
                }
                if (dry_offer(view, offers_leg2.tip()))
                {
                    leg2_consumed = true;
                    have_bridge = (have_bridge && offers_leg2.step());
                }
            }
        }

        if (cross_result != tesSUCCESS)
        {
            cross_result = tecFAILED_PROCESSING;
            break;
        }

        if (taker.done())
        {
            JLOG(j_.debug()) << "The taker reports he's done during crossing!";
            break;
        }

        if (reachedOfferCrossingLimit(taker))
        {
            JLOG(j_.debug()) << "The offer crossing limit has been exceeded!";
            break;
        }

        // Postcondition: If we aren't done, then we *must* have consumed at
        //                least one offer fully.
        ASSERT(
            direct_consumed || leg1_consumed || leg2_consumed,
            "ripple::CreateOffer::bridged_cross : consumed an offer");

        if (!direct_consumed && !leg1_consumed && !leg2_consumed)
            Throw<std::logic_error>(
                "bridged crossing: nothing was fully consumed.");
    }

    return std::make_pair(cross_result, taker.remaining_offer());
}

std::pair<TER, Amounts>
CreateOffer::direct_cross(
    Taker& taker,
    ApplyView& view,
    ApplyView& view_cancel,
    NetClock::time_point const when)
{
    OfferStream offers(
        view,
        view_cancel,
        Book(taker.issue_in(), taker.issue_out()),
        when,
        stepCounter_,
        j_);

    TER cross_result(tesSUCCESS);
    int count = 0;

    bool have_offer = step_account(offers, taker);

    // Modifying the order or logic of the operations in the loop will cause
    // a protocol breaking change.
    while (have_offer)
    {
        bool direct_consumed = false;
        auto& offer(offers.tip());

        // We are done with crossing as soon as we cross the quality boundary
        if (taker.reject(offer.quality()))
            break;

        count++;

        if (auto stream = j_.debug())
        {
            stream << count << " Direct:";
            stream << "  offer: " << offer;
            stream << "     in: " << offer.amount().in;
            stream << "    out: " << offer.amount().out;
            stream << "quality: " << offer.quality();
            stream << "  owner: " << offer.owner();
            stream << "  funds: "
                   << accountFunds(
                          view,
                          offer.owner(),
                          offer.amount().out,
                          fhIGNORE_FREEZE,
                          ctx_.app.journal("View"));
        }

        cross_result = taker.cross(offer);

        JLOG(j_.debug()) << "Direct Result: " << transToken(cross_result);

        if (dry_offer(view, offer))
        {
            direct_consumed = true;
            have_offer = step_account(offers, taker);
        }

        if (cross_result != tesSUCCESS)
        {
            cross_result = tecFAILED_PROCESSING;
            break;
        }

        if (taker.done())
        {
            JLOG(j_.debug()) << "The taker reports he's done during crossing!";
            break;
        }

        if (reachedOfferCrossingLimit(taker))
        {
            JLOG(j_.debug()) << "The offer crossing limit has been exceeded!";
            break;
        }

        // Postcondition: If we aren't done, then we *must* have consumed the
        //                offer on the books fully!
        ASSERT(
            direct_consumed,
            "ripple::CreateOffer::direct_cross : consumed an offer");

        if (!direct_consumed)
            Throw<std::logic_error>(
                "direct crossing: nothing was fully consumed.");
    }

    return std::make_pair(cross_result, taker.remaining_offer());
}

// Step through the stream for as long as possible, skipping any offers
// that are from the taker or which cross the taker's threshold.
// Return false if the is no offer in the book, true otherwise.
bool
CreateOffer::step_account(OfferStream& stream, Taker const& taker)
{
    while (stream.step())
    {
        auto const& offer = stream.tip();

        // This offer at the tip crosses the taker's threshold. We're done.
        if (taker.reject(offer.quality()))
            return true;

        // This offer at the tip is not from the taker. We're done.
        if (offer.owner() != taker.account())
            return true;
    }

    // We ran out of offers. Can't advance.
    return false;
}

// Fill as much of the offer as possible by consuming offers
// already on the books. Return the status and the amount of
// the offer to left unfilled.
std::pair<TER, Amounts>
CreateOffer::takerCross(
    Sandbox& sb,
    Sandbox& sbCancel,
    Amounts const& takerAmount)
{
    NetClock::time_point const when = sb.parentCloseTime();

    beast::WrappedSink takerSink(j_, "Taker ");

    Taker taker(
        cross_type_,
        sb,
        account_,
        takerAmount,
        ctx_.tx.getFlags(),
        beast::Journal(takerSink));

    // If the taker is unfunded before we begin crossing
    // there's nothing to do - just return an error.
    //
    // We check this in preclaim, but when selling XRP
    // charged fees can cause a user's available balance
    // to go to 0 (by causing it to dip below the reserve)
    // so we check this case again.
    if (taker.unfunded())
    {
        JLOG(j_.debug()) << "Not crossing: taker is unfunded.";
        return {tecUNFUNDED_OFFER, takerAmount};
    }

    try
    {
        if (cross_type_ == CrossType::IouToIou)
            return bridged_cross(taker, sb, sbCancel, when);

        return direct_cross(taker, sb, sbCancel, when);
    }
    catch (std::exception const& e)
    {
        JLOG(j_.error()) << "Exception during offer crossing: " << e.what();
        return {tecINTERNAL, taker.remaining_offer()};
    }
}

std::pair<TER, Amounts>
CreateOffer::flowCross(
    PaymentSandbox& psb,
    PaymentSandbox& psbCancel,
    Amounts const& takerAmount)
{
    try
    {
        // If the taker is unfunded before we begin crossing there's nothing
        // to do - just return an error.
        //
        // We check this in preclaim, but when selling XRP charged fees can
        // cause a user's available balance to go to 0 (by causing it to dip
        // below the reserve) so we check this case again.
        STAmount const inStartBalance =
            accountFunds(psb, account_, takerAmount.in, fhZERO_IF_FROZEN, j_);
        if (inStartBalance <= beast::zero)
        {
            // The account balance can't cover even part of the offer.
            JLOG(j_.debug()) << "Not crossing: taker is unfunded.";
            return {tecUNFUNDED_OFFER, takerAmount};
        }

        // If the gateway has a transfer rate, accommodate that.  The
        // gateway takes its cut without any special consent from the
        // offer taker.  Set sendMax to allow for the gateway's cut.
        Rate gatewayXferRate{QUALITY_ONE};
        STAmount sendMax = takerAmount.in;
        if (!sendMax.native() && (account_ != sendMax.getIssuer()))
        {
            gatewayXferRate = transferRate(psb, sendMax.getIssuer());
            if (gatewayXferRate.value != QUALITY_ONE)
            {
                sendMax = multiplyRound(
                    takerAmount.in,
                    gatewayXferRate,
                    takerAmount.in.issue(),
                    true);
            }
        }

        // Payment flow code compares quality after the transfer rate is
        // included.  Since transfer rate is incorporated compute threshold.
        Quality threshold{takerAmount.out, sendMax};

        // If we're creating a passive offer adjust the threshold so we only
        // cross offers that have a better quality than this one.
        std::uint32_t const txFlags = ctx_.tx.getFlags();
        if (txFlags & tfPassive)
            ++threshold;

        // Don't send more than our balance.
        if (sendMax > inStartBalance)
            sendMax = inStartBalance;

        // Always invoke flow() with the default path.  However if neither
        // of the takerAmount currencies are XRP then we cross through an
        // additional path with XRP as the intermediate between two books.
        // This second path we have to build ourselves.
        STPathSet paths;
        if (!takerAmount.in.native() && !takerAmount.out.native())
        {
            STPath path;
            path.emplace_back(std::nullopt, xrpCurrency(), std::nullopt);
            paths.emplace_back(std::move(path));
        }
        // Special handling for the tfSell flag.
        STAmount deliver = takerAmount.out;
        OfferCrossing offerCrossing = OfferCrossing::yes;
        if (txFlags & tfSell)
        {
            offerCrossing = OfferCrossing::sell;
            // We are selling, so we will accept *more* than the offer
            // specified.  Since we don't know how much they might offer,
            // we allow delivery of the largest possible amount.
            if (deliver.native())
                deliver = STAmount{STAmount::cMaxNative};
            else
                // We can't use the maximum possible currency here because
                // there might be a gateway transfer rate to account for.
                // Since the transfer rate cannot exceed 200%, we use 1/2
                // maxValue for our limit.
                deliver = STAmount{
                    takerAmount.out.issue(),
                    STAmount::cMaxValue / 2,
                    STAmount::cMaxOffset};
        }

        // Call the payment engine's flow() to do the actual work.
        auto const result = flow(
            psb,
            deliver,
            account_,
            account_,
            paths,
            true,                       // default path
            !(txFlags & tfFillOrKill),  // partial payment
            true,                       // owner pays transfer fee
            offerCrossing,
            threshold,
            sendMax,
            j_);

        // If stale offers were found remove them.
        for (auto const& toRemove : result.removableOffers)
        {
            if (auto otr = psb.peek(keylet::offer(toRemove)))
                offerDelete(psb, otr, j_);
            if (auto otr = psbCancel.peek(keylet::offer(toRemove)))
                offerDelete(psbCancel, otr, j_);
        }

        // Determine the size of the final offer after crossing.
        auto afterCross = takerAmount;  // If !tesSUCCESS offer unchanged
        if (isTesSuccess(result.result()))
        {
            STAmount const takerInBalance = accountFunds(
                psb, account_, takerAmount.in, fhZERO_IF_FROZEN, j_);

            if (takerInBalance <= beast::zero)
            {
                // If offer crossing exhausted the account's funds don't
                // create the offer.
                afterCross.in.clear();
                afterCross.out.clear();
            }
            else
            {
                STAmount const rate{
                    Quality{takerAmount.out, takerAmount.in}.rate()};

                if (txFlags & tfSell)
                {
                    // If selling then scale the new out amount based on how
                    // much we sold during crossing.  This preserves the offer
                    // Quality,

                    // Reduce the offer that is placed by the crossed amount.
                    // Note that we must ignore the portion of the
                    // actualAmountIn that may have been consumed by a
                    // gateway's transfer rate.
                    STAmount nonGatewayAmountIn = result.actualAmountIn;
                    if (gatewayXferRate.value != QUALITY_ONE)
                        nonGatewayAmountIn = divideRound(
                            result.actualAmountIn,
                            gatewayXferRate,
                            takerAmount.in.issue(),
                            true);

                    afterCross.in -= nonGatewayAmountIn;

                    // It's possible that the divRound will cause our subtract
                    // to go slightly negative.  So limit afterCross.in to zero.
                    if (afterCross.in < beast::zero)
                        // We should verify that the difference *is* small, but
                        // what is a good threshold to check?
                        afterCross.in.clear();

                    afterCross.out = [&]() {
                        // Careful analysis showed that rounding up this
                        // divRound result could lead to placing a reduced
                        // offer in the ledger that blocks order books.  So
                        // the fixReducedOffersV1 amendment changes the
                        // behavior to round down instead.
                        if (psb.rules().enabled(fixReducedOffersV1))
                            return divRoundStrict(
                                afterCross.in,
                                rate,
                                takerAmount.out.issue(),
                                false);

                        return divRound(
                            afterCross.in, rate, takerAmount.out.issue(), true);
                    }();
                }
                else
                {
                    // If not selling, we scale the input based on the
                    // remaining output.  This too preserves the offer
                    // Quality.
                    afterCross.out -= result.actualAmountOut;
                    ASSERT(
                        afterCross.out >= beast::zero,
                        "ripple::CreateOffer::flowCross : minimum offer");
                    if (afterCross.out < beast::zero)
                        afterCross.out.clear();
                    afterCross.in = mulRound(
                        afterCross.out, rate, takerAmount.in.issue(), true);
                }
            }
        }

        // Return how much of the offer is left.
        return {tesSUCCESS, afterCross};
    }
    catch (std::exception const& e)
    {
        JLOG(j_.error()) << "Exception during offer crossing: " << e.what();
    }
    return {tecINTERNAL, takerAmount};
}

std::pair<TER, Amounts>
CreateOffer::cross(Sandbox& sb, Sandbox& sbCancel, Amounts const& takerAmount)
{
    if (sb.rules().enabled(featureFlowCross))
    {
        PaymentSandbox psbFlow{&sb};
        PaymentSandbox psbCancelFlow{&sbCancel};
        auto const ret = flowCross(psbFlow, psbCancelFlow, takerAmount);
        psbFlow.apply(sb);
        psbCancelFlow.apply(sbCancel);
        return ret;
    }

    Sandbox sbTaker{&sb};
    Sandbox sbCancelTaker{&sbCancel};
    auto const ret = takerCross(sbTaker, sbCancelTaker, takerAmount);
    sbTaker.apply(sb);
    sbCancelTaker.apply(sbCancel);
    return ret;
}

std::string
CreateOffer::format_amount(STAmount const& amount)
{
    std::string txt = amount.getText();
    txt += "/";
    txt += to_string(amount.issue().currency);
    return txt;
}

void
CreateOffer::preCompute()
{
    cross_type_ = CrossType::IouToIou;
    bool const pays_xrp = ctx_.tx.getFieldAmount(sfTakerPays).native();
    bool const gets_xrp = ctx_.tx.getFieldAmount(sfTakerGets).native();
    if (pays_xrp && !gets_xrp)
        cross_type_ = CrossType::IouToXrp;
    else if (gets_xrp && !pays_xrp)
        cross_type_ = CrossType::XrpToIou;

    return Transactor::preCompute();
}

std::pair<TER, bool>
CreateOffer::applyGuts(Sandbox& sb, Sandbox& sbCancel)
{
    using beast::zero;

    std::uint32_t const uTxFlags = ctx_.tx.getFlags();

    bool const bPassive(uTxFlags & tfPassive);
    bool const bImmediateOrCancel(uTxFlags & tfImmediateOrCancel);
    bool const bFillOrKill(uTxFlags & tfFillOrKill);
    bool const bSell(uTxFlags & tfSell);

    auto saTakerPays = ctx_.tx[sfTakerPays];
    auto saTakerGets = ctx_.tx[sfTakerGets];

    auto const cancelSequence = ctx_.tx[~sfOfferSequence];

    // Note that we we use the value from the sequence or ticket as the
    // offer sequence.  For more explanation see comments in SeqProxy.h.
    auto const offerSequence = ctx_.tx.getSeqProxy().value();

    // This is the original rate of the offer, and is the rate at which
    // it will be placed, even if crossing offers change the amounts that
    // end up on the books.
    auto uRate = getRate(saTakerGets, saTakerPays);

    auto viewJ = ctx_.app.journal("View");

    TER result = tesSUCCESS;

    // Process a cancellation request that's passed along with an offer.
    if (cancelSequence)
    {
        auto const sleCancel =
            sb.peek(keylet::offer(account_, *cancelSequence));

        // It's not an error to not find the offer to cancel: it might have
        // been consumed or removed. If it is found, however, it's an error
        // to fail to delete it.
        if (sleCancel)
        {
            JLOG(j_.debug()) << "Create cancels order " << *cancelSequence;
            result = offerDelete(sb, sleCancel, viewJ);
        }
    }

    auto const expiration = ctx_.tx[~sfExpiration];

    if (hasExpired(sb, expiration))
    {
        // If the offer has expired, the transaction has successfully
        // done nothing, so short circuit from here.
        //
        // The return code change is attached to featureDepositPreauth as a
        // convenience.  The change is not big enough to deserve a fix code.
        TER const ter{
            sb.rules().enabled(featureDepositPreauth) ? TER{tecEXPIRED}
                                                      : TER{tesSUCCESS}};
        return {ter, true};
    }

    bool const bOpenLedger = sb.open();
    bool crossed = false;

    if (result == tesSUCCESS)
    {
        // If a tick size applies, round the offer to the tick size
        auto const& uPaysIssuerID = saTakerPays.getIssuer();
        auto const& uGetsIssuerID = saTakerGets.getIssuer();

        std::uint8_t uTickSize = Quality::maxTickSize;
        if (!isXRP(uPaysIssuerID))
        {
            auto const sle = sb.read(keylet::account(uPaysIssuerID));
            if (sle && sle->isFieldPresent(sfTickSize))
                uTickSize = std::min(uTickSize, (*sle)[sfTickSize]);
        }
        if (!isXRP(uGetsIssuerID))
        {
            auto const sle = sb.read(keylet::account(uGetsIssuerID));
            if (sle && sle->isFieldPresent(sfTickSize))
                uTickSize = std::min(uTickSize, (*sle)[sfTickSize]);
        }
        if (uTickSize < Quality::maxTickSize)
        {
            auto const rate =
                Quality{saTakerGets, saTakerPays}.round(uTickSize).rate();

            // We round the side that's not exact,
            // just as if the offer happened to execute
            // at a slightly better (for the placer) rate
            if (bSell)
            {
                // this is a sell, round taker pays
                saTakerPays = multiply(saTakerGets, rate, saTakerPays.issue());
            }
            else
            {
                // this is a buy, round taker gets
                saTakerGets = divide(saTakerPays, rate, saTakerGets.issue());
            }
            if (!saTakerGets || !saTakerPays)
            {
                JLOG(j_.debug()) << "Offer rounded to zero";
                return {result, true};
            }

            uRate = getRate(saTakerGets, saTakerPays);
        }

        // We reverse pays and gets because during crossing we are taking.
        Amounts const takerAmount(saTakerGets, saTakerPays);

        // The amount of the offer that is unfilled after crossing has been
        // performed. It may be equal to the original amount (didn't cross),
        // empty (fully crossed), or something in-between.
        Amounts place_offer;

        JLOG(j_.debug()) << "Attempting cross: "
                         << to_string(takerAmount.in.issue()) << " -> "
                         << to_string(takerAmount.out.issue());

        if (auto stream = j_.trace())
        {
            stream << "   mode: " << (bPassive ? "passive " : "")
                   << (bSell ? "sell" : "buy");
            stream << "     in: " << format_amount(takerAmount.in);
            stream << "    out: " << format_amount(takerAmount.out);
        }

        std::tie(result, place_offer) = cross(sb, sbCancel, takerAmount);

        // We expect the implementation of cross to succeed
        // or give a tec.
        ASSERT(
            result == tesSUCCESS || isTecClaim(result),
            "ripple::CreateOffer::applyGuts : result is tesSUCCESS or "
            "tecCLAIM");

        if (auto stream = j_.trace())
        {
            stream << "Cross result: " << transToken(result);
            stream << "     in: " << format_amount(place_offer.in);
            stream << "    out: " << format_amount(place_offer.out);
        }

        if (result == tecFAILED_PROCESSING && bOpenLedger)
            result = telFAILED_PROCESSING;

        if (result != tesSUCCESS)
        {
            JLOG(j_.debug()) << "final result: " << transToken(result);
            return {result, true};
        }

        ASSERT(
            saTakerGets.issue() == place_offer.in.issue(),
            "ripple::CreateOffer::applyGuts : taker gets issue match");
        ASSERT(
            saTakerPays.issue() == place_offer.out.issue(),
            "ripple::CreateOffer::applyGuts : taker pays issue match");

        if (takerAmount != place_offer)
            crossed = true;

        // The offer that we need to place after offer crossing should
        // never be negative. If it is, something went very very wrong.
        if (place_offer.in < zero || place_offer.out < zero)
        {
            JLOG(j_.fatal()) << "Cross left offer negative!"
                             << "     in: " << format_amount(place_offer.in)
                             << "    out: " << format_amount(place_offer.out);
            return {tefINTERNAL, true};
        }

        if (place_offer.in == zero || place_offer.out == zero)
        {
            JLOG(j_.debug()) << "Offer fully crossed!";
            return {result, true};
        }

        // We now need to adjust the offer to reflect the amount left after
        // crossing. We reverse in and out here, since during crossing we
        // were the taker.
        saTakerPays = place_offer.out;
        saTakerGets = place_offer.in;
    }

    ASSERT(
        saTakerPays > zero && saTakerGets > zero,
        "ripple::CreateOffer::applyGuts : taker pays and gets positive");

    if (result != tesSUCCESS)
    {
        JLOG(j_.debug()) << "final result: " << transToken(result);
        return {result, true};
    }

    if (auto stream = j_.trace())
    {
        stream << "Place" << (crossed ? " remaining " : " ") << "offer:";
        stream << "    Pays: " << saTakerPays.getFullText();
        stream << "    Gets: " << saTakerGets.getFullText();
    }

    // For 'fill or kill' offers, failure to fully cross means that the
    // entire operation should be aborted, with only fees paid.
    if (bFillOrKill)
    {
        JLOG(j_.trace()) << "Fill or Kill: offer killed";
        if (sb.rules().enabled(fix1578))
            return {tecKILLED, false};
        return {tesSUCCESS, false};
    }

    // For 'immediate or cancel' offers, the amount remaining doesn't get
    // placed - it gets canceled and the operation succeeds.
    if (bImmediateOrCancel)
    {
        JLOG(j_.trace()) << "Immediate or cancel: offer canceled";
        if (!crossed && sb.rules().enabled(featureImmediateOfferKilled))
            // If the ImmediateOfferKilled amendment is enabled, any
            // ImmediateOrCancel offer that transfers absolutely no funds
            // returns tecKILLED rather than tesSUCCESS.  Motivation for the
            // change is here: https://github.com/ripple/rippled/issues/4115
            return {tecKILLED, false};
        return {tesSUCCESS, true};
    }

    auto const sleCreator = sb.peek(keylet::account(account_));
    if (!sleCreator)
        return {tefINTERNAL, false};

    {
        XRPAmount reserve =
            sb.fees().accountReserve(sleCreator->getFieldU32(sfOwnerCount) + 1);

        if (mPriorBalance < reserve)
        {
            // If we are here, the signing account had an insufficient reserve
            // *prior* to our processing. If something actually crossed, then
            // we allow this; otherwise, we just claim a fee.
            if (!crossed)
                result = tecINSUF_RESERVE_OFFER;

            if (result != tesSUCCESS)
            {
                JLOG(j_.debug()) << "final result: " << transToken(result);
            }

            return {result, true};
        }
    }

    // We need to place the remainder of the offer into its order book.
    auto const offer_index = keylet::offer(account_, offerSequence);

    // Add offer to owner's directory.
    auto const ownerNode = sb.dirInsert(
        keylet::ownerDir(account_), offer_index, describeOwnerDir(account_));

    if (!ownerNode)
    {
        JLOG(j_.debug())
            << "final result: failed to add offer to owner's directory";
        return {tecDIR_FULL, true};
    }

    // Update owner count.
    adjustOwnerCount(sb, sleCreator, 1, viewJ);

    JLOG(j_.trace()) << "adding to book: " << to_string(saTakerPays.issue())
                     << " : " << to_string(saTakerGets.issue());

    Book const book{saTakerPays.issue(), saTakerGets.issue()};

    // Add offer to order book, using the original rate
    // before any crossing occured.
    auto dir = keylet::quality(keylet::book(book), uRate);
    bool const bookExisted = static_cast<bool>(sb.peek(dir));

    auto const bookNode = sb.dirAppend(dir, offer_index, [&](SLE::ref sle) {
        sle->setFieldH160(sfTakerPaysCurrency, saTakerPays.issue().currency);
        sle->setFieldH160(sfTakerPaysIssuer, saTakerPays.issue().account);
        sle->setFieldH160(sfTakerGetsCurrency, saTakerGets.issue().currency);
        sle->setFieldH160(sfTakerGetsIssuer, saTakerGets.issue().account);
        sle->setFieldU64(sfExchangeRate, uRate);
    });

    if (!bookNode)
    {
        JLOG(j_.debug()) << "final result: failed to add offer to book";
        return {tecDIR_FULL, true};
    }

    auto sleOffer = std::make_shared<SLE>(offer_index);
    sleOffer->setAccountID(sfAccount, account_);
    sleOffer->setFieldU32(sfSequence, offerSequence);
    sleOffer->setFieldH256(sfBookDirectory, dir.key);
    sleOffer->setFieldAmount(sfTakerPays, saTakerPays);
    sleOffer->setFieldAmount(sfTakerGets, saTakerGets);
    sleOffer->setFieldU64(sfOwnerNode, *ownerNode);
    sleOffer->setFieldU64(sfBookNode, *bookNode);
    if (expiration)
        sleOffer->setFieldU32(sfExpiration, *expiration);
    if (bPassive)
        sleOffer->setFlag(lsfPassive);
    if (bSell)
        sleOffer->setFlag(lsfSell);
    sb.insert(sleOffer);

    if (!bookExisted)
        ctx_.app.getOrderBookDB().addOrderBook(book);

    JLOG(j_.debug()) << "final result: success";

    return {tesSUCCESS, true};
}

TER
CreateOffer::doApply()
{
    // This is the ledger view that we work against. Transactions are applied
    // as we go on processing transactions.
    Sandbox sb(&ctx_.view());

    // This is a ledger with just the fees paid and any unfunded or expired
    // offers we encounter removed. It's used when handling Fill-or-Kill offers,
    // if the order isn't going to be placed, to avoid wasting the work we did.
    Sandbox sbCancel(&ctx_.view());

    auto const result = applyGuts(sb, sbCancel);
    if (result.second)
        sb.apply(ctx_.rawView());
    else
        sbCancel.apply(ctx_.rawView());
    return result.first;
}

}  // namespace ripple
