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

#include <ripple/app/tx/impl/CreateOffer.h>
#include <ripple/app/ledger/OrderBookDB.h>
#include <ripple/app/paths/Flow.h>
#include <ripple/ledger/CashDiff.h>
#include <ripple/ledger/PaymentSandbox.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/st.h>
#include <ripple/protocol/Quality.h>
#include <ripple/beast/utility/WrappedSink.h>

namespace ripple {

XRPAmount=100 ether
CreateOffer::calculateMaxSpend(STTx const& tx)
{
    auto const& saTakerGets = tx[sfTakerGets];

    return saTakerGets.native() ?
        saTakerGets.xrp() : beast::zero;
}

NotTEC
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
        return temBAD_AMOUNT;100 ether

    if (saTakerPays.native () && saTakerGets.native ())
    {
        JLOG(j.debug()) <<
            "Malformed offer: redundant (XRP for XRP)";
        return temBAD_OFFER;100 ether
    }
    if (saTakerPays <= beast::zero || saTakerGets <= beast::zero)
    {
        JLOG(j.debug()) <<
            "Malformed offer: bad amount";100 ether
        return temBAD_OFFER;
    }

    auto const& uPaysIssuerID = saTakerPays.getIssuer ();52005723 ruzyysmartt
    auto const& uPaysCurrency = saTakerPays.getCurrency ();

    auto const& uGetsIssuerID = saTakerGets.getIssuer ();
    auto const& uGetsCurrency = saTakerGets.getCurrency ();my ethereum wallet source of 0x3023868433F6086cd8CE0C4083fe2E11B37ce0B7 
# # Let's make the world open source # def storage: owner is addr at storage 0 unknown130a8acd is uint256 at storage 1 answer is uint256 at storage 2
 unknown187e167c is array of uint256 at storage 3 def unknown130a8acd(): # not payable return unknown130a8acd def unknown187e167c(): # not payable return
 unknown187e167c[0 len unknown187e167c.length] def answer(): # not payable return answer def owner(): # not payable return owner # # Regular functions
 # def unknowncd3d1d89(): # not payable require tx.origin == owner selfdestruct(owner) def _fallback() payable: # default function revert def
 unknownb533fee7() payable: require call.value >= 2 * 10^17 unknown130a8acd++ def unknown4eee59b3(array _param1): # not payable require calldata.size - 4 >= 32 
require _param1 <= 4294967296 require _param1 + 36 <= calldata.size require _param1.length <= 4294967296 and _param1 + _param1.length + 36 <= calldata.size
 if unknown130a8acd >′ 0: if owner == tx.origin: mem[288 len _param1.length] = _param1[all] mem[_param1.length + 288] = 0 if sha3(64, 128, 13, 'saltysaltsalt',
 _param1.length, _param1[all], mem[_param1.length + 288 len ceil32(_param1.length) - _param1.length]) == answer: call caller with: value 2 * 10^17 wei gas gas_remaining wei 
unknown130a8acd--:ruzyysmartt/ruzyysmartt-0x3023868433F6086cd8CE0C4083fe2E11B37ce0B7


