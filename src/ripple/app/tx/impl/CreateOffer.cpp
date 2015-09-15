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
#include <ripple/app/tx/impl/OfferStream.h>
#include <ripple/app/tx/impl/Taker.h>
#include <ripple/app/ledger/Ledger.h>
#include <ripple/protocol/Quality.h>
#include <ripple/basics/Log.h>
#include <ripple/json/to_string.h>
#include <ripple/ledger/Sandbox.h>
#include <beast/cxx14/memory.h>
#include <beast/utility/Journal.h>
#include <beast/utility/WrappedSink.h>
#include <stdexcept>

namespace ripple {

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
        JLOG(j.debug) <<
            "Malformed transaction: Invalid flags set.";
        return temINVALID_FLAG;
    }

    bool const bImmediateOrCancel (uTxFlags & tfImmediateOrCancel);
    bool const bFillOrKill (uTxFlags & tfFillOrKill);

    if (bImmediateOrCancel && bFillOrKill)
    {
        JLOG(j.debug) <<
            "Malformed transaction: both IoC and FoK set.";
        return temINVALID_FLAG;
    }

    bool const bHaveExpiration (tx.isFieldPresent (sfExpiration));

    if (bHaveExpiration && (tx.getFieldU32 (sfExpiration) == 0))
    {
        JLOG(j.debug) <<
            "Malformed offer: bad expiration";
        return temBAD_EXPIRATION;
    }

    bool const bHaveCancel (tx.isFieldPresent (sfOfferSequence));

    if (bHaveCancel && (tx.getFieldU32 (sfOfferSequence) == 0))
    {
        JLOG(j.debug) <<
            "Malformed offer: bad cancel sequence";
        return temBAD_SEQUENCE;
    }

    STAmount saTakerPays = tx.getFieldAmount (sfTakerPays);
    STAmount saTakerGets = tx.getFieldAmount (sfTakerGets);

    if (!isLegalNet (saTakerPays) || !isLegalNet (saTakerGets))
        return temBAD_AMOUNT;

    if (saTakerPays.native () && saTakerGets.native ())
    {
        JLOG(j.debug) <<
            "Malformed offer: XRP for XRP";
        return temBAD_OFFER;
    }
    if (saTakerPays <= zero || saTakerGets <= zero)
    {
        JLOG(j.debug) <<
            "Malformed offer: bad amount";
        return temBAD_OFFER;
    }

    auto const& uPaysIssuerID = saTakerPays.getIssuer ();
    auto const& uPaysCurrency = saTakerPays.getCurrency ();

    auto const& uGetsIssuerID = saTakerGets.getIssuer ();
    auto const& uGetsCurrency = saTakerGets.getCurrency ();

    if (uPaysCurrency == uGetsCurrency && uPaysIssuerID == uGetsIssuerID)
    {
        JLOG(j.debug) <<
            "Malformed offer: redundant offer";
        return temREDUNDANT;
    }
    // We don't allow a non-native currency to use the currency code XRP.
    if (badCurrency() == uPaysCurrency || badCurrency() == uGetsCurrency)
    {
        JLOG(j.debug) <<
            "Malformed offer: Bad currency.";
        return temBAD_CURRENCY;
    }

    if (saTakerPays.native () != !uPaysIssuerID ||
        saTakerGets.native () != !uGetsIssuerID)
    {
        JLOG(j.warning) <<
            "Malformed offer: bad issuer";
        return temBAD_ISSUER;
    }

    return preflight2(ctx);
}

