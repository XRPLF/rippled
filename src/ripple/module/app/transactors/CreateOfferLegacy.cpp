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

namespace ripple {

/** Determine if an order is still valid
    If the order is not valid it will be marked as unfunded.
*/
bool CreateOfferLegacy::isValidOffer (
    SLE::ref sleOffer,
    uint160 const& uOfferOwnerID,
    STAmount const& saOfferPays,
    STAmount const& saOfferGets,
    uint160 const& uTakerAccountID,
    std::unordered_set<uint256, beast::hardened_hash<uint256>>& usOfferUnfundedBecame,
    std::unordered_set<uint160, beast::hardened_hash<uint160>>& usAccountTouched,
    STAmount& saOfferFunds)
{
    if (sleOffer->isFieldPresent (sfExpiration) &&
        sleOffer->getFieldU32 (sfExpiration) <= mEngine->getLedger ()->getParentCloseTimeNC ())
    {
        // Offer is expired. Expired offers are considered unfunded. Delete it.
        m_journal.debug << "isValidOffer: encountered expired offer";

        usOfferUnfundedFound.insert (sleOffer->getIndex());

        return false;
    }

    if (uOfferOwnerID == uTakerAccountID)
    {
        // Would take own offer. Consider old offer expired. Delete it.
        m_journal.debug << "isValidOffer: encountered taker's own old offer";

        usOfferUnfundedFound.insert (sleOffer->getIndex());

        return false;
    }

    if (saOfferGets <= zero || saOfferPays <= zero)
    {
        // Offer has bad amounts. Consider offer expired. Delete it.
        m_journal.warning << "isValidOffer: BAD OFFER:" <<
            " saOfferPays=" << saOfferPays <<
            " saOfferGets=" << saOfferGets;

        usOfferUnfundedFound.insert (sleOffer->getIndex());

        return false;
    }

    m_journal.debug <<
        "isValidOffer: saOfferPays=" << saOfferPays.getFullText ();

    saOfferFunds = mEngine->view ().accountFunds (uOfferOwnerID, saOfferPays);

    if (saOfferFunds <= zero)
    {
        // Offer is unfunded, possibly due to previous balance action.
        m_journal.debug << "isValidOffer: offer unfunded: delete";

        auto account = usAccountTouched.find (uOfferOwnerID);

        if (account != usAccountTouched.end ())
        {
            // Previously touched account. Delete unfunded offer on success
            usOfferUnfundedBecame.insert (sleOffer->getIndex());
        }
        else
        {
            // Never touched source account.  Delete found unfunded offer
            // when possible.
            usOfferUnfundedFound.insert (sleOffer->getIndex());
        }

        return false;
    }

    return true;
}

/**
*/
bool CreateOfferLegacy::canCross (
    STAmount const& saTakerFunds,
    STAmount const& saSubTakerPays,
    STAmount const& saSubTakerGets,
    std::uint64_t uTipQuality,
    std::uint64_t uTakeQuality,
    bool isPassive,
    bool& isUnfunded,
    TER& terResult) const
{
    if (saTakerFunds <= zero)
    {
        // Taker is out of funds. Don't create the offer.
        isUnfunded = true;
        terResult = tesSUCCESS;
        return false;
    }

    if (saSubTakerPays <= zero || saSubTakerGets <= zero)
    {
        // Offer is completely consumed
        terResult = tesSUCCESS;
        return false;
    }

    // TODO We must also consider the synthesized tip as well
    if ((uTakeQuality < uTipQuality) || (isPassive && (uTakeQuality == uTipQuality)))
    {
        // Offer does not cross this offer
        terResult = tesSUCCESS;
        return false;
    }

    return true;
}

/** Apply a particular offer

    An existing offer is on the books. The price is the offer owner's, which
    might be better for taker. The taker pays what they can and gets all taker
    can pay for with `saTakerFunds`/`uTakerPaysRate`, limited by `saOfferPays`
    and `saOfferFunds`/`uOfferPaysRate`.
    If taker is an offer, taker is spending at same or better rate than they
    wanted.
    The taker should consider themselves as wanting to buy X amount and as
    willing to pay at most the rate of Y/X each.
    After having some part of their offer fulfilled at a better rate their offer
    should be reduced accordingly.
    There are no quality costs for offer vs offer taking.

    @param bSell `true` if using sell semantics.
    @param uTakerPaysRate >= QUALITY_ONE | TransferRate for third party IOUs paid by taker.
    @param uOfferPaysRate >= QUALITY_ONE | TransferRate for third party IOUs paid by offer owner.
    @param saOfferRate Original saOfferGets/saOfferPays, when offer was made.
    @param saOfferFunds Limit for saOfferPays : How much can pay including fees.
    @param saTakerFunds Limit for saOfferGets : How much can pay including fees.
    @param saOfferPays Request : this should be reduced as the offer is fullfilled.
    @param saOfferGets Request : this should be reduced as the offer is fullfilled.
    @param saTakerPays Limit for taker to pay.
    @param saTakerGets Limit for taker to get.
    @param saTakerPaid Actual amount the taker paid
    @param saTakerGot Actual amount the taker got
    @param saTakerIssuerFee Actual
    @param saOfferIssuerFee Actual

    @note Buy vs. Sell semantics:

    In Ripple, placed offers are always executed at the rate at which they were
    placed. However, the offer we are processing hasn't been placed yet and it
    can cross with offers that have. Such crossing offers can be consumed by
    existing offers that give a better rate.

    The only difference between buy and sell offers is the way in which we
    calculate what remains to be placed on the books after crossing offers
    are consumed.

    First, consider the case where a user is trying to place an offer to buy
    1 BTC for 700 USD:
    If an offer to giving 0.5 BTC for 300 USD is on the books, the offers cross
    and under buy semantics the user buys 0.5 BTC for 300 USD (less than the
    350 USD he was willing to pay) and the offer is adjusted and placed on the
    books as: "give 0.5 BTC for 350 USD" which is the amount left to buy at the
    original rate.

    Now consider the case where a user is trying to place an offer to sell
    700 USD for 1 BTC instead:
    If an offer to giving 0.5 BTC for 300 USD is on the books, the offers cross
    and under sell semantics the user will sell 350 USD for 0.5 BTC (more than
    he was willing to get) and his offer will be be adjusted and placed on the
    books as: "give 0.57 BTC for 400 USD" which is what is left to sell at the
    original rate.
*/
bool CreateOfferLegacy::applyOffer (
    const bool bSell,
    const std::uint32_t uTakerPaysRate, const std::uint32_t uOfferPaysRate,
    const STAmount& saOfferRate,
    const STAmount& saOfferFunds, const STAmount& saTakerFunds,
    const STAmount& saOfferPays, const STAmount& saOfferGets,
    const STAmount& saTakerPays, const STAmount& saTakerGets,
    STAmount& saTakerPaid, STAmount& saTakerGot,
    STAmount& saTakerIssuerFee, STAmount& saOfferIssuerFee)
{
    saOfferGets.throwComparable (saTakerFunds);

    assert (saOfferFunds > zero && saTakerFunds > zero); // Both must have funds.
    assert (saOfferGets > zero && saOfferPays > zero); // Must not be a null offer.

    // Available = limited by funds.
    // Limit offerer funds available, by transfer fees.
    STAmount    saOfferFundsAvailable   = QUALITY_ONE == uOfferPaysRate
                                          ? saOfferFunds          // As is.
                                          : STAmount::divide (saOfferFunds, STAmount (CURRENCY_ONE, ACCOUNT_ONE, uOfferPaysRate, -9)); // Reduce by offer fees.

    m_journal.info << "applyOffer: uOfferPaysRate=" << uOfferPaysRate;
    m_journal.info << "applyOffer: saOfferFundsAvailable=" << saOfferFundsAvailable.getFullText ();

    // Limit taker funds available, by transfer fees.
    STAmount    saTakerFundsAvailable   = QUALITY_ONE == uTakerPaysRate
                                          ? saTakerFunds          // As is.
                                          : STAmount::divide (saTakerFunds, STAmount (CURRENCY_ONE, ACCOUNT_ONE, uTakerPaysRate, -9)); // Reduce by taker fees.

    m_journal.info << "applyOffer: TAKER_FEES=" << STAmount (CURRENCY_ONE, ACCOUNT_ONE, uTakerPaysRate, -9).getFullText ();
    m_journal.info << "applyOffer: uTakerPaysRate=" << uTakerPaysRate;
    m_journal.info << "applyOffer: saTakerFundsAvailable=" << saTakerFundsAvailable.getFullText ();

    STAmount    saOfferPaysAvailable;   // Amount offer can pay out, limited by offer and offerer funds.
    STAmount    saOfferGetsAvailable;   // Amount offer would get, limited by offer funds.

    if (saOfferFundsAvailable >= saOfferPays)
    {
        // Offer was fully funded, avoid math shenanigans.

        saOfferPaysAvailable    = saOfferPays;
        saOfferGetsAvailable    = saOfferGets;
    }
    else
    {
        // Offer has limited funding, limit offer gets and pays by funds available.

        saOfferPaysAvailable    = saOfferFundsAvailable;
        saOfferGetsAvailable    = std::min (saOfferGets, STAmount::mulRound (saOfferPaysAvailable, saOfferRate, saOfferGets, true));
    }

    m_journal.info << "applyOffer: saOfferPaysAvailable=" << saOfferPaysAvailable.getFullText ();
    m_journal.info << "applyOffer: saOfferGetsAvailable=" << saOfferGetsAvailable.getFullText ();

    STAmount    saTakerPaysAvailable    = std::min (saTakerPays, saTakerFundsAvailable);
    m_journal.info << "applyOffer: saTakerPaysAvailable=" << saTakerPaysAvailable.getFullText ();

    // Limited = limited by other sides raw numbers.
    // Taker can't pay more to offer than offer can get.
    STAmount    saTakerPaysLimited      = std::min (saTakerPaysAvailable, saOfferGetsAvailable);
    m_journal.info << "applyOffer: saTakerPaysLimited=" << saTakerPaysLimited.getFullText ();

    // Align saTakerGetsLimited with saTakerPaysLimited.
    STAmount    saTakerGetsLimited      = saTakerPaysLimited >= saOfferGetsAvailable              // Cannot actually be greater
                                          ? saOfferPaysAvailable                                  // Potentially take entire offer. Avoid math shenanigans.
                                          : std::min (saOfferPaysAvailable, STAmount::divRound (saTakerPaysLimited, saOfferRate, saTakerGets, true)); // Take a portion of offer.

    m_journal.info << "applyOffer: saOfferRate=" << saOfferRate.getFullText ();
    m_journal.info << "applyOffer: saTakerGetsLimited=" << saTakerGetsLimited.getFullText ();

    // Got & Paid = Calculated by price and transfered without fees.
    // Compute from got as when !bSell, we want got to be exact to finish off offer if possible.

    saTakerGot  = bSell
                  ? saTakerGetsLimited                            // Get all available that are paid for.
                  : std::min (saTakerGets, saTakerGetsLimited);   // Limit by wanted.
    saTakerPaid = saTakerGot >= saTakerGetsLimited
                  ? saTakerPaysLimited
                  : std::min (saTakerPaysLimited, STAmount::mulRound (saTakerGot, saOfferRate, saTakerFunds, true));

    m_journal.info << "applyOffer: saTakerGot=" << saTakerGot.getFullText ();
    m_journal.info << "applyOffer: saTakerPaid=" << saTakerPaid.getFullText ();

    if (uTakerPaysRate == QUALITY_ONE)
    {
        saTakerIssuerFee    = STAmount (saTakerPaid.getCurrency (), saTakerPaid.getIssuer ());
    }
    else
    {
        // Compute fees in a rounding safe way.

        STAmount transferRate (CURRENCY_ONE, ACCOUNT_ONE, uTakerPaysRate, -9);
        m_journal.info << "applyOffer: transferRate="
                       << transferRate.getFullText ();

        // TakerCost includes transfer fees.
        STAmount saTakerCost = STAmount::mulRound (
            saTakerPaid, transferRate, true);

        m_journal.info << "applyOffer: saTakerCost="
                       << saTakerCost.getFullText ();
        m_journal.info << "applyOffer: saTakerFunds="
                       << saTakerFunds.getFullText ();
        saTakerIssuerFee    = saTakerCost > saTakerFunds
                              ? saTakerFunds - saTakerPaid
            // Not enough funds to cover fee, stiff issuer the rounding error.
                              : saTakerCost - saTakerPaid;
        m_journal.info << "applyOffer: saTakerIssuerFee="
                       << saTakerIssuerFee.getFullText ();
        assert (saTakerIssuerFee >= zero);
    }

    if (uOfferPaysRate == QUALITY_ONE)
    {
        saOfferIssuerFee = STAmount (
            saTakerGot.getCurrency (), saTakerGot.getIssuer ());
    }
    else
    {
        // Compute fees in a rounding safe way.
        STAmount saOfferCost = STAmount::mulRound (
            saTakerGot, STAmount (CURRENCY_ONE, ACCOUNT_ONE, uOfferPaysRate, -9),
            true);

        saOfferIssuerFee = saOfferCost > saOfferFunds
            ? saOfferFunds - saTakerGot
            // Not enough funds to cover fee, stiff issuer the rounding error.
            : saOfferCost - saTakerGot;
    }

    m_journal.info << "applyOffer: saTakerGot=" << saTakerGot.getFullText ();

    return saTakerGot >= saOfferPaysAvailable;
    // True, if consumed offer.
}

/** Take as much as possible.
    We adjusts account balances and charges fees on top to taker.

    @param uBookBase The order book to take against.
    @param saTakerPays What the taker offers (w/ issuer)
    @param saTakerGets What the taker wanted (w/ issuer)
    @param saTakerPaid What taker could have paid including saved not including
                       fees. To reduce an offer.
    @param saTakerGot What taker got not including fees. To reduce an offer.
    @param bUnfunded if tesSUCCESS, consider offer unfunded after taking.
    @return tesSUCCESS, terNO_ACCOUNT, telFAILED_PROCESSING, or
            tecFAILED_PROCESSING
*/
TER CreateOfferLegacy::takeOffers (
    const bool          bOpenLedger,
    const bool          bPassive,
    const bool          bSell,
    uint256 const&      uBookBase,
    const uint160&      uTakerAccountID,
    SLE::ref            sleTakerAccount,
    const STAmount&     saTakerPays,
    const STAmount&     saTakerGets,
    STAmount&           saTakerPaid,
    STAmount&           saTakerGot,
    bool&               bUnfunded)
{
    // The book has the most elements. Take the perspective of the book.
    // Book is ordered for taker: taker pays / taker gets (smaller is better)
    // The order is for the other book's currencies for get and pays are
    // opposites.
    // We want the same ratio for the respective currencies so we swap paid and
    // gets for determing take quality.

    assert (saTakerPays && saTakerGets);

    m_journal.debug <<
        "takeOffers: bSell: " << bSell <<
        ": against book: " << to_string (uBookBase);

    LedgerEntrySet& lesActive = mEngine->view ();
    std::uint64_t const uTakeQuality = STAmount::getRate (saTakerGets, saTakerPays);
    STAmount saTakerRate = STAmount::setRate (uTakeQuality);
    uint160 const uTakerPaysAccountID = saTakerPays.getIssuer ();
    uint160 const uTakerGetsAccountID = saTakerGets.getIssuer ();
    TER terResult = temUNCERTAIN;

    // Offers that became unfunded.
    std::unordered_set<uint256, beast::hardened_hash<uint256>> usOfferUnfundedBecame;

    // Accounts touched.
    std::unordered_set<uint160, beast::hardened_hash<uint160>> usAccountTouched;

    saTakerPaid = STAmount (
        saTakerPays.getCurrency (), saTakerPays.getIssuer ());
    saTakerGot = STAmount (
        saTakerGets.getCurrency (), saTakerGets.getIssuer ());
    bUnfunded = false;

    // TODO: need to track the synthesized book (source->XRP + XRP->target)
    //       here as well.
    OrderBookIterator directBookIter (lesActive,
        saTakerPays.getCurrency(), saTakerPays.getIssuer(),
        saTakerGets.getCurrency(), saTakerGets.getIssuer());

    while ((temUNCERTAIN == terResult) && directBookIter.nextOffer())
    {
        STAmount saTakerFunds = lesActive.accountFunds (uTakerAccountID, saTakerPays);
        STAmount saSubTakerPays = saTakerPays - saTakerPaid; // How much more to spend.
        STAmount saSubTakerGets = saTakerGets - saTakerGot; // How much more is wanted.
        std::uint64_t uTipQuality = directBookIter.getCurrentQuality();

        if (!canCross(
                saTakerFunds,
                saSubTakerPays,
                saSubTakerGets,
                uTipQuality,
                uTakeQuality,
                bPassive,
                bUnfunded,
                terResult))
            break;

        // We have a crossing offer to consider.

        // TODO Must consider the synthesized orderbook instead of just the
        // direct one (i.e. look at A->XRP->B)
        SLE::pointer sleOffer = directBookIter.getCurrentOffer ();

        if (!sleOffer)
        { // offer is in directory but not in ledger
            // CHECKME is this cleaning up corruption?
            uint256 offerIndex = directBookIter.getCurrentIndex ();
            m_journal.warning <<
                "takeOffers: offer not found : " << offerIndex;
            usMissingOffers.insert (missingOffer_t (
                directBookIter.getCurrentIndex (),
                directBookIter.getCurrentDirectory ()));

            continue;
        }

        m_journal.debug <<
            "takeOffers: considering offer : " <<
            sleOffer->getJson (0);

        uint160 const&  uOfferOwnerID = sleOffer->getFieldAccount160 (sfAccount);
        STAmount saOfferPays = sleOffer->getFieldAmount (sfTakerGets);
        STAmount saOfferGets = sleOffer->getFieldAmount (sfTakerPays);

        STAmount        saOfferFunds;   // Funds of offer owner to payout.

        bool const bValid = isValidOffer (
            sleOffer,
            uOfferOwnerID,
            saOfferPays,
            saOfferGets,
            uTakerAccountID,
            usOfferUnfundedBecame,
            usAccountTouched,
            saOfferFunds);

        if (bValid)
        {
            STAmount    saSubTakerPaid;
            STAmount    saSubTakerGot;
            STAmount    saTakerIssuerFee;
            STAmount    saOfferIssuerFee;
            STAmount    saOfferRate = STAmount::setRate (uTipQuality);

            if (m_journal.debug)
            {
                m_journal.debug <<
                    "takeOffers: applyOffer:    saTakerPays: " <<
                    saTakerPays.getFullText ();
                m_journal.debug <<
                    "takeOffers: applyOffer:    saTakerPaid: " <<
                    saTakerPaid.getFullText ();
                m_journal.debug <<
                    "takeOffers: applyOffer:   saTakerFunds: " <<
                    saTakerFunds.getFullText ();
                m_journal.debug <<
                    "takeOffers: applyOffer:   saOfferFunds: " <<
                    saOfferFunds.getFullText ();
                m_journal.debug <<
                    "takeOffers: applyOffer:    saOfferPays: " <<
                    saOfferPays.getFullText ();
                m_journal.debug <<
                    "takeOffers: applyOffer:    saOfferGets: " <<
                    saOfferGets.getFullText ();
                m_journal.debug <<
                    "takeOffers: applyOffer:    saOfferRate: " <<
                    saOfferRate.getFullText ();
                m_journal.debug <<
                    "takeOffers: applyOffer: saSubTakerPays: " <<
                    saSubTakerPays.getFullText ();
                m_journal.debug <<
                    "takeOffers: applyOffer: saSubTakerGets: " <<
                    saSubTakerGets.getFullText ();
                m_journal.debug <<
                    "takeOffers: applyOffer:    saTakerPays: " <<
                    saTakerPays.getFullText ();
                m_journal.debug <<
                    "takeOffers: applyOffer:    saTakerGets: " <<
                    saTakerGets.getFullText ();
            }

            bool const bOfferDelete = applyOffer (
                bSell,
                lesActive.rippleTransferRate (
                    uTakerAccountID,
                    uOfferOwnerID,
                    uTakerPaysAccountID),
                lesActive.rippleTransferRate (
                    uOfferOwnerID,
                    uTakerAccountID,
                    uTakerGetsAccountID),
                saOfferRate,
                saOfferFunds,
                saTakerFunds,
                saOfferPays,
                saOfferGets,
                saSubTakerPays,
                saSubTakerGets,
                saSubTakerPaid,
                saSubTakerGot,
                saTakerIssuerFee,
                saOfferIssuerFee);

            if (m_journal.debug)
            {
                m_journal.debug <<
                    "takeOffers: applyOffer: saSubTakerPaid: " <<
                    saSubTakerPaid.getFullText ();
                m_journal.debug <<
                    "takeOffers: applyOffer:  saSubTakerGot: " <<
                    saSubTakerGot.getFullText ();
            }

            // Adjust offer
            // TODO We need to consider the combined (synthesized)
            // order book.

            // Offer owner will pay less.  Subtract what taker just got.
            saOfferPays -= saSubTakerGot;
            sleOffer->setFieldAmount (sfTakerGets, saOfferPays);

            // Offer owner will get less.  Subtract what owner just paid.
            saOfferGets -= saSubTakerPaid;
            sleOffer->setFieldAmount (sfTakerPays, saOfferGets);

            mEngine->entryModify (sleOffer);

            if (bOfferDelete)
            {
                // TODO need to handle the synthetic case here

                // Offer now fully claimed or now unfunded.
                m_journal.debug <<
                    "takeOffers: Offer claimed: Delete.";

                // Delete unfunded offer on success.
                usOfferUnfundedBecame.insert (sleOffer->getIndex());

                // Offer owner's account is no longer pristine.
                usAccountTouched.insert (uOfferOwnerID);
            }
            else if (saSubTakerGot)
            {
                m_journal.debug <<
                    "takeOffers: Offer partial claim.";

                // TODO check the synthetic case here (to ensure there
                //      is no corruption)

                if (saOfferPays <= zero || saOfferGets <= zero)
                {
                    m_journal.warning <<
                        "takeOffers: ILLEGAL OFFER RESULT.";
                    bUnfunded   = true;
                    terResult   = bOpenLedger
                                    ? telFAILED_PROCESSING
                                    : tecFAILED_PROCESSING;
                }
            }
            else
            {
                // Taker got nothing, probably due to rounding. Consider
                // taker unfunded.
                m_journal.debug << "takeOffers: No claim.";

                bUnfunded   = true;
                terResult   = tesSUCCESS;                   // Done.
            }

            assert (uTakerGetsAccountID == saSubTakerGot.getIssuer ());
            assert (uTakerPaysAccountID == saSubTakerPaid.getIssuer ());

            if (!bUnfunded)
            {
                // Distribute funds. The sends charge appropriate fees
                // which are implied by offer.

                // TODO Adjust for synthetic transfers - pay into A->XRP
                // and pay out of XRP->B

                // Offer owner pays taker.
                terResult = lesActive.accountSend (
                    uOfferOwnerID, uTakerAccountID, saSubTakerGot);

                if (tesSUCCESS == terResult)
                {
                    // TODO: in the synthesized case, pay from B to the original taker
                    // Taker -> A -> XRP -> B -> ... -> Taker

                    // Taker pays offer owner.
                    terResult   = lesActive.accountSend (
                        uTakerAccountID, uOfferOwnerID, saSubTakerPaid);
                }

                if (bSell)
                {
                    // Sell semantics:
                    // Reduce amount considered received to original
                    // offer's rate. Not by the crossing rate, which is
                    // higher.
                    STAmount saEffectiveGot = STAmount::divide(
                        saSubTakerPaid, saTakerRate, saTakerGets);
                    saSubTakerGot = std::min(saEffectiveGot, saSubTakerGot);
                }
                else
                {
                    // Buy semantics: Reduce amount considered paid by
                    // taker's rate. Not by actual cost which is lower.
                    // That is, take less as to just satify our buy
                    // requirement.

                    // Taker could pay.
                    STAmount saTakerCould = saTakerPays - saTakerPaid;

                    if (saTakerFunds < saTakerCould)
                        saTakerCould = saTakerFunds;

                    STAmount saTakerUsed = STAmount::multiply (
                        saSubTakerGot, saTakerRate, saTakerPays);

                    if (m_journal.debug)
                    {
                        m_journal.debug <<
                            "takeOffers: applyOffer:   saTakerCould: " <<
                            saTakerCould.getFullText ();
                        m_journal.debug <<
                            "takeOffers: applyOffer:  saSubTakerGot: " <<
                            saSubTakerGot.getFullText ();
                        m_journal.debug <<
                            "takeOffers: applyOffer:    saTakerRate: " <<
                            saTakerRate.getFullText ();
                        m_journal.debug <<
                            "takeOffers: applyOffer:    saTakerUsed: " <<
                            saTakerUsed.getFullText ();
                    }

                    saSubTakerPaid  = std::min (saTakerCould, saTakerUsed);
                }

                saTakerPaid     += saSubTakerPaid;
                saTakerGot      += saSubTakerGot;

                if (tesSUCCESS == terResult)
                    terResult   = temUNCERTAIN;
            }
        }
    }

    if (temUNCERTAIN == terResult)
        terResult = tesSUCCESS;

    m_journal.debug <<
        "takeOffers: " << transToken (terResult);

    if (tesSUCCESS == terResult)
    {
        // On success, delete offers that became unfunded.
        for (auto offerIndex : usOfferUnfundedBecame)
        {
            m_journal.debug <<

                "takeOffers: became unfunded: " <<
                    to_string (offerIndex);

            lesActive.offerDelete (offerIndex);
        }
    }

    m_journal.debug <<
        "takeOffers< " << transToken (terResult);

    return terResult;
}

TER CreateOfferLegacy::doApply ()
{
    if (m_journal.debug) m_journal.debug <<
        "OfferCreate> " << mTxn.getJson (0);

    std::uint32_t const uTxFlags = mTxn.getFlags ();
    bool const bPassive (uTxFlags & tfPassive);
    bool const bImmediateOrCancel (uTxFlags & tfImmediateOrCancel);
    bool const bFillOrKill (uTxFlags & tfFillOrKill);
    bool const bSell (uTxFlags & tfSell);
    STAmount saTakerPays = mTxn.getFieldAmount (sfTakerPays);
    STAmount saTakerGets = mTxn.getFieldAmount (sfTakerGets);

    if (!saTakerPays.isLegalNet () || !saTakerGets.isLegalNet ())
        return temBAD_AMOUNT;

    m_journal.debug <<
        "saTakerPays=" << saTakerPays.getFullText () <<
        " saTakerGets=" << saTakerGets.getFullText ();

    uint160 const uPaysIssuerID = saTakerPays.getIssuer ();
    uint160 const uGetsIssuerID = saTakerGets.getIssuer ();

    bool const bHaveExpiration (mTxn.isFieldPresent (sfExpiration));
    bool const bHaveCancel (mTxn.isFieldPresent (sfOfferSequence));

    std::uint32_t const uExpiration = mTxn.getFieldU32 (sfExpiration);
    std::uint32_t const uCancelSequence = mTxn.getFieldU32 (sfOfferSequence);

    // FIXME understand why we use SequenceNext instead of current transaction
    //       sequence to determine the transaction. Why is the offer seuqnce
    //       number insufficient?

    std::uint32_t const uAccountSequenceNext = mTxnAccount->getFieldU32 (sfSequence);
    std::uint32_t const uSequence = mTxn.getSequence ();

    const uint256 uLedgerIndex = Ledger::getOfferIndex (mTxnAccountID, uSequence);

    m_journal.debug <<
        "Creating offer node: " << to_string (uLedgerIndex) <<
        " uSequence=" << uSequence;

    const uint160 uPaysCurrency = saTakerPays.getCurrency ();
    const uint160 uGetsCurrency = saTakerGets.getCurrency ();
    const std::uint64_t uRate = STAmount::getRate (saTakerGets, saTakerPays);

    TER                        terResult                = tesSUCCESS;
    uint256                    uDirectory;        // Delete hints.
    std::uint64_t              uOwnerNode;
    std::uint64_t              uBookNode;

    LedgerEntrySet& lesActive = mEngine->view ();
    LedgerEntrySet lesCheckpoint = lesActive; // Checkpoint with just fees paid.
    lesActive.bumpSeq (); // Begin ledger variance.

    SLE::pointer sleCreator = mEngine->entryCache (
        ltACCOUNT_ROOT, Ledger::getAccountRootIndex (mTxnAccountID));

    if (uTxFlags & tfOfferCreateMask)
    {
        m_journal.debug <<
            "Malformed transaction: Invalid flags set.";

        return temINVALID_FLAG;
    }
    else if (bImmediateOrCancel && bFillOrKill)
    {
        m_journal.debug <<
            "Malformed transaction: both IoC and FoK set.";

        return temINVALID_FLAG;
    }
    else if (bHaveExpiration && !uExpiration)
    {
        m_journal.warning <<
            "Malformed offer: bad expiration";

        terResult   = temBAD_EXPIRATION;
    }
    else if (saTakerPays.isNative () && saTakerGets.isNative ())
    {
        m_journal.warning <<
            "Malformed offer: XRP for XRP";

        terResult   = temBAD_OFFER;
    }
    else if (saTakerPays <= zero || saTakerGets <= zero)
    {
        m_journal.warning <<
            "Malformed offer: bad amount";

        terResult   = temBAD_OFFER;
    }
    else if (uPaysCurrency == uGetsCurrency && uPaysIssuerID == uGetsIssuerID)
    {
        m_journal.warning <<
            "Malformed offer: redundant offer";

        terResult   = temREDUNDANT;
    }
    // FIXME: XRP is not a bad currency, not not allowed as IOU
    else if (CURRENCY_BAD == uPaysCurrency || CURRENCY_BAD == uGetsCurrency)
    {
        m_journal.warning <<
            "Malformed offer: Bad currency.";

        terResult   = temBAD_CURRENCY;
    }
    else if (saTakerPays.isNative () != !uPaysIssuerID || saTakerGets.isNative () != !uGetsIssuerID)
    {
        m_journal.warning <<
            "Malformed offer: bad issuer";

        terResult   = temBAD_ISSUER;
    }
    else if (lesActive.accountFunds (mTxnAccountID, saTakerGets) <= zero)
    {
        m_journal.warning <<
            "delay: Offers must be at least partially funded.";

        terResult   = tecUNFUNDED_OFFER;
    }
    // This can probably be simplified to make sure that you cancel sequences
    // before the transaction sequence number.
    else if (bHaveCancel && (!uCancelSequence || uAccountSequenceNext - 1 <= uCancelSequence))
    {
        m_journal.debug <<
            "uAccountSequenceNext=" << uAccountSequenceNext <<
            " uOfferSequence=" << uCancelSequence;

        terResult   = temBAD_SEQUENCE;
    }

    // Cancel offer.
    if ((tesSUCCESS == terResult) && bHaveCancel)
    {
        const uint256   uCancelIndex = Ledger::getOfferIndex (mTxnAccountID, uCancelSequence);
        SLE::pointer    sleCancel    = mEngine->entryCache (ltOFFER, uCancelIndex);

        if (sleCancel)
        {
            m_journal.warning <<
                "uCancelSequence=" << uCancelSequence;

            terResult   = mEngine->view ().offerDelete (sleCancel);
        }
        else
        {
            // It's not an error to not find the offer to cancel: it might have
            // been consumed or removed as we are processing. Additionally, it
            // might not even have been an offer - we don't care.

            if (m_journal.warning) m_journal.warning <<
                "offer not found: " <<
                RippleAddress::createHumanAccountID (mTxnAccountID) <<
                " : " << uCancelSequence <<
                " : " << to_string (uCancelIndex);
        }
    }

    // We definitely know the time that the parent ledger closed but we do not
    // know the closing time of the ledger under construction.
    // FIXME: Make sure that expiration is documented in terms of the close time
    //        of the previous ledger.
    bool bExpired =
        bHaveExpiration &&
        (mEngine->getLedger ()->getParentCloseTimeNC () >= uExpiration);

    // If all is well and this isn't an offer to XRP, then we make sure we are
    // authorized to hold what the taker will pay.
    if (tesSUCCESS == terResult && !saTakerPays.isNative () && !bExpired)
    {
        SLE::pointer sleTakerPays = mEngine->entryCache (
            ltACCOUNT_ROOT, Ledger::getAccountRootIndex (uPaysIssuerID));

        if (!sleTakerPays)
        {
            m_journal.warning <<
                "delay: can't receive IOUs from non-existent issuer: " <<
                RippleAddress::createHumanAccountID (uPaysIssuerID);

            terResult   = (mParams & tapRETRY) ? terNO_ACCOUNT : tecNO_ISSUER;
        }
        else if (sleTakerPays->getFieldU32 (sfFlags) & lsfRequireAuth)
        {
            SLE::pointer sleRippleState (mEngine->entryCache (
                ltRIPPLE_STATE,
                Ledger::getRippleStateIndex (
                    mTxnAccountID, uPaysIssuerID, uPaysCurrency)));

            // Entries have a canonical representation, determined by a
            // lexicographical "greater than" comparison employing strict weak
            // ordering.
            // Determine which entry we need to access.
            bool const canonical_gt (mTxnAccountID > uPaysIssuerID);

            if (!sleRippleState)
            {
                terResult   = (mParams & tapRETRY)
                    ? terNO_LINE
                    : tecNO_LINE;
            }
            else if (!(sleRippleState->getFieldU32 (sfFlags) & (canonical_gt ? lsfLowAuth : lsfHighAuth)))
            {
                m_journal.debug <<
                    "delay: can't receive IOUs from issuer without auth.";

                terResult   = (mParams & tapRETRY) ? terNO_AUTH : tecNO_AUTH;
            }
        }
    }

    STAmount        saPaid;
    STAmount        saGot;
    bool            bUnfunded   = false;
    const bool      bOpenLedger = (mParams & tapOPEN_LEDGER);

    if ((tesSUCCESS == terResult) && !bExpired)
    {
        const uint256   uTakeBookBase   = Ledger::getBookBase (
            uGetsCurrency, uGetsIssuerID, uPaysCurrency, uPaysIssuerID);

        if (m_journal.trace) m_journal.trace <<
            "take against book:" << to_string (uTakeBookBase) <<
            " for " << saTakerGets.getFullText () <<
            " -> " << saTakerPays.getFullText ();

        // Take using the parameters of the offer.
        if (m_journal.debug) m_journal.debug <<
            "takeOffers: BEFORE saTakerGets=" << saTakerGets.getFullText ();

        terResult   = takeOffers (
                          bOpenLedger,
                          bPassive,
                          bSell,
                          uTakeBookBase,
                          mTxnAccountID,
                          sleCreator,
                          saTakerGets,    // Reverse as we are the taker for taking.
                          saTakerPays,
                          saPaid,         // Buy semantics: how much would have sold at full price. Sell semantics: how much was sold.
                          saGot,          // How much was got.
                          bUnfunded);

        if (m_journal.debug)
        {
            m_journal.debug << "takeOffers=" << terResult;
            m_journal.debug << "takeOffers: saPaid=" << saPaid.getFullText ();
            m_journal.debug << "takeOffers:  saGot=" << saGot.getFullText ();
        }

        if (tesSUCCESS == terResult && !bUnfunded)
        {
            // Reduce pay in from takers by what offer just got.
            saTakerPays -= saGot;
            // Reduce pay out to takers by what srcAccount just paid.
            saTakerGets -= saPaid;

            if (m_journal.debug)
            {
                m_journal.debug <<
                    "takeOffers: AFTER saTakerPays=" <<
                    saTakerPays.getFullText ();
                m_journal.debug <<
                    "takeOffers: AFTER saTakerGets=" <<
                    saTakerGets.getFullText ();
            }
        }
    }

    if (m_journal.debug)
    {
        m_journal.debug <<
            "takeOffers: saTakerPays=" <<saTakerPays.getFullText ();
        m_journal.debug <<
            "takeOffers: saTakerGets=" << saTakerGets.getFullText ();
        m_journal.debug <<
            "takeOffers: mTxnAccountID=" <<
            RippleAddress::createHumanAccountID (mTxnAccountID);
        m_journal.debug <<
            "takeOffers:         FUNDS=" <<
            lesActive.accountFunds (mTxnAccountID, saTakerGets).getFullText ();
    }

    if (tesSUCCESS != terResult)
    {
        // Fail as is.
    }
    else if (bExpired)
    {
        // nothing to do
    }
    else if (saTakerPays < zero || saTakerGets < zero)
    {
        // If ledger is not final, can vote no.
        // When we are processing an open ledger, failures are local and we charge no fee;
        // otherwise we must claim a fee (even if they do nothing else due to an error)
        // to prevent a DoS.
        terResult   = bOpenLedger ? telFAILED_PROCESSING : tecFAILED_PROCESSING;
    }
    else if (bFillOrKill && (saTakerPays || saTakerGets))
    {
        // Fill or kill and have leftovers.
        lesActive.swapWith (lesCheckpoint); // Restore with just fees paid.
    }
    else if (
        saTakerPays <= zero                                             // Wants nothing more.
        || saTakerGets <= zero                                          // Offering nothing more.
        || bImmediateOrCancel                                           // Do not persist.
        || lesActive.accountFunds (mTxnAccountID, saTakerGets) <= zero  // Not funded.
        || bUnfunded)                                                   // Consider unfunded.
    {
        // Complete as is.
    }
    else if (mPriorBalance.getNValue () < mEngine->getLedger ()->getReserve (sleCreator->getFieldU32 (sfOwnerCount) + 1))
    {
        // If we are here, the signing account had an insufficient reserve
        // *prior* to our processing. We use the prior balance to simplify
        // client writing and make the user experience better.

        if (bOpenLedger) // Ledger is not final, can vote no.
        {
            // Hope for more reserve to come in or more offers to consume. If we
            // specified a local error this transaction will not be retried, so
            // specify a tec to distribute the transaction and allow it to be
            // retried. In particular, it may have been was successful to a
            // degree (partially filled) and if it hasn't, it might succeed.
            terResult   = tecINSUF_RESERVE_OFFER;
        }
        else if (!saPaid && !saGot)
        {
            // Ledger is final, insufficent reserve to create offer, processed
            // nothing.

            terResult   = tecINSUF_RESERVE_OFFER;
        }
        else
        {
            // Ledger is final, insufficent reserve to create offer, processed
            // something.

            // Consider the offer unfunded. Treat as tesSUCCESS.
        }
    }
    else
    {
        // We need to place the remainder of the offer into its order book.
        if (m_journal.debug) m_journal.debug <<
            "offer not fully consumed:" <<
            " saTakerPays=" << saTakerPays.getFullText () <<
            " saTakerGets=" << saTakerGets.getFullText ();

        // Add offer to owner's directory.
        terResult   = lesActive.dirAdd (uOwnerNode,
            Ledger::getOwnerDirIndex (mTxnAccountID), uLedgerIndex,
            std::bind (&Ledger::ownerDirDescriber, std::placeholders::_1,
                       std::placeholders::_2, mTxnAccountID));


        if (tesSUCCESS == terResult)
        {
            // Update owner count.
            lesActive.ownerCountAdjust (mTxnAccountID, 1, sleCreator);

            uint256 uBookBase   = Ledger::getBookBase (
                uPaysCurrency,
                uPaysIssuerID,
                uGetsCurrency,
                uGetsIssuerID);

            if (m_journal.trace) m_journal.trace <<
                "adding to book: " << to_string (uBookBase) <<
                " : " << saTakerPays.getHumanCurrency () <<
                "/" << RippleAddress::createHumanAccountID (saTakerPays.getIssuer ()) <<
                " -> " << saTakerGets.getHumanCurrency () <<
                "/" << RippleAddress::createHumanAccountID (saTakerGets.getIssuer ());

            uDirectory  = Ledger::getQualityIndex (uBookBase, uRate);   // Use original rate.

            // Add offer to order book.
            terResult   = lesActive.dirAdd (uBookNode, uDirectory, uLedgerIndex,
                std::bind (&Ledger::qualityDirDescriber, std::placeholders::_1,
                           std::placeholders::_2, saTakerPays.getCurrency (),
                           uPaysIssuerID, saTakerGets.getCurrency (), uGetsIssuerID,
                           uRate));
        }

        if (tesSUCCESS == terResult)
        {
            if (m_journal.debug)
            {
                m_journal.debug <<
                    "sfAccount=" <<
                    RippleAddress::createHumanAccountID (mTxnAccountID);
                m_journal.debug <<
                    "uPaysIssuerID=" <<
                    RippleAddress::createHumanAccountID (uPaysIssuerID);
                m_journal.debug <<
                    "uGetsIssuerID=" <<
                    RippleAddress::createHumanAccountID (uGetsIssuerID);
                m_journal.debug <<
                    "saTakerPays.isNative()=" <<
                    saTakerPays.isNative ();
                m_journal.debug <<
                    "saTakerGets.isNative()=" <<
                    saTakerGets.isNative ();
                m_journal.debug <<
                    "uPaysCurrency=" <<
                    saTakerPays.getHumanCurrency ();
                m_journal.debug <<
                    "uGetsCurrency=" <<
                    saTakerGets.getHumanCurrency ();
            }

            SLE::pointer sleOffer (mEngine->entryCreate (ltOFFER, uLedgerIndex));

            sleOffer->setFieldAccount (sfAccount, mTxnAccountID);
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

            if (m_journal.debug) m_journal.debug <<
                "final terResult=" << transToken (terResult) <<
                " sleOffer=" << sleOffer->getJson (0);
        }
    }

    // On storing meta data, delete offers that were found unfunded to prevent
    // encountering them in future.
    if (tesSUCCESS == terResult)
    {

        // Go through the list of unfunded offers and remove them
        for (auto const& offerIndex : usOfferUnfundedFound)
        {
            m_journal.trace <<
                "takeOffers: found unfunded: " << to_string (offerIndex);
            lesActive.offerDelete (offerIndex);
        }

        // Go through the list of offers not found and remove them from the
        // order book.
        // DAVIDSCHWARTZ - is this a fix to remove corrupted entries?
        for (auto const& indexes : usMissingOffers)
        {
            SLE::pointer sleDirectory (
                lesActive.entryCache (ltDIR_NODE, indexes.second));

            if (sleDirectory)
            {
                STVector256 svIndexes = sleDirectory->getFieldV256 (sfIndexes);
                std::vector<uint256>& vuiIndexes = svIndexes.peekValue();

                auto it = std::find (
                    vuiIndexes.begin (), vuiIndexes.end (), indexes.first);

                if (it != vuiIndexes.end ())
                {
                    vuiIndexes.erase (it);
                    sleDirectory->setFieldV256 (sfIndexes, svIndexes);
                    lesActive.entryModify (sleDirectory);
                    m_journal.warning <<
                        "takeOffers: offer " << indexes.first <<
                        " removed from directory " << indexes.second;
                }
                else
                {
                    m_journal.debug <<
                        "takeOffers: offer " << indexes.first <<
                        " not found in directory " << indexes.second;
                }
            }
            else
            {
                m_journal.warning <<
                    "takeOffers: directory " << indexes.second <<
                    " not found for offer " << indexes.first;
            }
        }
    }

    if (tesSUCCESS != terResult) m_journal.debug <<
        "final terResult=" << transToken (terResult);

    return terResult;
}

}
