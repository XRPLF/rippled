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

// Make sure an offer is still valid. If not, mark it unfunded.
bool OfferCreateTransactor::isValidOffer (
    SLE::ref            sleOffer,
    const uint160&      uOfferOwnerID,
    const STAmount&     saOfferPays,
    const STAmount&     saOfferGets,
    const uint160&      uTakerAccountID,
    boost::unordered_set<uint256>&  usOfferUnfundedFound,
    boost::unordered_set<uint256>&  usOfferUnfundedBecame,
    boost::unordered_set<uint160>&  usAccountTouched,
    STAmount&           saOfferFunds) const // <--
{
    bool bValid = false;

    if (sleOffer->isFieldPresent (sfExpiration) && sleOffer->getFieldU32 (sfExpiration) <= mEngine->getLedger ()->getParentCloseTimeNC ())
    {
        // Offer is expired. Expired offers are considered unfunded. Delete it.
        m_journal.info << "isValidOffer: encountered expired offer";

        usOfferUnfundedFound.insert (sleOffer->getIndex());

        bValid  = false;
    }
    else if (uOfferOwnerID == uTakerAccountID)
    {
        // Would take own offer. Consider old offer expired. Delete it.
        m_journal.info << "isValidOffer: encountered taker's own old offer";

        usOfferUnfundedFound.insert (sleOffer->getIndex());

        bValid  = false;
    }
    else if (!saOfferGets.isPositive () || !saOfferPays.isPositive ())
    {
        // Offer has bad amounts. Consider offer expired. Delete it.
        m_journal.warning << "isValidOffer: BAD OFFER:" <<
            " saOfferPays=" << saOfferPays <<
            " saOfferGets=" << saOfferGets;

        usOfferUnfundedFound.insert (sleOffer->getIndex());

        bValid = false;
    }
    else
    {
        m_journal.trace << "isValidOffer: saOfferPays=" << saOfferPays.getFullText ();

        saOfferFunds    = mEngine->getNodes ().accountFunds (uOfferOwnerID, saOfferPays);

        if (!saOfferFunds.isPositive ())
        {
            // Offer is unfunded, possibly due to previous balance action.
            m_journal.debug << "isValidOffer: offer unfunded: delete";

            boost::unordered_set<uint160>::iterator account = usAccountTouched.find (uOfferOwnerID);

            if (account != usAccountTouched.end ())
            {
                // Previously touched account.
                usOfferUnfundedBecame.insert (sleOffer->getIndex()); // Delete unfunded offer on success.
            }
            else
            {
                // Never touched source account.
                usOfferUnfundedFound.insert (sleOffer->getIndex());  // Delete found unfunded offer when possible.
            }

            bValid  = false;
        }
        else
        {
            bValid  = true;
        }
    }

    return bValid;
}