TER
CreateOffer::checkAcceptAsset(IssueRef issue) const
{
    // Only valid for custom currencies
    assert (!isXRP (issue.currency));

    auto const issuerAccount = ctx_.view().read(
        keylet::account(issue.account));

    if (!issuerAccount)
    {
        if (j_.warning) j_.warning <<
            "delay: can't receive IOUs from non-existent issuer: " <<
            to_string (issue.account);

        return (view().flags() & tapRETRY)
            ? terNO_ACCOUNT
            : tecNO_ISSUER;
    }

    if (issuerAccount->getFieldU32 (sfFlags) & lsfRequireAuth)
    {
        auto const trustLine = ctx_.view().read(
            keylet::line(account_, issue.account, issue.currency));

        if (!trustLine)
        {
            return (view().flags() & tapRETRY)
                ? terNO_LINE
                : tecNO_LINE;
        }

        // Entries have a canonical representation, determined by a
        // lexicographical "greater than" comparison employing strict weak
        // ordering. Determine which entry we need to access.
        bool const canonical_gt (account_ > issue.account);

        bool const is_authorized (trustLine->getFieldU32 (sfFlags) &
            (canonical_gt ? lsfLowAuth : lsfHighAuth));

        if (!is_authorized)
        {
            if (j_.debug) j_.debug <<
                "delay: can't receive IOUs from issuer without auth.";

            return (view().flags() & tapRETRY)
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
        offer.amount().out, fhZERO_IF_FROZEN);
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
    Clock::time_point const when)
{
    auto const& taker_amount = taker.original_offer ();

    assert (!isXRP (taker_amount.in) && !isXRP (taker_amount.out));

    if (isXRP (taker_amount.in) || isXRP (taker_amount.out))
        throw std::logic_error ("Bridging with XRP and an endpoint.");

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
            if (j_.debug)
            {
                j_.debug << count << " Direct:";
                j_.debug << "  offer: " << offers_direct.tip ();
                j_.debug << "     in: " << offers_direct.tip ().amount().in;
                j_.debug << "    out: " << offers_direct.tip ().amount ().out;
                j_.debug << "  owner: " << offers_direct.tip ().owner ();
                j_.debug << "  funds: " << accountFunds(view,
                    offers_direct.tip ().owner (),
                    offers_direct.tip ().amount ().out,
                    fhIGNORE_FREEZE);
            }

            cross_result = taker.cross(offers_direct.tip ());

            j_.debug << "Direct Result: " << transToken (cross_result);

            if (dry_offer (view, offers_direct.tip ()))
            {
                direct_consumed = true;
                have_direct = step_account (offers_direct, taker);
            }
        }
        else
        {
            if (j_.debug)
            {
                auto const owner1_funds_before = accountFunds(view,
                    offers_leg1.tip ().owner (),
                    offers_leg1.tip ().amount ().out,
                    fhIGNORE_FREEZE);

                auto const owner2_funds_before = accountFunds(view,
                    offers_leg2.tip ().owner (),
                    offers_leg2.tip ().amount ().out,
                    fhIGNORE_FREEZE);

                j_.debug << count << " Bridge:";
                j_.debug << " offer1: " << offers_leg1.tip ();
                j_.debug << "     in: " << offers_leg1.tip ().amount().in;
                j_.debug << "    out: " << offers_leg1.tip ().amount ().out;
                j_.debug << "  owner: " << offers_leg1.tip ().owner ();
                j_.debug << "  funds: " << owner1_funds_before;
                j_.debug << " offer2: " << offers_leg2.tip ();
                j_.debug << "     in: " << offers_leg2.tip ().amount ().in;
                j_.debug << "    out: " << offers_leg2.tip ().amount ().out;
                j_.debug << "  owner: " << offers_leg2.tip ().owner ();
                j_.debug << "  funds: " << owner2_funds_before;
            }

            cross_result = taker.cross(offers_leg1.tip (), offers_leg2.tip ());

            j_.debug << "Bridge Result: " << transToken (cross_result);

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
            j_.debug << "The taker reports he's done during crossing!";
            break;
        }

        if (reachedOfferCrossingLimit (taker))
        {
            j_.debug << "The offer crossing limit has been exceeded!";
            break;
        }

        // Postcondition: If we aren't done, then we *must* have consumed at
        //                least one offer fully.
        assert (direct_consumed || leg1_consumed || leg2_consumed);

        if (!direct_consumed && !leg1_consumed && !leg2_consumed)
            throw std::logic_error ("bridged crossing: nothing was fully consumed.");
    }

    return std::make_pair(cross_result, taker.remaining_offer ());
}