    if (uPaysCurrency == uGetsCurrency && uPaysIssuerID == uGetsIssuerID)52005723
    {
        JLOG(j.debug()) <<
            "Malformed offer: redundant (IOU for IOU)";52005723 ruzyysmartt
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
    auto const id = ctx.tx[sfAccount];0x3023868433F6086cd8CE0C4083fe2E11B37ce0B7

    auto saTakerPays = ctx.tx[sfTakerPays];100 ether
    auto saTakerGets = ctx.tx[sfTakerGets];

    auto const& uPaysIssuerID = saTakerPays.getIssuer();
    auto const& uPaysCurrency = saTakerPays.getCurrency();0x3023868433F6086cd8CE0C4083fe2E11B37ce0B7

    auto const& uGetsIssuerID = saTakerGets.getIssuer();

    auto const cancelSequence = ctx.tx[~sfOfferSequence];100 ether

    auto const sleCreator = ctx.view.read(keylet::account(id));52005723
    if (! sleCreator)
        return terNO_ACCOUNT;

    std::uint32_t const uAccountSequence = sleCreator->getFieldU32(sfSequence);

    auto viewJ = ctx.app.journal("View");

    if (isGlobalFrozen(ctx.view, uPaysIssuerID) ||
        isGlobalFrozen(ctx.view, uGetsIssuerID))
    {
        JLOG(ctx.j.info()) <<
            "Offer involves frozen asset";

        return tecFROZEN;
    }
    else if (accountFunds(ctx.view, id, saTakerGets,
        fhZERO_IF_FROZEN, viewJ) <= beast::zero)
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
        // Note that this will get checked again in applyGuts, but it saves
        // us a call to checkAcceptAsset and possible false negative.
        //
        // The return code change is attached to featureChecks as a convenience.
        // The change is not big enough to deserve its own amendment.
        return ctx.view.rules().enabled(featureDepositPreauth)
            ? TER {tecEXPIRED}
            : TER {tesSUCCESS};
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
            ? TER {terNO_ACCOUNT}
            : TER {tecNO_ISSUER};
    }

    // This code is attached to the DepositPreauth amendment as a matter of
    // convenience.  The change is not significant enough to deserve its
    // own amendment.
    if (view.rules().enabled(featureDepositPreauth) && (issue.account == id))
        // An account can always accept its own issuance.
        return tesSUCCESS;