// Take as much as possible. Adjusts account balances. Charges fees on top to taker.
// -->    uBookBase: The order book to take against.
// -->  saTakerPays: What the taker offers (w/ issuer)
// -->  saTakerGets: What the taker wanted (w/ issuer)
// <--  saTakerPaid: What taker could have paid including saved not including fees. To reduce an offer.
// <--   saTakerGot: What taker got not including fees. To reduce an offer.
// <--    terResult: tesSUCCESS, terNO_ACCOUNT, telFAILED_PROCESSING, or tecFAILED_PROCESSING
// <--    bUnfunded: if tesSUCCESS, consider offer unfunded after taking.
TER OfferCreateTransactor::takeOffers (
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

    // The order is for the other books currencys for get and pays are opposites.
    // We want the same ratio for the respective currencies.
    // So we swap paid and gets for determing take quality.

    assert (saTakerPays && saTakerGets);

    m_journal.debug << "takeOffers: bSell: " << bSell << ": against book: " << uBookBase.ToString ();

    LedgerEntrySet&         lesActive           = mEngine->getNodes ();
    const std::uint64_t     uTakeQuality        = STAmount::getRate (saTakerGets, saTakerPays);
    STAmount                saTakerRate         = STAmount::setRate (uTakeQuality);
    const uint160           uTakerPaysAccountID = saTakerPays.getIssuer ();
    const uint160           uTakerGetsAccountID = saTakerGets.getIssuer ();
    TER                     terResult           = temUNCERTAIN;

    boost::unordered_set<uint256>   usOfferUnfundedBecame;  // Offers that became unfunded.
    boost::unordered_set<uint160>   usAccountTouched;       // Accounts touched.

    saTakerPaid     = STAmount (saTakerPays.getCurrency (), saTakerPays.getIssuer ());
    saTakerGot      = STAmount (saTakerGets.getCurrency (), saTakerGets.getIssuer ());
    bUnfunded       = false;

    OrderBookIterator bookIterator (lesActive,
        saTakerPays.getCurrency(), saTakerPays.getIssuer(),
        saTakerGets.getCurrency(), saTakerGets.getIssuer());

    while ((temUNCERTAIN == terResult) && bookIterator.nextOffer())
    {
        STAmount        saTakerFunds    = lesActive.accountFunds (uTakerAccountID, saTakerPays);
        STAmount        saSubTakerPays  = saTakerPays - saTakerPaid; // How much more to spend.
        STAmount        saSubTakerGets  = saTakerGets - saTakerGot; // How much more is wanted.
        std::uint64_t          uTipQuality     = bookIterator.getCurrentQuality();

        if (!saTakerFunds.isPositive ())
        {
            // Taker is out of funds. Don't create the offer.
            bUnfunded = true;
            terResult = tesSUCCESS;
        }
        else if (!saSubTakerPays.isPositive() || !saSubTakerGets.isPositive())
        {
            // Offer is completely consumed
            terResult = tesSUCCESS;
        }
        else if ((uTakeQuality < uTipQuality)
                 || (bPassive && uTakeQuality == uTipQuality))
        {
            // Offer does not cross this offer
            STAmount    saTipRate           = STAmount::setRate (uTipQuality);

            m_journal.debug << "takeOffers: done:" <<
                " uTakeQuality=" << uTakeQuality <<
                " " << get_compare_sign (uTakeQuality, uTipQuality) <<
                " uTipQuality=" << uTipQuality <<
                " saTakerRate=" << saTakerRate <<
                " " << get_compare_sign (saTakerRate, saTipRate) <<
                " saTipRate=" << saTakerRate <<
                " bPassive=" << bPassive;

            terResult   = tesSUCCESS;
        }
        else
        {
            // We have a crossing offer to consider.

            SLE::pointer    sleOffer        = bookIterator.getCurrentOffer ();

            if (!sleOffer)
            { // offer is in directory but not in ledger
                uint256 offerIndex = bookIterator.getCurrentIndex ();
                m_journal.warning << "takeOffers: offer not found : " << offerIndex;
                usMissingOffers.insert (missingOffer_t (
                    bookIterator.getCurrentIndex (), bookIterator.getCurrentDirectory ()));
            }
            else
            {
                m_journal.debug << "takeOffers: considering offer : " << sleOffer->getJson (0);

                const uint160&  uOfferOwnerID   = sleOffer->getFieldAccount160 (sfAccount);
                STAmount        saOfferPays     = sleOffer->getFieldAmount (sfTakerGets);
                STAmount        saOfferGets     = sleOffer->getFieldAmount (sfTakerPays);

                STAmount        saOfferFunds;   // Funds of offer owner to payout.
                bool            bValid;

                bValid  =  isValidOffer (
                               sleOffer, uOfferOwnerID, saOfferPays, saOfferGets,
                               uTakerAccountID,
                               usOfferUnfundedFound, usOfferUnfundedBecame, usAccountTouched,
                               saOfferFunds);

                if (bValid)
                {
                    STAmount    saSubTakerPaid;
                    STAmount    saSubTakerGot;
                    STAmount    saTakerIssuerFee;
                    STAmount    saOfferIssuerFee;
                    STAmount    saOfferRate = STAmount::setRate (uTipQuality);

                    m_journal.trace << "takeOffers: applyOffer:    saTakerPays: " << saTakerPays.getFullText ();
                    m_journal.trace << "takeOffers: applyOffer:    saTakerPaid: " << saTakerPaid.getFullText ();
                    m_journal.trace << "takeOffers: applyOffer:   saTakerFunds: " << saTakerFunds.getFullText ();
                    m_journal.trace << "takeOffers: applyOffer:   saOfferFunds: " << saOfferFunds.getFullText ();
                    m_journal.trace << "takeOffers: applyOffer:    saOfferPays: " << saOfferPays.getFullText ();
                    m_journal.trace << "takeOffers: applyOffer:    saOfferGets: " << saOfferGets.getFullText ();
                    m_journal.trace << "takeOffers: applyOffer:    saOfferRate: " << saOfferRate.getFullText ();
                    m_journal.trace << "takeOffers: applyOffer: saSubTakerPays: " << saSubTakerPays.getFullText ();
                    m_journal.trace << "takeOffers: applyOffer: saSubTakerGets: " << saSubTakerGets.getFullText ();
                    m_journal.trace << "takeOffers: applyOffer:    saTakerPays: " << saTakerPays.getFullText ();
                    m_journal.trace << "takeOffers: applyOffer:    saTakerGets: " << saTakerGets.getFullText ();

                    bool    bOfferDelete    = STAmount::applyOffer (
                                                  bSell,
                                                  lesActive.rippleTransferRate (uTakerAccountID, uOfferOwnerID, uTakerPaysAccountID),
                                                  lesActive.rippleTransferRate (uOfferOwnerID, uTakerAccountID, uTakerGetsAccountID),
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

                    m_journal.debug << "takeOffers: applyOffer: saSubTakerPaid: " << saSubTakerPaid.getFullText ();
                    m_journal.debug << "takeOffers: applyOffer:  saSubTakerGot: " << saSubTakerGot.getFullText ();

                    // Adjust offer

                    // Offer owner will pay less.  Subtract what taker just got.
                    sleOffer->setFieldAmount (sfTakerGets, saOfferPays -= saSubTakerGot);

                    // Offer owner will get less.  Subtract what owner just paid.
                    sleOffer->setFieldAmount (sfTakerPays, saOfferGets -= saSubTakerPaid);

                    mEngine->entryModify (sleOffer);

                    if (bOfferDelete)
                    {
                        // Offer now fully claimed or now unfunded.
                        m_journal.debug << "takeOffers: Offer claimed: Delete.";

                        usOfferUnfundedBecame.insert (sleOffer->getIndex()); // Delete unfunded offer on success.

                        // Offer owner's account is no longer pristine.
                        usAccountTouched.insert (uOfferOwnerID);
                    }
                    else if (saSubTakerGot)
                    {
                        m_journal.debug << "takeOffers: Offer partial claim.";

                        if (!saOfferPays.isPositive () || !saOfferGets.isPositive ())
                        {
                            m_journal.warning << "takeOffers: ILLEGAL OFFER RESULT.";
                            bUnfunded   = true;
                            terResult   = bOpenLedger ? telFAILED_PROCESSING : tecFAILED_PROCESSING;
                        }
                    }
                    else
                    {
                        // Taker got nothing, probably due to rounding. Consider taker unfunded.
                        m_journal.debug << "takeOffers: No claim.";

                        bUnfunded   = true;
                        terResult   = tesSUCCESS;                   // Done.
                    }

                    assert (uTakerGetsAccountID == saSubTakerGot.getIssuer ());
                    assert (uTakerPaysAccountID == saSubTakerPaid.getIssuer ());

                    if (!bUnfunded)
                    {
                        // Distribute funds. The sends charge appropriate fees which are implied by offer.

                        terResult   = lesActive.accountSend (uOfferOwnerID, uTakerAccountID, saSubTakerGot);            // Offer owner pays taker.

                        if (tesSUCCESS == terResult)
                            terResult   = lesActive.accountSend (uTakerAccountID, uOfferOwnerID, saSubTakerPaid);           // Taker pays offer owner.

                        if (bSell)
                        {
                            // Sell semantics:
                            // Reduce amount considered received to original offer's rate.
                            // Not by crossing rate, which is higher.
                            STAmount saEffectiveGot = STAmount::divide(saSubTakerPaid, saTakerRate, saTakerGets);
                            saSubTakerGot = std::min(saEffectiveGot, saSubTakerGot);
                        }
                        else
                        {
                            // Buy semantics: Reduce amount considered paid by taker's rate. Not by actual cost which is lower.
                            // That is, take less as to just satify our buy requirement.
                            STAmount    saTakerCould    = saTakerPays - saTakerPaid;    // Taker could pay.

                            if (saTakerFunds < saTakerCould)
                                saTakerCould    = saTakerFunds;

                            STAmount    saTakerUsed = STAmount::multiply (saSubTakerGot, saTakerRate, saTakerPays);

                            m_journal.debug << "takeOffers: applyOffer:   saTakerCould: " << saTakerCould.getFullText ();
                            m_journal.debug << "takeOffers: applyOffer:  saSubTakerGot: " << saSubTakerGot.getFullText ();
                            m_journal.debug << "takeOffers: applyOffer:    saTakerRate: " << saTakerRate.getFullText ();
                            m_journal.debug << "takeOffers: applyOffer:    saTakerUsed: " << saTakerUsed.getFullText ();

                            saSubTakerPaid  = std::min (saTakerCould, saTakerUsed);
                        }

                        saTakerPaid     += saSubTakerPaid;
                        saTakerGot      += saSubTakerGot;

                        if (tesSUCCESS == terResult)
                            terResult   = temUNCERTAIN;
                    }
                }
            }
        }
    }

    if (temUNCERTAIN == terResult)
        terResult = tesSUCCESS;

    m_journal.debug << "takeOffers: " << transToken (terResult);

    if (tesSUCCESS == terResult)
    {
        // On success, delete offers that became unfunded.
        BOOST_FOREACH (uint256 const & uOfferIndex, usOfferUnfundedBecame)
        {
            m_journal.debug << "takeOffers: became unfunded: " << uOfferIndex.ToString ();

            lesActive.offerDelete (uOfferIndex);
        }
    }

    m_journal.debug << "takeOffers< " << transToken (terResult);

    return terResult;
}

TER OfferCreateTransactor::doApply ()
{
    m_journal.trace << "OfferCreate> " << mTxn.getJson (0);
    const std::uint32_t            uTxFlags                = mTxn.getFlags ();
    const bool                bPassive                = isSetBit (uTxFlags, tfPassive);
    const bool                bImmediateOrCancel        = isSetBit (uTxFlags, tfImmediateOrCancel);
    const bool                bFillOrKill                = isSetBit (uTxFlags, tfFillOrKill);
    const bool                bSell                    = isSetBit (uTxFlags, tfSell);
    STAmount                saTakerPays                = mTxn.getFieldAmount (sfTakerPays);
    STAmount                saTakerGets                = mTxn.getFieldAmount (sfTakerGets);

    if (!saTakerPays.isLegalNet () || !saTakerGets.isLegalNet ())
        return temBAD_AMOUNT;

    m_journal.trace << 
        "saTakerPays=" << saTakerPays.getFullText () <<
        " saTakerGets=" << saTakerGets.getFullText ();

    const uint160            uPaysIssuerID            = saTakerPays.getIssuer ();
    const uint160            uGetsIssuerID            = saTakerGets.getIssuer ();
    const std::uint32_t            uExpiration                = mTxn.getFieldU32 (sfExpiration);
    const bool                bHaveExpiration            = mTxn.isFieldPresent (sfExpiration);
    const bool                bHaveCancel                = mTxn.isFieldPresent (sfOfferSequence);
    const std::uint32_t            uCancelSequence            = mTxn.getFieldU32 (sfOfferSequence);
    const std::uint32_t            uAccountSequenceNext    = mTxnAccount->getFieldU32 (sfSequence);
    const std::uint32_t            uSequence                = mTxn.getSequence ();

    const uint256            uLedgerIndex            = Ledger::getOfferIndex (mTxnAccountID, uSequence);

    m_journal.trace << "Creating offer node: " << uLedgerIndex.ToString () << " uSequence=" << uSequence;

    const uint160            uPaysCurrency            = saTakerPays.getCurrency ();
    const uint160            uGetsCurrency            = saTakerGets.getCurrency ();
    const std::uint64_t            uRate                    = STAmount::getRate (saTakerGets, saTakerPays);

    TER                        terResult                = tesSUCCESS;
    uint256                    uDirectory;        // Delete hints.
    std::uint64_t                    uOwnerNode;
    std::uint64_t                    uBookNode;

    LedgerEntrySet&            lesActive                = mEngine->getNodes ();
    LedgerEntrySet            lesCheckpoint            = lesActive;                            // Checkpoint with just fees paid.
    lesActive.bumpSeq ();                                                                    // Begin ledger variance.

    SLE::pointer            sleCreator                = mEngine->entryCache (ltACCOUNT_ROOT, Ledger::getAccountRootIndex (mTxnAccountID));

    if (uTxFlags & tfOfferCreateMask)
    {
        m_journal.info << "Malformed transaction: Invalid flags set.";

        return temINVALID_FLAG;
    }
    else if (bImmediateOrCancel && bFillOrKill)
    {
        m_journal.info << "Malformed transaction: both IoC and FoK set.";

        return temINVALID_FLAG;
    }
    else if (bHaveExpiration && !uExpiration)
    {
        m_journal.warning << "Malformed offer: bad expiration";

        terResult   = temBAD_EXPIRATION;
    }
    else if (saTakerPays.isNative () && saTakerGets.isNative ())
    {
        m_journal.warning << "Malformed offer: XRP for XRP";

        terResult   = temBAD_OFFER;
    }
    else if (!saTakerPays.isPositive () || !saTakerGets.isPositive ())
    {
        m_journal.warning << "Malformed offer: bad amount";

        terResult   = temBAD_OFFER;
    }
    else if (uPaysCurrency == uGetsCurrency && uPaysIssuerID == uGetsIssuerID)
    {
        m_journal.warning << "Malformed offer: redundant offer";

        terResult   = temREDUNDANT;
    }
    else if (CURRENCY_BAD == uPaysCurrency || CURRENCY_BAD == uGetsCurrency)
    {
        m_journal.warning << "Malformed offer: Bad currency.";

        terResult   = temBAD_CURRENCY;
    }
    else if (saTakerPays.isNative () != !uPaysIssuerID || saTakerGets.isNative () != !uGetsIssuerID)
    {
        m_journal.warning << "Malformed offer: bad issuer";

        terResult   = temBAD_ISSUER;
    }
    else if (!lesActive.accountFunds (mTxnAccountID, saTakerGets).isPositive ())
    {
        m_journal.warning << "delay: Offers must be at least partially funded.";

        terResult   = tecUNFUNDED_OFFER;
    }
    else if (bHaveCancel && (!uCancelSequence || uAccountSequenceNext - 1 <= uCancelSequence))
    {
        m_journal.info << "uAccountSequenceNext=" << uAccountSequenceNext << " uOfferSequence=" << uCancelSequence;

        terResult   = temBAD_SEQUENCE;
    }

    // Cancel offer.
    if ((tesSUCCESS == terResult) && bHaveCancel)
    {
        const uint256   uCancelIndex = Ledger::getOfferIndex (mTxnAccountID, uCancelSequence);
        SLE::pointer    sleCancel    = mEngine->entryCache (ltOFFER, uCancelIndex);

        if (sleCancel)
        {
            m_journal.warning << "uCancelSequence=" << uCancelSequence;

            terResult   = mEngine->getNodes ().offerDelete (sleCancel);
        }
        else
        {
            m_journal.warning << "offer not found: "
                    << RippleAddress::createHumanAccountID (mTxnAccountID)
                    << " : " << uCancelSequence
                    << " : " << uCancelIndex.ToString ();
        }
    }

    // Make sure authorized to hold what taker will pay.
    bool bExpired = (bHaveExpiration && mEngine->getLedger ()->getParentCloseTimeNC () >= uExpiration);
    if (tesSUCCESS == terResult && !saTakerPays.isNative () && !bExpired)
    {
        SLE::pointer        sleTakerPays    = mEngine->entryCache (ltACCOUNT_ROOT, Ledger::getAccountRootIndex (uPaysIssuerID));

        if (!sleTakerPays)
        {
            m_journal.warning << "delay: can't receive IOUs from non-existent issuer: " << RippleAddress::createHumanAccountID (uPaysIssuerID);

            terResult   = isSetBit (mParams, tapRETRY) ? terNO_ACCOUNT : tecNO_ISSUER;
        }
        else if (isSetBit (sleTakerPays->getFieldU32 (sfFlags), lsfRequireAuth))
        {
            SLE::pointer    sleRippleState  = mEngine->entryCache (ltRIPPLE_STATE, Ledger::getRippleStateIndex (mTxnAccountID, uPaysIssuerID, uPaysCurrency));
            bool            bHigh           = mTxnAccountID > uPaysIssuerID;

            if (!sleRippleState)
            {
                terResult   = isSetBit (mParams, tapRETRY) ? terNO_LINE : tecNO_LINE;
            }
            else if (!isSetBit (sleRippleState->getFieldU32 (sfFlags), (bHigh ? lsfHighAuth : lsfLowAuth)))
            {
                m_journal.debug << "delay: can't receive IOUs from issuer without auth.";

                terResult   = isSetBit (mParams, tapRETRY) ? terNO_AUTH : tecNO_AUTH;
            }
        }
    }

    STAmount        saPaid;
    STAmount        saGot;
    bool            bUnfunded   = false;
    const bool      bOpenLedger = isSetBit (mParams, tapOPEN_LEDGER);

    if ((tesSUCCESS == terResult) && !bExpired)
    {
        const uint256   uTakeBookBase   = Ledger::getBookBase (uGetsCurrency, uGetsIssuerID, uPaysCurrency, uPaysIssuerID);

        m_journal.info <<
            "take against book:" << uTakeBookBase.ToString () << 
            " for " << saTakerGets.getFullText () << 
            " -> " << saTakerPays.getFullText ();

        // Take using the parameters of the offer.
        m_journal.debug << 
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

        m_journal.debug << "takeOffers=" << terResult;
        m_journal.debug << "takeOffers: saPaid=" << saPaid.getFullText ();
        m_journal.debug << "takeOffers:  saGot=" << saGot.getFullText ();

        if (tesSUCCESS == terResult && !bUnfunded)
        {
            saTakerPays     -= saGot;               // Reduce pay in from takers by what offer just got.
            saTakerGets     -= saPaid;              // Reduce pay out to takers by what srcAccount just paid.

            m_journal.debug <<
                "takeOffers: AFTER saTakerPays=" << saTakerPays.getFullText ();
            m_journal.debug <<
                "takeOffers: AFTER saTakerGets=" << saTakerGets.getFullText ();
        }
    }

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

    if (tesSUCCESS != terResult)
    {
        // Fail as is.
        nothing ();
    }
    else if (bExpired)
    {
        // nothing to do
        nothing ();
    }
    else if (saTakerPays.isNegative () || saTakerGets.isNegative ())
    {
        // If ledger is not final, can vote no.
        terResult   = bOpenLedger ? telFAILED_PROCESSING : tecFAILED_PROCESSING;
    }
    else if (bFillOrKill && (saTakerPays || saTakerGets))
    {
        // Fill or kill and have leftovers.
        lesActive.swapWith (lesCheckpoint);                                 // Restore with just fees paid.
    }
    else if (
        !saTakerPays.isPositive()                                           // Wants nothing more.
        || !saTakerGets.isPositive()                                        // Offering nothing more.
        || bImmediateOrCancel                                               // Do not persist.
        || !lesActive.accountFunds (mTxnAccountID, saTakerGets).isPositive () // Not funded.
        || bUnfunded)                                                       // Consider unfunded.
    {
        // Complete as is.
        nothing ();
    }
    else if (mPriorBalance.getNValue () < mEngine->getLedger ()->getReserve (sleCreator->getFieldU32 (sfOwnerCount) + 1))
    {
        if (bOpenLedger) // Ledger is not final, can vote no.
        {
            // Hope for more reserve to come in or more offers to consume.
            terResult   = tecINSUF_RESERVE_OFFER;
        }
        else if (!saPaid && !saGot)
        {
            // Ledger is final, insufficent reserve to create offer, processed nothing.

            terResult   = tecINSUF_RESERVE_OFFER;
        }
        else
        {
            // Ledger is final, insufficent reserve to create offer, processed something.

            // Consider the offer unfunded. Treat as tesSUCCESS.
            nothing ();
        }
    }
    else
    {
        // We need to place the remainder of the offer into its order book.
        m_journal.info << "offer not fully consumed:"
            " saTakerPays=" << saTakerPays.getFullText () <<
            " saTakerGets=" << saTakerGets.getFullText ();

        // Add offer to owner's directory.
        terResult   = lesActive.dirAdd (uOwnerNode, Ledger::getOwnerDirIndex (mTxnAccountID), uLedgerIndex,
                                        BIND_TYPE (&Ledger::ownerDirDescriber, P_1, P_2, mTxnAccountID));


        if (tesSUCCESS == terResult)
        {
            lesActive.ownerCountAdjust (mTxnAccountID, 1, sleCreator); // Update owner count.

            uint256 uBookBase   = Ledger::getBookBase (uPaysCurrency, uPaysIssuerID, uGetsCurrency, uGetsIssuerID);

            m_journal.info << 
                "adding to book: " << uBookBase.ToString () <<
                " : " << saTakerPays.getHumanCurrency () <<
                "/" << RippleAddress::createHumanAccountID (saTakerPays.getIssuer ()) <<
                " -> " << saTakerGets.getHumanCurrency () <<
                "/" << RippleAddress::createHumanAccountID (saTakerGets.getIssuer ());

            uDirectory  = Ledger::getQualityIndex (uBookBase, uRate);   // Use original rate.

            // Add offer to order book.
            terResult   = lesActive.dirAdd (uBookNode, uDirectory, uLedgerIndex,
                                            BIND_TYPE (&Ledger::qualityDirDescriber, P_1, P_2, 
                                                    saTakerPays.getCurrency (), uPaysIssuerID,
                                                    saTakerGets.getCurrency (), uGetsIssuerID, uRate));
        }

        if (tesSUCCESS == terResult)
        {
            m_journal.debug << 
                "sfAccount=" << RippleAddress::createHumanAccountID (mTxnAccountID);
            m_journal.debug <<
                "uPaysIssuerID=" << RippleAddress::createHumanAccountID (uPaysIssuerID);
            m_journal.debug <<
                "uGetsIssuerID=" << RippleAddress::createHumanAccountID (uGetsIssuerID);
            m_journal.trace <<
                "saTakerPays.isNative()=" << saTakerPays.isNative ();
            m_journal.trace <<
                "saTakerGets.isNative()=" << saTakerGets.isNative ();
            m_journal.debug <<
                "uPaysCurrency=" << saTakerPays.getHumanCurrency ();
            m_journal.debug <<
                "uGetsCurrency=" << saTakerGets.getHumanCurrency ();

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

            m_journal.trace << 
                "final terResult=" << transToken (terResult) <<
                " sleOffer=" << sleOffer->getJson (0);
        }
    }

    // On storing meta data, delete offers that were found unfunded to prevent encountering them in future.
    if (tesSUCCESS == terResult)
    {

        // Go through the list of unfunded offers and remove them
        BOOST_FOREACH (uint256 const & uOfferIndex, usOfferUnfundedFound)
        {
            m_journal.info << 
                "takeOffers: found unfunded: " << uOfferIndex.ToString ();

            lesActive.offerDelete (uOfferIndex);

        }

        // Go through the list of offers not found and remove them from the order book
        BOOST_FOREACH (missingOffer_t const& indexes, usMissingOffers)
        {
            SLE::pointer sleDirectory = lesActive.entryCache (ltDIR_NODE, indexes.second);
            if (sleDirectory)
            {
                STVector256 svIndexes = sleDirectory->getFieldV256 (sfIndexes);
                std::vector<uint256>& vuiIndexes = svIndexes.peekValue();
                std::vector<uint256>::iterator it = std::find (vuiIndexes.begin(), vuiIndexes.end (), indexes.first);
                if (it != vuiIndexes.end ())
                {
                    vuiIndexes.erase (it);
                    sleDirectory->setFieldV256 (sfIndexes, svIndexes);
                    lesActive.entryModify (sleDirectory);
                    m_journal.warning << "takeOffers: offer " << indexes.first <<
                        " removed from directory " << indexes.second;
                }
                else
                {
                    m_journal.info << "takeOffers: offer " << indexes.first <<
                        " not found in directory " << indexes.second;
                }
            }
            else
            {
                m_journal.warning << "takeOffers: directory " << indexes.second <<
                    " not found for offer " << indexes.first;
            }
        }
    }

    if (tesSUCCESS != terResult) m_journal.info << 
        "final terResult=" << transToken (terResult);

    return terResult;
}

}