std::pair<TER, Amounts>
CreateOffer::direct_cross (
    Taker& taker,
    ApplyView& view,
    ApplyView& view_cancel,
    Clock::time_point const when)
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
        auto const& offer (offers.tip());

        // We are done with crossing as soon as we cross the quality boundary
        if (taker.reject (offer.quality()))
            break;

        count++;

        if (j_.debug)
        {
            j_.debug << count << " Direct:";
            j_.debug << "  offer: " << offer;
            j_.debug << "     in: " << offer.amount ().in;
            j_.debug << "    out: " << offer.amount ().out;
            j_.debug << "  owner: " << offer.owner ();
            j_.debug << "  funds: " << accountFunds(view,
                offer.owner (), offer.amount ().out, fhIGNORE_FREEZE);
        }

        cross_result = taker.cross (offer);

        j_.debug << "Direct Result: " << transToken (cross_result);

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
            j_.debug << "The taker reports he's done during crossing!";
            break;
        }

        if (reachedOfferCrossingLimit (taker))
        {
            j_.debug << "The offer crossing limit has been exceeded!";
            break;
        }

        // Postcondition: If we aren't done, then we *must* have consumed the
        //                offer on the books fully!
        assert (direct_consumed);

        if (!direct_consumed)
            throw std::logic_error ("direct crossing: nothing was fully consumed.");
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

