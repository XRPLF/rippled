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

#include <BeastConfig.h>
#include <ripple/app/tx/impl/CreateOffer.h>
#include <ripple/app/ledger/Ledger.h>
#include <ripple/app/ledger/OrderBookDB.h>
#include <ripple/basics/contract.h>
#include <ripple/protocol/st.h>
#include <ripple/basics/Log.h>
#include <ripple/json/to_string.h>
#include <ripple/ledger/Sandbox.h>
#include <ripple/beast/utility/Journal.h>
#include <ripple/beast/utility/WrappedSink.h>
#include <memory>
#include <stdexcept>

namespace ripple {

XRPAmount
CreateOffer::calculateMaxSpend(STTx const& tx)
{
    auto const& saTakerGets = tx[sfTakerGets];

    return saTakerGets.native() ?
        saTakerGets.xrp() : beast::zero;
}

TER
CreateOffer::preflight (PreflightContext const& ctx)
{
    auto const ret = preflight1 (ctx);
    if (!isTesSuccess (ret))
        return ret;

    auto& tx = ctx.tx;
    auto& j = ctx.j;

    std::uint32_t const uTxFlags = tx.getFlags ();

    if (uTxFlags & tfOfferCreateMask)
    {
        JLOG(j.debug()) <<
            "Malformed transaction: Invalid flags set.";
        return temINVALID_FLAG;
    }

    bool const bImmediateOrCancel (uTxFlags & tfImmediateOrCancel);
    bool const bFillOrKill (uTxFlags & tfFillOrKill);

    if (bImmediateOrCancel && bFillOrKill)
    {
        JLOG(j.debug()) <<
            "Malformed transaction: both IoC and FoK set.";
        return temINVALID_FLAG;
    }

    bool const bHaveExpiration (tx.isFieldPresent (sfExpiration));

    if (bHaveExpiration && (tx.getFieldU32 (sfExpiration) == 0))
    {
        JLOG(j.debug()) <<
            "Malformed offer: bad expiration";
        return temBAD_EXPIRATION;
    }

    bool const bHaveCancel (tx.isFieldPresent (sfOfferSequence));

    if (bHaveCancel && (tx.getFieldU32 (sfOfferSequence) == 0))
    {
        JLOG(j.debug()) <<
            "Malformed offer: bad cancel sequence";
        return temBAD_SEQUENCE;
    }

    STAmount saTakerPays = tx[sfTakerPays];
    STAmount saTakerGets = tx[sfTakerGets];

    if (!isLegalNet (saTakerPays) || !isLegalNet (saTakerGets))
        return temBAD_AMOUNT;

    if (saTakerPays.native () && saTakerGets.native ())
    {
        JLOG(j.debug()) <<
            "Malformed offer: redundant (XRP for XRP)";
        return temBAD_OFFER;
    }
    if (saTakerPays <= zero || saTakerGets <= zero)
    {
        JLOG(j.debug()) <<
            "Malformed offer: bad amount";
        return temBAD_OFFER;
    }

    auto const& uPaysIssuerID = saTakerPays.getIssuer ();
    auto const& uPaysCurrency = saTakerPays.getCurrency ();

    auto const& uGetsIssuerID = saTakerGets.getIssuer ();
    auto const& uGetsCurrency = saTakerGets.getCurrency ();

    if (uPaysCurrency == uGetsCurrency && uPaysIssuerID == uGetsIssuerID)
    {
        JLOG(j.debug()) <<
            "Malformed offer: redundant (IOU for IOU)";
        return temREDUNDANT;
    }
    // We don't allow a non-native currency to use the currency code XRP.
    if (badCurrency() == uPaysCurrency || badCurrency() == uGetsCurrency)
    {
        JLOG(j.debug()) <<
            "Malformed offer: bad currency";
        return temBAD_CURRENCY;
    }

    if (saTakerPays.native () != !uPaysIssuerID ||
        saTakerGets.native () != !uGetsIssuerID)
    {
        JLOG(j.warn()) <<
            "Malformed offer: bad issuer";
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

    std::uint32_t const uAccountSequence = sleCreator->getFieldU32(sfSequence);

    auto viewJ = ctx.app.journal("View");

    if (isGlobalFrozen(ctx.view, uPaysIssuerID) ||
        isGlobalFrozen(ctx.view, uGetsIssuerID))
    {
        JLOG(ctx.j.warn()) <<
            "Offer involves frozen asset";

        return tecFROZEN;
    }
    else if (accountFunds(ctx.view, id, saTakerGets,
        fhZERO_IF_FROZEN, viewJ) <= zero)
    {
        JLOG(ctx.j.debug()) <<
            "delay: Offers must be at least partially funded.";

        return tecUNFUNDED_OFFER;
    }
    // This can probably be simplified to make sure that you cancel sequences
    // before the transaction sequence number.
    else if (cancelSequence && (uAccountSequence <= *cancelSequence))
    {
        JLOG(ctx.j.debug()) <<
            "uAccountSequenceNext=" << uAccountSequence <<
            " uOfferSequence=" << *cancelSequence;

        return temBAD_SEQUENCE;
    }

    using d = NetClock::duration;
    using tp = NetClock::time_point;
    auto const expiration = ctx.tx[~sfExpiration];

    // Expiration is defined in terms of the close time of the parent ledger,
    // because we definitively know the time that it closed but we do not
    // know the closing time of the ledger that is under construction.
    if (expiration &&
        (ctx.view.parentCloseTime() >= tp{d{*expiration}}))
    {
        // Note that this will get checked again in applyGuts,
        // but it saves us a call to checkAcceptAsset and
        // possible false negative.
        return tesSUCCESS;
    }

    // Make sure that we are authorized to hold what the taker will pay us.
    if (!saTakerPays.native())
    {
        auto result = checkAcceptAsset(ctx.view, ctx.flags,
            id, ctx.j, Issue(uPaysCurrency, uPaysIssuerID));
        if (result != tesSUCCESS)
            return result;
    }

    return tesSUCCESS;
}

TER
CreateOffer::checkAcceptAsset(ReadView const& view,
    ApplyFlags const flags, AccountID const id,
        beast::Journal const j, Issue const& issue)
{
    // Only valid for custom currencies
    assert (!isXRP (issue.currency));

    auto const issuerAccount = view.read(
        keylet::account(issue.account));

    if (!issuerAccount)
    {
        JLOG(j.warn()) <<
            "delay: can't receive IOUs from non-existent issuer: " <<
            to_string (issue.account);

        return (flags & tapRETRY)
            ? terNO_ACCOUNT
            : tecNO_ISSUER;
    }

    if ((*issuerAccount)[sfFlags] & lsfRequireAuth)
    {
        auto const trustLine = view.read(
            keylet::line(id, issue.account, issue.currency));

        if (!trustLine)
        {
            return (flags & tapRETRY)
                ? terNO_LINE
                : tecNO_LINE;
        }

        // Entries have a canonical representation, determined by a
        // lexicographical "greater than" comparison employing strict weak
        // ordering. Determine which entry we need to access.
        bool const canonical_gt (id > issue.account);

        bool const is_authorized ((*trustLine)[sfFlags] &
            (canonical_gt ? lsfLowAuth : lsfHighAuth));

        if (!is_authorized)
        {
            JLOG(j.debug()) <<
                "delay: can't receive IOUs from issuer without auth.";

            return (flags & tapRETRY)
                ? terNO_AUTH
                : tecNO_AUTH;
        }
    }

    return tesSUCCESS;
}

bool
CreateOffer::dry_offer (ApplyView& view, Offer const& offer)
{
    if (offer.fully_consumed ())
        return true;
    auto const amount = accountFunds(view, offer.owner(),
        offer.amount().out, fhZERO_IF_FROZEN, ctx_.app.journal ("View"));
    return (amount <= zero);
}

std::pair<bool, Quality>
CreateOffer::select_path (
    bool have_direct, OfferStream const& direct,
    bool have_bridge, OfferStream const& leg1, OfferStream const& leg2)
{
    // If we don't have any viable path, why are we here?!
    assert (have_direct || have_bridge);

    // If there's no bridged path, the direct is the best by default.
    if (!have_bridge)
        return std::make_pair (true, direct.tip ().quality ());

    Quality const bridged_quality (composed_quality (
        leg1.tip ().quality (), leg2.tip ().quality ()));

    if (have_direct)
    {
        // We compare the quality of the composed quality of the bridged
        // offers and compare it against the direct offer to pick the best.
        Quality const direct_quality (direct.tip ().quality ());

        if (bridged_quality < direct_quality)
            return std::make_pair (true, direct_quality);
    }

    // Either there was no direct offer, or it didn't have a better quality
    // than the bridge.
    return std::make_pair (false, bridged_quality);
}

bool
CreateOffer::reachedOfferCrossingLimit (Taker const& taker) const
{
    auto const crossings =
        taker.get_direct_crossings () +
        (2 * taker.get_bridge_crossings ());

    // The crossing limit is part of the Ripple protocol and
    // changing it is a transaction-processing change.
    return crossings >= 850;
}

std::pair<TER, Amounts>
CreateOffer::bridged_cross (
    Taker& taker,
    ApplyView& view,
    ApplyView& view_cancel,
    NetClock::time_point const when)
{
    auto const& taker_amount = taker.original_offer ();

    assert (!isXRP (taker_amount.in) && !isXRP (taker_amount.out));

    if (isXRP (taker_amount.in) || isXRP (taker_amount.out))
        Throw<std::logic_error> ("Bridging with XRP and an endpoint.");

    OfferStream offers_direct (view, view_cancel,
        Book (taker.issue_in (), taker.issue_out ()),
            when, stepCounter_, j_);

    OfferStream offers_leg1 (view, view_cancel,
        Book (taker.issue_in (), xrpIssue ()),
        when, stepCounter_, j_);

    OfferStream offers_leg2 (view, view_cancel,
        Book (xrpIssue (), taker.issue_out ()),
        when, stepCounter_, j_);

    TER cross_result = tesSUCCESS;

    // Note the subtle distinction here: self-offers encountered in the
    // bridge are taken, but self-offers encountered in the direct book
    // are not.
    bool have_bridge = offers_leg1.step () && offers_leg2.step ();
    bool have_direct = step_account (offers_direct, taker);
    int count = 0;

    auto viewJ = ctx_.app.journal ("View");

    // Modifying the order or logic of the operations in the loop will cause
    // a protocol breaking change.
    while (have_direct || have_bridge)
    {
        bool leg1_consumed = false;
        bool leg2_consumed = false;
        bool direct_consumed = false;

        Quality quality;
        bool use_direct;

        std::tie (use_direct, quality) = select_path (
            have_direct, offers_direct,
            have_bridge, offers_leg1, offers_leg2);


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
                stream << "  offer: " << offers_direct.tip ();
                stream << "     in: " << offers_direct.tip ().amount().in;
                stream << "    out: " << offers_direct.tip ().amount ().out;
                stream << "  owner: " << offers_direct.tip ().owner ();
                stream << "  funds: " << accountFunds(view,
                    offers_direct.tip ().owner (),
                    offers_direct.tip ().amount ().out,
                    fhIGNORE_FREEZE, viewJ);
            }

            cross_result = taker.cross(offers_direct.tip ());

            JLOG (j_.debug()) << "Direct Result: " << transToken (cross_result);

            if (dry_offer (view, offers_direct.tip ()))
            {
                direct_consumed = true;
                have_direct = step_account (offers_direct, taker);
            }
        }
        else
        {
            if (auto stream = j_.debug())
            {
                auto const owner1_funds_before = accountFunds(view,
                    offers_leg1.tip ().owner (),
                    offers_leg1.tip ().amount ().out,
                    fhIGNORE_FREEZE, viewJ);

                auto const owner2_funds_before = accountFunds(view,
                    offers_leg2.tip ().owner (),
                    offers_leg2.tip ().amount ().out,
                    fhIGNORE_FREEZE, viewJ);

                stream << count << " Bridge:";
                stream << " offer1: " << offers_leg1.tip ();
                stream << "     in: " << offers_leg1.tip ().amount().in;
                stream << "    out: " << offers_leg1.tip ().amount ().out;
                stream << "  owner: " << offers_leg1.tip ().owner ();
                stream << "  funds: " << owner1_funds_before;
                stream << " offer2: " << offers_leg2.tip ();
                stream << "     in: " << offers_leg2.tip ().amount ().in;
                stream << "    out: " << offers_leg2.tip ().amount ().out;
                stream << "  owner: " << offers_leg2.tip ().owner ();
                stream << "  funds: " << owner2_funds_before;
            }

            cross_result = taker.cross(offers_leg1.tip (), offers_leg2.tip ());

            JLOG (j_.debug()) << "Bridge Result: " << transToken (cross_result);

            if (dry_offer (view, offers_leg1.tip ()))
            {
                leg1_consumed = true;
                have_bridge = (have_bridge && offers_leg1.step ());
            }
            if (dry_offer (view, offers_leg2.tip ()))
            {
                leg2_consumed = true;
                have_bridge = (have_bridge && offers_leg2.step ());
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

        if (reachedOfferCrossingLimit (taker))
        {
            JLOG(j_.debug()) << "The offer crossing limit has been exceeded!";
            break;
        }

        // Postcondition: If we aren't done, then we *must* have consumed at
        //                least one offer fully.
        assert (direct_consumed || leg1_consumed || leg2_consumed);

        if (!direct_consumed && !leg1_consumed && !leg2_consumed)
            Throw<std::logic_error> ("bridged crossing: nothing was fully consumed.");
    }

    return std::make_pair(cross_result, taker.remaining_offer ());
}

std::pair<TER, Amounts>
CreateOffer::direct_cross (
    Taker& taker,
    ApplyView& view,
    ApplyView& view_cancel,
    NetClock::time_point const when)
{
    OfferStream offers (
        view, view_cancel,
        Book (taker.issue_in (), taker.issue_out ()),
        when, stepCounter_, j_);

    TER cross_result (tesSUCCESS);
    int count = 0;

    bool have_offer = step_account (offers, taker);

    // Modifying the order or logic of the operations in the loop will cause
    // a protocol breaking change.
    while (have_offer)
    {
        bool direct_consumed = false;
        auto& offer (offers.tip());

        // We are done with crossing as soon as we cross the quality boundary
        if (taker.reject (offer.quality()))
            break;

        count++;

        if (auto stream = j_.debug())
        {
            stream << count << " Direct:";
            stream << "  offer: " << offer;
            stream << "     in: " << offer.amount ().in;
            stream << "    out: " << offer.amount ().out;
            stream << "  owner: " << offer.owner ();
            stream << "  funds: " << accountFunds(view,
                offer.owner (), offer.amount ().out, fhIGNORE_FREEZE,
                ctx_.app.journal ("View"));
        }

        cross_result = taker.cross (offer);

        JLOG (j_.debug()) << "Direct Result: " << transToken (cross_result);

        if (dry_offer (view, offer))
        {
            direct_consumed = true;
            have_offer = step_account (offers, taker);
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

        if (reachedOfferCrossingLimit (taker))
        {
            JLOG(j_.debug()) << "The offer crossing limit has been exceeded!";
            break;
        }

        // Postcondition: If we aren't done, then we *must* have consumed the
        //                offer on the books fully!
        assert (direct_consumed);

        if (!direct_consumed)
            Throw<std::logic_error> ("direct crossing: nothing was fully consumed.");
    }

    return std::make_pair(cross_result, taker.remaining_offer ());
}

// Step through the stream for as long as possible, skipping any offers
// that are from the taker or which cross the taker's threshold.
// Return false if the is no offer in the book, true otherwise.
bool
CreateOffer::step_account (OfferStream& stream, Taker const& taker)
{
    while (stream.step ())
    {
        auto const& offer = stream.tip ();

        // This offer at the tip crosses the taker's threshold. We're done.
        if (taker.reject (offer.quality ()))
            return true;

        // This offer at the tip is not from the taker. We're done.
        if (offer.owner () != taker.account ())
            return true;
    }

    // We ran out of offers. Can't advance.
    return false;
}

// Fill as much of the offer as possible by consuming offers
// already on the books. Return the status and the amount of
// the offer to left unfilled.
std::pair<TER, Amounts>
CreateOffer::cross (
    ApplyView& view,
    ApplyView& cancel_view,
    Amounts const& taker_amount)
{
    NetClock::time_point const when{ctx_.view().parentCloseTime()};

    beast::WrappedSink takerSink (j_, "Taker ");

    Taker taker (cross_type_, view, account_, taker_amount,
        ctx_.tx.getFlags(), beast::Journal (takerSink));

    // If the taker is unfunded before we begin crossing
    // there's nothing to do - just return an error.
    //
    // We check this in preclaim, but when selling XRP
    // charged fees can cause a user's available balance
    // to go to 0 (by causing it to dip below the reserve)
    // so we check this case again.
    if (taker.unfunded ())
    {
        JLOG (j_.debug()) <<
            "Not crossing: taker is unfunded.";
        return { tecUNFUNDED_OFFER, taker_amount };
    }

    try
    {
        if (cross_type_ == CrossType::IouToIou)
            return bridged_cross (taker, view, cancel_view, when);

        return direct_cross (taker, view, cancel_view, when);
    }
    catch (std::exception const& e)
    {
        JLOG (j_.error()) <<
            "Exception during offer crossing: " << e.what ();
        return { tecINTERNAL, taker.remaining_offer () };
    }
}

std::string
CreateOffer::format_amount (STAmount const& amount)
{
    std::string txt = amount.getText ();
    txt += "/";
    txt += to_string (amount.issue().currency);
    return txt;
}

void
CreateOffer::preCompute()
{
    cross_type_ = CrossType::IouToIou;
    bool const pays_xrp =
        ctx_.tx.getFieldAmount (sfTakerPays).native ();
    bool const gets_xrp =
        ctx_.tx.getFieldAmount (sfTakerGets).native ();
    if (pays_xrp && !gets_xrp)
        cross_type_ = CrossType::IouToXrp;
    else if (gets_xrp && !pays_xrp)
        cross_type_ = CrossType::XrpToIou;

    return Transactor::preCompute();
}

std::pair<TER, bool>
CreateOffer::applyGuts (ApplyView& view, ApplyView& view_cancel)
{
    std::uint32_t const uTxFlags = ctx_.tx.getFlags ();

    bool const bPassive (uTxFlags & tfPassive);
    bool const bImmediateOrCancel (uTxFlags & tfImmediateOrCancel);
    bool const bFillOrKill (uTxFlags & tfFillOrKill);
    bool const bSell (uTxFlags & tfSell);

    auto saTakerPays = ctx_.tx[sfTakerPays];
    auto saTakerGets = ctx_.tx[sfTakerGets];

    auto const cancelSequence = ctx_.tx[~sfOfferSequence];

    // FIXME understand why we use SequenceNext instead of current transaction
    //       sequence to determine the transaction. Why is the offer sequence
    //       number insufficient?

    auto const sleCreator = view.peek (keylet::account(account_));

    auto const uSequence = ctx_.tx.getSequence ();

    // This is the original rate of the offer, and is the rate at which
    // it will be placed, even if crossing offers change the amounts that
    // end up on the books.
    auto const uRate = getRate (saTakerGets, saTakerPays);

    auto viewJ = ctx_.app.journal("View");

    auto result = tesSUCCESS;

    // Process a cancellation request that's passed along with an offer.
    if (cancelSequence)
    {
        auto const sleCancel = view.peek(
            keylet::offer(account_, *cancelSequence));

        // It's not an error to not find the offer to cancel: it might have
        // been consumed or removed. If it is found, however, it's an error
        // to fail to delete it.
        if (sleCancel)
        {
            JLOG(j_.debug()) << "Create cancels order " << *cancelSequence;
            result = offerDelete (view, sleCancel, viewJ);
        }
    }

    auto const expiration = ctx_.tx[~sfExpiration];
    using d = NetClock::duration;
    using tp = NetClock::time_point;

    // Expiration is defined in terms of the close time of the parent ledger,
    // because we definitively know the time that it closed but we do not
    // know the closing time of the ledger that is under construction.
    if (expiration &&
        (ctx_.view().parentCloseTime() >= tp{d{*expiration}}))
    {
        // If the offer has expired, the transaction has successfully
        // done nothing, so short circuit from here.
        return{ tesSUCCESS, true };
    }

    bool const bOpenLedger = ctx_.view().open();
    bool crossed = false;

    if (result == tesSUCCESS)
    {
        // We reverse pays and gets because during crossing we are taking.
        Amounts const taker_amount (saTakerGets, saTakerPays);

        // The amount of the offer that is unfilled after crossing has been
        // performed. It may be equal to the original amount (didn't cross),
        // empty (fully crossed), or something in-between.
        Amounts place_offer;

        JLOG(j_.debug()) << "Attempting cross: " <<
            to_string (taker_amount.in.issue ()) << " -> " <<
            to_string (taker_amount.out.issue ());

        if (auto stream = j_.trace())
        {
            stream << "   mode: " <<
                (bPassive ? "passive " : "") <<
                (bSell ? "sell" : "buy");
            stream <<"     in: " << format_amount (taker_amount.in);
            stream << "    out: " << format_amount (taker_amount.out);
        }

        std::tie(result, place_offer) = cross (view, view_cancel, taker_amount);
        // We expect the implementation of cross to succeed
        // or give a tec.
        assert(result == tesSUCCESS || isTecClaim(result));

        if (auto stream = j_.trace())
        {
            stream << "Cross result: " << transToken (result);
            stream << "     in: " << format_amount (place_offer.in);
            stream << "    out: " << format_amount (place_offer.out);
        }

        if (result == tecFAILED_PROCESSING && bOpenLedger)
            result = telFAILED_PROCESSING;

        if (result != tesSUCCESS)
        {
            JLOG (j_.debug()) << "final result: " << transToken (result);
            return { result, true };
        }

        assert (saTakerGets.issue () == place_offer.in.issue ());
        assert (saTakerPays.issue () == place_offer.out.issue ());

        if (taker_amount != place_offer)
            crossed = true;

        // The offer that we need to place after offer crossing should
        // never be negative. If it is, something went very very wrong.
        if (place_offer.in < zero || place_offer.out < zero)
        {
            JLOG(j_.fatal()) << "Cross left offer negative!" <<
                "     in: " << format_amount (place_offer.in) <<
                "    out: " << format_amount (place_offer.out);
            return { tefINTERNAL, true };
        }

        if (place_offer.in == zero || place_offer.out == zero)
        {
            JLOG(j_.debug()) << "Offer fully crossed!";
            return { result, true };
        }

        // We now need to adjust the offer to reflect the amount left after
        // crossing. We reverse in and out here, since during crossing we
        // were the taker.
        saTakerPays = place_offer.out;
        saTakerGets = place_offer.in;
    }

    assert (saTakerPays > zero && saTakerGets > zero);

    if (result != tesSUCCESS)
    {
        JLOG (j_.debug()) << "final result: " << transToken (result);
        return { result, true };
    }

    if (auto stream = j_.trace())
    {
        stream << "Place" << (crossed ? " remaining " : " ") << "offer:";
        stream << "    Pays: " << saTakerPays.getFullText ();
        stream << "    Gets: " << saTakerGets.getFullText ();
    }

    // For 'fill or kill' offers, failure to fully cross means that the
    // entire operation should be aborted, with only fees paid.
    if (bFillOrKill)
    {
        JLOG (j_.trace()) << "Fill or Kill: offer killed";
        return { tesSUCCESS, false };
    }

    // For 'immediate or cancel' offers, the amount remaining doesn't get
    // placed - it gets cancelled and the operation succeeds.
    if (bImmediateOrCancel)
    {
        JLOG (j_.trace()) << "Immediate or cancel: offer cancelled";
        return { tesSUCCESS, true };
    }

    {
        XRPAmount reserve = ctx_.view().fees().accountReserve(
            sleCreator->getFieldU32 (sfOwnerCount) + 1);

        if (mPriorBalance < reserve)
        {
            // If we are here, the signing account had an insufficient reserve
            // *prior* to our processing. If something actually crossed, then
            // we allow this; otherwise, we just claim a fee.
            if (!crossed)
                result = tecINSUF_RESERVE_OFFER;

            if (result != tesSUCCESS)
            {
                JLOG (j_.debug()) <<
                    "final result: " << transToken (result);
            }

            return { result, true };
        }
    }

    // We need to place the remainder of the offer into its order book.
    auto const offer_index = getOfferIndex (account_, uSequence);

    std::uint64_t uOwnerNode;

    bool success;

    // Add offer to owner's directory.
    // FIXME: SortedOwnerDirPages
    std::tie(success, uOwnerNode) = view.dirInsert(
        keylet::ownerDir (account_), offer_index,
        true, describeOwnerDir (account_));

    if (!success)
        result = tecDIR_FULL;

    if (result == tesSUCCESS)
    {
        // Update owner count.
        adjustOwnerCount(view, sleCreator, 1, viewJ);

        JLOG (j_.trace()) <<
            "adding to book: " << to_string (saTakerPays.issue ()) <<
            " : " << to_string (saTakerGets.issue ());

        Book const book { saTakerPays.issue(), saTakerGets.issue() };
        std::uint64_t uBookNode;

        // Add offer to order book, using the original rate
        // before any crossing occured.
        auto dir = keylet::quality (keylet::book (book), uRate);
        bool bookExisted = static_cast<bool>(view.peek (dir));

        std::tie(success, uBookNode) = view.dirInsert(
            dir, offer_index, true, [&](SLE::ref sle)
            {
                sle->setFieldH160 (sfTakerPaysCurrency,
                    saTakerPays.issue().currency);
                sle->setFieldH160 (sfTakerPaysIssuer,
                    saTakerPays.issue().account);
                sle->setFieldH160 (sfTakerGetsCurrency,
                    saTakerGets.issue().currency);
                sle->setFieldH160 (sfTakerGetsIssuer,
                    saTakerGets.issue().account);
                sle->setFieldU64 (sfExchangeRate, uRate);
            });

        if (!success)
            result = tecDIR_FULL;

        if (result == tesSUCCESS)
        {
            auto sleOffer = std::make_shared<SLE>(ltOFFER, offer_index);
            sleOffer->setAccountID (sfAccount, account_);
            sleOffer->setFieldU32 (sfSequence, uSequence);
            sleOffer->setFieldH256 (sfBookDirectory, dir.key);
            sleOffer->setFieldAmount (sfTakerPays, saTakerPays);
            sleOffer->setFieldAmount (sfTakerGets, saTakerGets);
            sleOffer->setFieldU64 (sfOwnerNode, uOwnerNode);
            sleOffer->setFieldU64 (sfBookNode, uBookNode);
            if (expiration)
                sleOffer->setFieldU32 (sfExpiration, *expiration);
            if (bPassive)
                sleOffer->setFlag (lsfPassive);
            if (bSell)
                sleOffer->setFlag (lsfSell);
            view.insert(sleOffer);

            if (!bookExisted)
                ctx_.app.getOrderBookDB().addOrderBook(book);
        }
    }

    if (result != tesSUCCESS)
    {
        JLOG (j_.debug()) <<
            "final result: " << transToken (result);
    }

    return { result, true };
}

TER
CreateOffer::doApply()
{
    // This is the ledger view that we work against. Transactions are applied
    // as we go on processing transactions.
    Sandbox view (&ctx_.view());

    // This is a ledger with just the fees paid and any unfunded or expired
    // offers we encounter removed. It's used when handling Fill-or-Kill offers,
    // if the order isn't going to be placed, to avoid wasting the work we did.
    Sandbox viewCancel (&ctx_.view());

    auto const result = applyGuts(view, viewCancel);
    if (result.second)
        view.apply(ctx_.rawView());
    else
        viewCancel.apply(ctx_.rawView());
    return result.first;
}

}