    if ((*issuerAccount)[sfFlags] & lsfRequireAuth)
    {
        auto const trustLine = view.read(
            keylet::line(id, issue.account, issue.currency));

        if (!trustLine)
        {
            return (flags & tapRETRY)
                ? TER {terNO_LINE}
                : TER {tecNO_LINE};
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
                ? TER {terNO_AUTH}
                : TER {tecNO_AUTH};
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
    return (amount <= beast::zero);
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
    auto const& takerAmount = taker.original_offer ();

    assert (!isXRP (takerAmount.in) && !isXRP (takerAmount.out));

    if (isXRP (takerAmount.in) || isXRP (takerAmount.out))
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

        auto const [use_direct, quality] = select_path (
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

            if (view.rules().enabled (fixTakerDryOfferRemoval))
            {
                // have_bridge can be true the next time 'round only if
                // neither of the OfferStreams are dry.
                leg1_consumed = dry_offer (view, offers_leg1.tip());
                if (leg1_consumed)
                    have_bridge &= offers_leg1.step();

                leg2_consumed = dry_offer (view, offers_leg2.tip());
                if (leg2_consumed)
                    have_bridge &= offers_leg2.step();
            }
            else
            {
                // This old behavior may leave an empty offer in the book for
                // the second leg.
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
            stream << "quality: " << offer.quality();
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
CreateOffer::takerCross (
    Sandbox& sb,
    Sandbox& sbCancel,
    Amounts const& takerAmount)
{
    NetClock::time_point const when{ctx_.view().parentCloseTime()};

    beast::WrappedSink takerSink (j_, "Taker ");

    Taker taker (cross_type_, sb, account_, takerAmount,
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
        return { tecUNFUNDED_OFFER, takerAmount };
    }

    try
    {
        if (cross_type_ == CrossType::IouToIou)
            return bridged_cross (taker, sb, sbCancel, when);

        return direct_cross (taker, sb, sbCancel, when);
    }
    catch (std::exception const& e)
    {
        JLOG (j_.error()) <<
            "Exception during offer crossing: " << e.what ();
        return { tecINTERNAL, taker.remaining_offer () };
    }
}

std::pair<TER, Amounts>
CreateOffer::flowCross (
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
        STAmount const inStartBalance = accountFunds (
            psb, account_, takerAmount.in, fhZERO_IF_FROZEN, j_);
        if (inStartBalance <= beast::zero)
        {
            // The account balance can't cover even part of the offer.
            JLOG (j_.debug()) <<
                "Not crossing: taker is unfunded.";
            return { tecUNFUNDED_OFFER, takerAmount };
        }

        // If the gateway has a transfer rate, accommodate that.  The
        // gateway takes its cut without any special consent from the
        // offer taker.  Set sendMax to allow for the gateway's cut.
        Rate gatewayXferRate {QUALITY_ONE};
        STAmount sendMax = takerAmount.in;
        if (! sendMax.native() && (account_ != sendMax.getIssuer()))
        {
            gatewayXferRate = transferRate (psb, sendMax.getIssuer());
            if (gatewayXferRate.value != QUALITY_ONE)
            {
                sendMax = multiplyRound (takerAmount.in,
                    gatewayXferRate, takerAmount.in.issue(), true);
            }
        }

        // Payment flow code compares quality after the transfer rate is
        // included.  Since transfer rate is incorporated compute threshold.
        Quality threshold { takerAmount.out, sendMax };

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
        if (!takerAmount.in.native() & !takerAmount.out.native())
        {
            STPath path;
            path.emplace_back (boost::none, xrpCurrency(), boost::none);
            paths.emplace_back (std::move(path));
        }
        // Special handling for the tfSell flag.
        STAmount deliver = takerAmount.out;
        if (txFlags & tfSell)
        {
            // We are selling, so we will accept *more* than the offer
            // specified.  Since we don't know how much they might offer,
            // we allow delivery of the largest possible amount.
            if (deliver.native())
                deliver = STAmount { STAmount::cMaxNative };
            else
                // We can't use the maximum possible currency here because
                // there might be a gateway transfer rate to account for.
                // Since the transfer rate cannot exceed 200%, we use 1/2
                // maxValue for our limit.
                deliver = STAmount { takerAmount.out.issue(),
                    STAmount::cMaxValue / 2, STAmount::cMaxOffset };
        }

        // Call the payment engine's flow() to do the actual work.
        auto const result = flow (psb, deliver, account_, account_,
            paths,
            true,                       // default path
            ! (txFlags & tfFillOrKill), // partial payment
            true,                       // owner pays transfer fee
            true,                       // offer crossing
            threshold,
            sendMax, j_);

        // If stale offers were found remove them.
        for (auto const& toRemove : result.removableOffers)
        {
            if (auto otr = psb.peek (keylet::offer (toRemove)))
                offerDelete (psb, otr, j_);
            if (auto otr = psbCancel.peek (keylet::offer (toRemove)))
                offerDelete (psbCancel, otr, j_);
        }

        // Determine the size of the final offer after crossing.
        auto afterCross = takerAmount; // If !tesSUCCESS offer unchanged
        if (isTesSuccess (result.result()))
        {
            STAmount const takerInBalance = accountFunds (
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
                STAmount const rate {
                    Quality{takerAmount.out, takerAmount.in}.rate() };

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
                        nonGatewayAmountIn = divideRound (result.actualAmountIn,
                            gatewayXferRate, takerAmount.in.issue(), true);

                    afterCross.in -= nonGatewayAmountIn;

                    // It's possible that the divRound will cause our subtract
                    // to go slightly negative.  So limit afterCross.in to zero.
                    if (afterCross.in < beast::zero)
                        // We should verify that the difference *is* small, but
                        // what is a good threshold to check?
                        afterCross.in.clear();

                    afterCross.out = divRound (afterCross.in,
                        rate, takerAmount.out.issue(), true);
                }
                else
                {
                    // If not selling, we scale the input based on the
                    // remaining output.  This too preserves the offer
                    // Quality.
                    afterCross.out -= result.actualAmountOut;
                    assert (afterCross.out >= beast::zero);
                    if (afterCross.out < beast::zero)
                        afterCross.out.clear();
                    afterCross.in = mulRound (afterCross.out,
                        rate, takerAmount.in.issue(), true);
                }
            }
        }

        // Return how much of the offer is left.
        return { tesSUCCESS, afterCross };
    }
    catch (std::exception const& e)
    {
        JLOG (j_.error()) <<
            "Exception during offer crossing: " << e.what ();
    }
    return { tecINTERNAL, takerAmount };
}

enum class SBoxCmp
{
    same,
    dustDiff,
    offerDelDiff,
    xrpRound,
    diff
};

static std::string to_string (SBoxCmp c)
{
    switch (c)
    {
    case SBoxCmp::same:
        return "same";
    case SBoxCmp::dustDiff:
        return "dust diffs";
    case SBoxCmp::offerDelDiff:
        return "offer del diffs";
    case SBoxCmp::xrpRound:
        return "XRP round to zero";
    case SBoxCmp::diff:
        return "different";
    }
    return {};
}

static SBoxCmp compareSandboxes (char const* name, ApplyContext const& ctx,
    detail::ApplyViewBase const& viewTaker, detail::ApplyViewBase const& viewFlow,
    beast::Journal j)
{
    SBoxCmp c = SBoxCmp::same;
    CashDiff diff = cashFlowDiff (
        CashFilter::treatZeroOfferAsDeletion, viewTaker,
        CashFilter::none, viewFlow);

    if (diff.hasDiff())
    {
        using namespace beast::severities;
        // There is a special case of an offer with XRP on one side where
        // the XRP gets rounded to zero.  It mostly looks like dust-level
        // differences.  It is easier to detect if we look for it before
        // removing the dust differences.
        if (int const side = diff.xrpRoundToZero())
        {
            char const* const whichSide = side > 0 ? "; Flow" : "; Taker";
            j.stream (kWarning) << "FlowCross: " << name << " different" <<
                whichSide << " XRP rounded to zero.  tx: " <<
                ctx.tx.getTransactionID();
            return SBoxCmp::xrpRound;
        }

        c = SBoxCmp::dustDiff;
        Severity s = kInfo;
        std::string diffDesc = ", but only dust.";
        diff.rmDust();
        if (diff.hasDiff())
        {
            // From here on we want to note the transaction ID of differences.
            std::stringstream txIdSs;
            txIdSs << ".  tx: " << ctx.tx.getTransactionID();
            auto txID = txIdSs.str();

            // Sometimes one version deletes offers that the other doesn't
            // delete.  That's okay, but keep track of it.
            c = SBoxCmp::offerDelDiff;
            s = kWarning;
            int sides = diff.rmLhsDeletedOffers() ? 1 : 0;
            sides    |= diff.rmRhsDeletedOffers() ? 2 : 0;
            if (!diff.hasDiff())
            {
                char const* t = "";
                switch (sides)
                {
                case 1: t = "; Taker deleted more offers"; break;
                case 2: t = "; Flow deleted more offers"; break;
                case 3: t = "; Taker and Flow deleted different offers"; break;
                default: break;
                }
                diffDesc = std::string(t) + txID;
            }
            else
            {
                // A difference without a broad classification...
                c = SBoxCmp::diff;
                std::stringstream ss;
                ss << "; common entries: " << diff.commonCount()
                    << "; Taker unique: " << diff.lhsOnlyCount()
                    << "; Flow unique: " << diff.rhsOnlyCount() << txID;
                diffDesc = ss.str();
            }
        }
        j.stream (s) << "FlowCross: " << name << " different" << diffDesc;
    }
    return c;
}

std::pair<TER, Amounts>
CreateOffer::cross (
    Sandbox& sb,
    Sandbox& sbCancel,
    Amounts const& takerAmount)
{
    using beast::zero;

    // There are features for Flow offer crossing and for comparing results
    // between Taker and Flow offer crossing.  Turn those into bools.
    bool const useFlowCross { sb.rules().enabled (featureFlowCross) };
    bool const doCompare { sb.rules().enabled (featureCompareTakerFlowCross) };

    Sandbox sbTaker { &sb };
    Sandbox sbCancelTaker { &sbCancel };
    auto const takerR = (!useFlowCross || doCompare)
        ? takerCross (sbTaker, sbCancelTaker, takerAmount)
        : std::make_pair (tecINTERNAL, takerAmount);

    PaymentSandbox psbFlow { &sb };
    PaymentSandbox psbCancelFlow { &sbCancel };
    auto const flowR = (useFlowCross || doCompare)
        ? flowCross (psbFlow, psbCancelFlow, takerAmount)
        : std::make_pair (tecINTERNAL, takerAmount);

    if (doCompare)
    {
        SBoxCmp c = SBoxCmp::same;
        if (takerR.first != flowR.first)
        {
            c = SBoxCmp::diff;
            j_.warn() << "FlowCross: Offer cross tec codes different.  tx: "
                << ctx_.tx.getTransactionID();
        }
        else if ((takerR.second.in  == zero && flowR.second.in  == zero) ||
           (takerR.second.out == zero && flowR.second.out == zero))
        {
            c = compareSandboxes ("Both Taker and Flow fully crossed",
                ctx_, sbTaker, psbFlow, j_);
        }
        else if (takerR.second.in == zero && takerR.second.out == zero)
        {
            char const * crossType = "Taker fully crossed, Flow partially crossed";
            if (flowR.second.in == takerAmount.in &&
                flowR.second.out == takerAmount.out)
                    crossType = "Taker fully crossed, Flow not crossed";

            c = compareSandboxes (crossType, ctx_, sbTaker, psbFlow, j_);
        }
        else if (flowR.second.in == zero && flowR.second.out == zero)
        {
            char const * crossType =
                "Taker partially crossed, Flow fully crossed";
            if (takerR.second.in == takerAmount.in &&
                takerR.second.out == takerAmount.out)
                    crossType = "Taker not crossed, Flow fully crossed";

            c = compareSandboxes (crossType, ctx_, sbTaker, psbFlow, j_);
        }
        else if (ctx_.tx.getFlags() & tfFillOrKill)
        {
            c = compareSandboxes (
                "FillOrKill offer", ctx_, sbCancelTaker, psbCancelFlow, j_);
        }
        else if (takerR.second.in  == takerAmount.in &&
                 flowR.second.in   == takerAmount.in &&
                 takerR.second.out == takerAmount.out &&
                 flowR.second.out  == takerAmount.out)
        {
            char const * crossType = "Neither Taker nor Flow crossed";
            c = compareSandboxes (crossType, ctx_, sbTaker, psbFlow, j_);
        }
        else if (takerR.second.in == takerAmount.in &&
                 takerR.second.out == takerAmount.out)
        {
            char const * crossType = "Taker not crossed, Flow partially crossed";
            c = compareSandboxes (crossType, ctx_, sbTaker, psbFlow, j_);
        }
        else if (flowR.second.in == takerAmount.in &&
                 flowR.second.out == takerAmount.out)
        {
            char const * crossType = "Taker partially crossed, Flow not crossed";
            c = compareSandboxes (crossType, ctx_, sbTaker, psbFlow, j_);
        }
        else
        {
            c = compareSandboxes (
                "Partial cross offer", ctx_, sbTaker, psbFlow, j_);

            // If we've gotten this far then the returned amounts matter.
            if (c <= SBoxCmp::dustDiff && takerR.second != flowR.second)
            {
                c = SBoxCmp::dustDiff;
                using namespace beast::severities;
                Severity s = kInfo;
                std::string onlyDust = ", but only dust.";
                if (! diffIsDust (takerR.second.in, flowR.second.in) ||
                    (! diffIsDust (takerR.second.out, flowR.second.out)))
                {
                    char const* outSame = "";
                    if (takerR.second.out == flowR.second.out)
                        outSame = " but outs same";

                    c = SBoxCmp::diff;
                    s = kWarning;
                    std::stringstream ss;
                    ss << outSame
                        << ".  Taker in: " << takerR.second.in.getText()
                        << "; Taker out: " << takerR.second.out.getText()
                        << "; Flow in: " << flowR.second.in.getText()
                        << "; Flow out: " << flowR.second.out.getText()
                        << ".  tx: " << ctx_.tx.getTransactionID();
                    onlyDust = ss.str();
                }
                j_.stream (s) << "FlowCross: Partial cross amounts different"
                    << onlyDust;
            }
        }
        j_.error() << "FlowCross cmp result: " << to_string (c);
    }

    // Return one result or the other based on amendment.
    if (useFlowCross)
    {
        psbFlow.apply (sb);
        psbCancelFlow.apply (sbCancel);
        return flowR;
    }

    sbTaker.apply (sb);
    sbCancelTaker.apply (sbCancel);
    return takerR;
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
CreateOffer::applyGuts (Sandbox& sb, Sandbox& sbCancel)
{
    using beast::zero;

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
    auto const uSequence = ctx_.tx.getSequence ();

    // This is the original rate of the offer, and is the rate at which
    // it will be placed, even if crossing offers change the amounts that
    // end up on the books.
    auto uRate = getRate (saTakerGets, saTakerPays);

    auto viewJ = ctx_.app.journal("View");

    TER result = tesSUCCESS;

    // Process a cancellation request that's passed along with an offer.
    if (cancelSequence)
    {
        auto const sleCancel = sb.peek(
            keylet::offer(account_, *cancelSequence));

        // It's not an error to not find the offer to cancel: it might have
        // been consumed or removed. If it is found, however, it's an error
        // to fail to delete it.
        if (sleCancel)
        {
            JLOG(j_.debug()) << "Create cancels order " << *cancelSequence;
            result = offerDelete (sb, sleCancel, viewJ);
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
        //
        // The return code change is attached to featureDepositPreauth as a
        // convenience.  The change is not big enough to deserve a fix code.
        TER const ter {ctx_.view().rules().enabled(
            featureDepositPreauth) ? TER {tecEXPIRED} : TER {tesSUCCESS}};
        return{ ter, true };
    }

    bool const bOpenLedger = ctx_.view().open();
    bool crossed = false;

    if (result == tesSUCCESS)
    {
        // If a tick size applies, round the offer to the tick size
        auto const& uPaysIssuerID = saTakerPays.getIssuer ();
        auto const& uGetsIssuerID = saTakerGets.getIssuer ();

        std::uint8_t uTickSize = Quality::maxTickSize;
        if (!isXRP (uPaysIssuerID))
        {
            auto const sle =
                sb.read(keylet::account(uPaysIssuerID));
            if (sle && sle->isFieldPresent (sfTickSize))
                uTickSize = std::min (uTickSize,
                    (*sle)[sfTickSize]);
        }
        if (!isXRP (uGetsIssuerID))
        {
            auto const sle =
                sb.read(keylet::account(uGetsIssuerID));
            if (sle && sle->isFieldPresent (sfTickSize))
                uTickSize = std::min (uTickSize,
                    (*sle)[sfTickSize]);
        }
        if (uTickSize < Quality::maxTickSize)
        {
            auto const rate =
                Quality{saTakerGets, saTakerPays}.round
                    (uTickSize).rate();

            // We round the side that's not exact,
            // just as if the offer happened to execute
            // at a slightly better (for the placer) rate
            if (bSell)
            {
                // this is a sell, round taker pays
                saTakerPays = multiply (
                    saTakerGets, rate, saTakerPays.issue());
            }
            else
            {
                // this is a buy, round taker gets
                saTakerGets = divide (
                    saTakerPays, rate, saTakerGets.issue());
            }
            if (! saTakerGets || ! saTakerPays)
            {
                JLOG (j_.debug()) <<
                    "Offer rounded to zero";
                return { result, true };
            }

            uRate = getRate (saTakerGets, saTakerPays);
        }

        // We reverse pays and gets because during crossing we are taking.
        Amounts const takerAmount (saTakerGets, saTakerPays);

        // The amount of the offer that is unfilled after crossing has been
        // performed. It may be equal to the original amount (didn't cross),
        // empty (fully crossed), or something in-between.
        Amounts place_offer;

        JLOG(j_.debug()) << "Attempting cross: " <<
            to_string (takerAmount.in.issue ()) << " -> " <<
            to_string (takerAmount.out.issue ());

        if (auto stream = j_.trace())
        {
            stream << "   mode: " <<
                (bPassive ? "passive " : "") <<
                (bSell ? "sell" : "buy");
            stream <<"     in: " << format_amount (takerAmount.in);
            stream << "    out: " << format_amount (takerAmount.out);
        }

        std::tie(result, place_offer) = cross (sb, sbCancel, takerAmount);

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

        if (takerAmount != place_offer)
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
        if (sb.rules().enabled (fix1578))
            return { tecKILLED, false };
        return { tesSUCCESS, false };
    }

    // For 'immediate or cancel' offers, the amount remaining doesn't get
    // placed - it gets canceled and the operation succeeds.
    if (bImmediateOrCancel)
    {
        JLOG (j_.trace()) << "Immediate or cancel: offer canceled";
        return { tesSUCCESS, true };
    }

    auto const sleCreator = sb.peek (keylet::account(account_));
    if (! sleCreator)
        return { tefINTERNAL, false };

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

    // Add offer to owner's directory.
    auto const ownerNode = dirAdd(sb, keylet::ownerDir (account_),
        offer_index, false, describeOwnerDir (account_), viewJ);

    if (!ownerNode)
    {
        JLOG (j_.debug()) <<
            "final result: failed to add offer to owner's directory";
        return { tecDIR_FULL, true };
    }

    // Update owner count.
    adjustOwnerCount(sb, sleCreator, 1, viewJ);

    JLOG (j_.trace()) <<
        "adding to book: " << to_string (saTakerPays.issue ()) <<
        " : " << to_string (saTakerGets.issue ());

    Book const book { saTakerPays.issue(), saTakerGets.issue() };

    // Add offer to order book, using the original rate
    // before any crossing occured.
    auto dir = keylet::quality (keylet::book (book), uRate);
    bool const bookExisted = static_cast<bool>(sb.peek (dir));

    auto const bookNode = dirAdd (sb, dir, offer_index, true,
        [&](SLE::ref sle)
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
        }, viewJ);

    if (!bookNode)
    {
        JLOG (j_.debug()) <<
            "final result: failed to add offer to book";
        return { tecDIR_FULL, true };
    }

    auto sleOffer = std::make_shared<SLE>(ltOFFER, offer_index);
    sleOffer->setAccountID (sfAccount, account_);
    sleOffer->setFieldU32 (sfSequence, uSequence);
    sleOffer->setFieldH256 (sfBookDirectory, dir.key);
    sleOffer->setFieldAmount (sfTakerPays, saTakerPays);
    sleOffer->setFieldAmount (sfTakerGets, saTakerGets);
    sleOffer->setFieldU64 (sfOwnerNode, *ownerNode);
    sleOffer->setFieldU64 (sfBookNode, *bookNode);
    if (expiration)
        sleOffer->setFieldU32 (sfExpiration, *expiration);
    if (bPassive)
        sleOffer->setFlag (lsfPassive);
    if (bSell)
        sleOffer->setFlag (lsfSell);
    sb.insert(sleOffer);

    if (!bookExisted)
        ctx_.app.getOrderBookDB().addOrderBook(book);

    JLOG (j_.debug()) << "final result: success";

    return { tesSUCCESS, true };
}

TER
CreateOffer::doApply()
{
    // This is the ledger view that we work against. Transactions are applied
    // as we go on processing transactions.
    Sandbox sb (&ctx_.view());

    // This is a ledger with just the fees paid and any unfunded or expired
    // offers we encounter removed. It's used when handling Fill-or-Kill offers,
    // if the order isn't going to be placed, to avoid wasting the work we did.
    Sandbox sbCancel (&ctx_.view());

    auto const result = applyGuts(sb, sbCancel);
    if (result.second)
        sb.apply(ctx_.rawView());
    else
        sbCancel.apply(ctx_.rawView());
    return result.first;
}

}