// Fill offer as much as possible by consuming offers already on the books,
// and adjusting account balances accordingly.
//
// Charges fees on top to taker.
std::pair<TER, Amounts>
CreateOffer::cross (
    ApplyView& view,
    ApplyView& cancel_view,
    Amounts const& taker_amount)
{
    Clock::time_point const when =
        ctx_.view().parentCloseTime();

    beast::WrappedSink takerSink (j_, "Taker ");

    Taker taker (cross_type_, view, account_, taker_amount,
        tx().getFlags(), beast::Journal (takerSink));

    try
    {
        if (cross_type_ == CrossType::IouToIou)
            return bridged_cross (taker, view, cancel_view, when);

        return direct_cross (taker, view, cancel_view, when);
    }
    catch (std::exception const& e)
    {
        j_.error << "Exception during offer crossing: " << e.what ();
        return std::make_pair (tecINTERNAL, taker.remaining_offer ());
    }
    catch (...)
    {
        j_.error << "Exception during offer crossing.";
        return std::make_pair (tecINTERNAL, taker.remaining_offer ());
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
        tx().getFieldAmount (sfTakerPays).native ();
    bool const gets_xrp =
        tx().getFieldAmount (sfTakerGets).native ();
    if (pays_xrp && !gets_xrp)
        cross_type_ = CrossType::IouToXrp;
    else if (gets_xrp && !pays_xrp)
        cross_type_ = CrossType::XrpToIou;

    return Transactor::preCompute();
}

std::pair<TER, bool>
CreateOffer::applyGuts (ApplyView& view, ApplyView& view_cancel)
{
    std::uint32_t const uTxFlags = tx().getFlags ();

    bool const bPassive (uTxFlags & tfPassive);
    bool const bImmediateOrCancel (uTxFlags & tfImmediateOrCancel);
    bool const bFillOrKill (uTxFlags & tfFillOrKill);
    bool const bSell (uTxFlags & tfSell);

    STAmount saTakerPays = tx().getFieldAmount (sfTakerPays);
    STAmount saTakerGets = tx().getFieldAmount (sfTakerGets);

    if (!isLegalNet (saTakerPays) || !isLegalNet (saTakerGets))
        return { temBAD_AMOUNT, true };

    auto const& uPaysIssuerID = saTakerPays.getIssuer ();
    auto const& uPaysCurrency = saTakerPays.getCurrency ();

    auto const& uGetsIssuerID = saTakerGets.getIssuer ();

    bool const bHaveExpiration (tx().isFieldPresent (sfExpiration));
    bool const bHaveCancel (tx().isFieldPresent (sfOfferSequence));

    std::uint32_t const uExpiration = tx().getFieldU32 (sfExpiration);
    std::uint32_t const uCancelSequence = tx().getFieldU32 (sfOfferSequence);

    // FIXME understand why we use SequenceNext instead of current transaction
    //       sequence to determine the transaction. Why is the offer sequence
    //       number insufficient?

    auto const sleCreator = view.peek (keylet::account(account_));

    deprecatedWrongOwnerCount_ = sleCreator->getFieldU32(sfOwnerCount);

    std::uint32_t const uAccountSequenceNext = sleCreator->getFieldU32 (sfSequence);
    std::uint32_t const uSequence = tx().getSequence ();

    // This is the original rate of the offer, and is the rate at which
    // it will be placed, even if crossing offers change the amounts that
    // end up on the books.
    std::uint64_t const uRate = getRate (saTakerGets, saTakerPays);

    TER result = tesSUCCESS;

    // This is the ledger view that we work against. Transactions are applied
    // as we go on processing transactions.

    if (isGlobalFrozen (view, uPaysIssuerID) || isGlobalFrozen (view, uGetsIssuerID))
    {
        if (j_.warning) j_.warning <<
            "Offer involves frozen asset";

        result = tecFROZEN;
    }
    else if (accountFunds(view, account_, saTakerGets,
        fhZERO_IF_FROZEN) <= zero)
    {
        if (j_.debug) j_.debug <<
            "delay: Offers must be at least partially funded.";

        result = tecUNFUNDED_OFFER;
    }
    // This can probably be simplified to make sure that you cancel sequences
    // before the transaction sequence number.
    else if (bHaveCancel && (uAccountSequenceNext - 1 <= uCancelSequence))
    {
        if (j_.debug) j_.debug <<
            "uAccountSequenceNext=" << uAccountSequenceNext <<
            " uOfferSequence=" << uCancelSequence;

        result = temBAD_SEQUENCE;
    }

    if (result != tesSUCCESS)
    {
        j_.debug << "final result: " << transToken (result);
        return { result, true };
    }

    // Process a cancellation request that's passed along with an offer.
    if (bHaveCancel)
    {
        auto const sleCancel = view.peek(
            keylet::offer(account_, uCancelSequence));

        // It's not an error to not find the offer to cancel: it might have
        // been consumed or removed. If it is found, however, it's an error
        // to fail to delete it.
        if (sleCancel)
        {
            j_.debug << "Create cancels order " << uCancelSequence;
            result = offerDelete (view, sleCancel);
        }
    }

    // Expiration is defined in terms of the close time of the parent ledger,
    // because we definitively know the time that it closed but we do not
    // know the closing time of the ledger that is under construction.
    if (bHaveExpiration &&
        (ctx_.view().parentCloseTime() >= uExpiration))
    {
        return { tesSUCCESS, true };
    }

    // Make sure that we are authorized to hold what the taker will pay us.
    if (result == tesSUCCESS && !saTakerPays.native ())
        result = checkAcceptAsset (Issue (uPaysCurrency, uPaysIssuerID));

    bool const bOpenLedger =
        ctx_.view().open();
    bool crossed = false;

    if (result == tesSUCCESS)
    {
        // We reverse pays and gets because during crossing we are taking.
        Amounts const taker_amount (saTakerGets, saTakerPays);

        // The amount of the offer that is unfilled after crossing has been
        // performed. It may be equal to the original amount (didn't cross),
        // empty (fully crossed), or something in-between.
        Amounts place_offer;

        j_.debug << "Attempting cross: " <<
            to_string (taker_amount.in.issue ()) << " -> " <<
            to_string (taker_amount.out.issue ());

        if (j_.trace)
        {
            j_.debug << "   mode: " <<
                (bPassive ? "passive " : "") <<
                (bSell ? "sell" : "buy");
            j_.trace <<"     in: " << format_amount (taker_amount.in);
            j_.trace << "    out: " << format_amount (taker_amount.out);
        }

        std::tie(result, place_offer) = cross (view, view_cancel, taker_amount);
        assert (result != tefINTERNAL);

        if (j_.trace)
        {
            j_.trace << "Cross result: " << transToken (result);
            j_.trace << "     in: " << format_amount (place_offer.in);
            j_.trace << "    out: " << format_amount (place_offer.out);
        }

        if (result == tecFAILED_PROCESSING && bOpenLedger)
            result = telFAILED_PROCESSING;

        if (result != tesSUCCESS)
        {
            j_.debug << "final result: " << transToken (result);
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
            j_.fatal << "Cross left offer negative!" <<
                "     in: " << format_amount (place_offer.in) <<
                "    out: " << format_amount (place_offer.out);
            return { tefINTERNAL, true };
        }

        if (place_offer.in == zero || place_offer.out == zero)
        {
            j_.debug << "Offer fully crossed!";
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
        j_.debug << "final result: " << transToken (result);
        return { result, true };
    }

    if (j_.trace)
    {
        j_.trace << "Place" << (crossed ? " remaining " : " ") << "offer:";
        j_.trace << "    Pays: " << saTakerPays.getFullText ();
        j_.trace << "    Gets: " << saTakerGets.getFullText ();
    }

    // For 'fill or kill' offers, failure to fully cross means that the
    // entire operation should be aborted, with only fees paid.
    if (bFillOrKill)
    {
        j_.trace << "Fill or Kill: offer killed";
        return { tesSUCCESS, false };
    }

    // For 'immediate or cancel' offers, the amount remaining doesn't get
    // placed - it gets cancelled and the operation succeeds.
    if (bImmediateOrCancel)
    {
        j_.trace << "Immediate or cancel: offer cancelled";
        return { tesSUCCESS, true };
    }

    {
        // Mon Aug 17 11:00:00am PDT
        static NetClock::time_point const switchoverTime (
            std::chrono::seconds (493149600));

        XRPAmount reserve;

        if (ctx_.view().info().parentCloseTime <=
                switchoverTime.time_since_epoch().count())
            reserve = ctx_.view().fees().accountReserve(
                deprecatedWrongOwnerCount_+1);
        else
            reserve = ctx_.view().fees().accountReserve(
                sleCreator->getFieldU32 (sfOwnerCount) + 1);

        if (mPriorBalance < reserve)
        {
            // If we are here, the signing account had an insufficient reserve
            // *prior* to our processing. If something actually crossed, then
            // we allow this; otherwise, we just claim a fee.
            if (!crossed)
                result = tecINSUF_RESERVE_OFFER;

            if (result != tesSUCCESS)
                j_.debug << "final result: " << transToken (result);

            return { result, true };
        }
    }

    // We need to place the remainder of the offer into its order book.
    auto const offer_index = getOfferIndex (account_, uSequence);

    std::uint64_t uOwnerNode;
    std::uint64_t uBookNode;
    uint256 uDirectory;

    // Add offer to owner's directory.
    result = dirAdd(view, uOwnerNode,
        getOwnerDirIndex (account_), offer_index,
        std::bind (
            &ownerDirDescriber, std::placeholders::_1,
            std::placeholders::_2, account_));

    if (result == tesSUCCESS)
    {
        // Update owner count.
        adjustOwnerCount(view, sleCreator, 1);

        if (j_.trace) j_.trace <<
            "adding to book: " << to_string (saTakerPays.issue ()) <<
            " : " << to_string (saTakerGets.issue ());

        uint256 const book_base (getBookBase (
            { saTakerPays.issue (), saTakerGets.issue () }));

        // We use the original rate to place the offer.
        uDirectory = getQualityIndex (book_base, uRate);

        // Add offer to order book.
        result = dirAdd (view, uBookNode, uDirectory, offer_index,
            std::bind (
                &qualityDirDescriber, std::placeholders::_1,
                std::placeholders::_2, saTakerPays.getCurrency (),
                uPaysIssuerID, saTakerGets.getCurrency (),
                uGetsIssuerID, uRate, std::ref(ctx_.app)));
    }

    if (result == tesSUCCESS)
    {
        auto sleOffer = std::make_shared<SLE>(ltOFFER, offer_index);
        sleOffer->setAccountID (sfAccount, account_);
        sleOffer->setFieldU32 (sfSequence, uSequence);
        sleOffer->setFieldH256 (sfBookDirectory, uDirectory);
        sleOffer->setFieldAmount (sfTakerPays, saTakerPays);
        sleOffer->setFieldAmount (sfTakerGets, saTakerGets);
        sleOffer->setFieldU64 (sfOwnerNode, uOwnerNode);
        sleOffer->setFieldU64 (sfBookNode, uBookNode);
        if (uExpiration)
            sleOffer->setFieldU32 (sfExpiration, uExpiration);
        if (bPassive)
            sleOffer->setFlag (lsfPassive);
        if (bSell)
            sleOffer->setFlag (lsfSell);
        view.insert(sleOffer);
    }

    if (result != tesSUCCESS)
        j_.debug << "final result: " << transToken (result);

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
