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

#include <ripple_app/paths/Calculators.h>
#include <ripple_app/paths/RippleCalc.h>
#include <ripple_app/paths/Tuning.h>

namespace ripple {

// OPTIMIZE: When calculating path increment, note if increment consumes all
// liquidity. No need to revisit path in the future if all liquidity is used.
//


 // VFALCO TODO Update the comment for this function, the argument list no
//             resembles the comment
//
//             Provide a better explanation for what this function does.

// If needed, advance to next funded offer.
// - Automatically advances to first offer.
// --> bEntryAdvance: true, to advance to next entry. false, recalculate.
// <-- uOfferIndex : 0=end of list.
TER calcNodeAdvance (
    RippleCalc& rippleCalc,
    const unsigned int          nodeIndex,
    PathState&                  pathState,
    const bool                  bMultiQuality,
    const bool                  bReverse)
{
    auto& previousNode = pathState.vpnNodes[nodeIndex - 1];
    auto& node = pathState.vpnNodes[nodeIndex];

    uint256&        uDirectTip      = node.uDirectTip;
    uint256&        uDirectEnd      = node.uDirectEnd;
    bool&           bDirectAdvance  = node.bDirectAdvance;
    bool&           bDirectRestart  = node.bDirectRestart;
    SLE::pointer&   sleDirectDir    = node.sleDirectDir;
    STAmount&       saOfrRate       = node.saOfrRate;

    bool&           bEntryAdvance   = node.bEntryAdvance;
    unsigned int&   uEntry          = node.uEntry;
    uint256&        uOfferIndex     = node.uOfferIndex;
    SLE::pointer&   sleOffer        = node.sleOffer;
    uint160&        uOfrOwnerID     = node.uOfrOwnerID;
    STAmount&       saOfferFunds    = node.saOfferFunds;
    STAmount&       saTakerPays     = node.saTakerPays;
    STAmount&       saTakerGets     = node.saTakerGets;
    bool&           bFundsDirty     = node.bFundsDirty;

    TER             errorCode       = tesSUCCESS;

    WriteLog (lsTRACE, RippleCalc)
            << "calcNodeAdvance: TakerPays:"
            << saTakerPays << " TakerGets:" << saTakerGets;

    int loopCount = 0;

    do
    {
        // VFALCO NOTE Why not use a for() loop?
        // VFALCO TODO The limit on loop iterations puts an
        //             upper limit on the number of different quality
        // levels (ratio of pay:get) that will be considered for one path.
        // Changing this value has repercusssions on validation and consensus.
        //
        if (++loopCount > NODE_ADVANCE_MAX_LOOPS)
        {
            WriteLog (lsWARNING, RippleCalc) << "Loop count exceeded";
            return tefEXCEPTION;
        }

        bool    bDirectDirDirty = false;

        if (!uDirectTip)
        {
            // Need to initialize current node.

            uDirectTip = Ledger::getBookBase (
                previousNode.uCurrencyID, previousNode.uIssuerID,
                node.uCurrencyID,
                node.uIssuerID);
            uDirectEnd      = Ledger::getQualityNext (uDirectTip);

            sleDirectDir    = rippleCalc.mActiveLedger.entryCache (ltDIR_NODE, uDirectTip);

            // Associated vars are dirty, if found it.
            bDirectDirDirty = !!sleDirectDir;

            // Advance, if didn't find it. Normal not to be unable to lookup
            // firstdirectory. Maybe even skip this lookup.
            bDirectAdvance  = !sleDirectDir;
            bDirectRestart  = false;

            WriteLog (lsTRACE, RippleCalc)
                << "calcNodeAdvance: Initialize node:"
                << " uDirectTip=" << uDirectTip
                <<" uDirectEnd=" << uDirectEnd
                << " bDirectAdvance=" << bDirectAdvance;
        }

        if (bDirectAdvance || bDirectRestart)
        {
            // Get next quality.
            if (bDirectAdvance)
            {
                uDirectTip  = rippleCalc.mActiveLedger.getNextLedgerIndex (
                    uDirectTip, uDirectEnd);
            }

            bDirectDirDirty = true;
            bDirectAdvance  = false;
            bDirectRestart  = false;

            if (!!uDirectTip)
            {
                // Have another quality directory.
                WriteLog (lsTRACE, RippleCalc)
                    << "calcNodeAdvance: Quality advance: uDirectTip="
                    << uDirectTip;

                sleDirectDir = rippleCalc.mActiveLedger.entryCache (ltDIR_NODE, uDirectTip);
            }
            else if (bReverse)
            {
                WriteLog (lsTRACE, RippleCalc)
                    << "calcNodeAdvance: No more offers.";

                uOfferIndex = 0;
                break;
            }
            else
            {
                // No more offers. Should be done rather than fall off end of
                // book.
                WriteLog (lsWARNING, RippleCalc)
                    << "calcNodeAdvance: Unreachable: "
                    << "Fell off end of order book.";
                // FIXME: why?
                return rippleCalc.mOpenLedger ? telFAILED_PROCESSING :
                    tecFAILED_PROCESSING;
            }
        }

        if (bDirectDirDirty)
        {
            saOfrRate = STAmount::setRate (Ledger::getQuality (uDirectTip));
            // For correct ratio
            uEntry          = 0;
            bEntryAdvance   = true;

            WriteLog (lsTRACE, RippleCalc)
                << "calcNodeAdvance: directory dirty: saOfrRate="
                << saOfrRate;
        }

        if (!bEntryAdvance)
        {
            if (bFundsDirty)
            {
                // We were called again probably merely to update structure
                // variables.
                saTakerPays = sleOffer->getFieldAmount (sfTakerPays);
                saTakerGets = sleOffer->getFieldAmount (sfTakerGets);

                saOfferFunds = rippleCalc.mActiveLedger.accountFunds (
                    uOfrOwnerID, saTakerGets);
                // Funds left.
                bFundsDirty     = false;

                WriteLog (lsTRACE, RippleCalc)
                    << "calcNodeAdvance: funds dirty: saOfrRate="
                    << saOfrRate;
            }
            else
            {
                WriteLog (lsTRACE, RippleCalc) << "calcNodeAdvance: as is";
            }
        }
        else if (!rippleCalc.mActiveLedger.dirNext (
            uDirectTip, sleDirectDir, uEntry, uOfferIndex))
        {
            // Failed to find an entry in directory.
            // Do another cur directory iff bMultiQuality
            if (bMultiQuality)
            {
                // We are allowed to process multiple qualities if this is the
                // only path.
                WriteLog (lsTRACE, RippleCalc)
                    << "calcNodeAdvance: next quality";
                bDirectAdvance  = true;         // Process next quality.
            }
            else if (!bReverse)
            {
                WriteLog (lsWARNING, RippleCalc)
                    << "calcNodeAdvance: unreachable: ran out of offers";
                return rippleCalc.mOpenLedger ? telFAILED_PROCESSING :
                    tecFAILED_PROCESSING;
                // TEMPORARY
            }
            else
            {
                // Ran off end of offers.
                bEntryAdvance   = false;        // Done.
                uOfferIndex     = 0;            // Report nore more entries.
            }
        }
        else
        {
            // Got a new offer.
            sleOffer = rippleCalc.mActiveLedger.entryCache (ltOFFER, uOfferIndex);

            if (!sleOffer)
            {
                WriteLog (lsWARNING, RippleCalc) <<
                    "Missing offer in directory";
                bEntryAdvance = true;
            }
            else
            {
                uOfrOwnerID = sleOffer->getFieldAccount160 (sfAccount);
                saTakerPays = sleOffer->getFieldAmount (sfTakerPays);
                saTakerGets = sleOffer->getFieldAmount (sfTakerGets);

                const AccountCurrencyIssuer asLine (
                    uOfrOwnerID, node.uCurrencyID, node.uIssuerID);

                WriteLog (lsTRACE, RippleCalc)
                    << "calcNodeAdvance: uOfrOwnerID="
                    << RippleAddress::createHumanAccountID (uOfrOwnerID)
                    << " saTakerPays=" << saTakerPays
                    << " saTakerGets=" << saTakerGets
                    << " uOfferIndex=" << uOfferIndex;

                if (sleOffer->isFieldPresent (sfExpiration) &&
                    (sleOffer->getFieldU32 (sfExpiration) <=
                     rippleCalc.mActiveLedger.getLedger ()->getParentCloseTimeNC ()))
                {
                    // Offer is expired.
                    WriteLog (lsTRACE, RippleCalc)
                        << "calcNodeAdvance: expired offer";
                    rippleCalc.mUnfundedOffers.insert(uOfferIndex);
                    continue;
                }
                else if (saTakerPays <= zero || saTakerGets <= zero)
                {
                    // Offer has bad amounts. Offers should never have a bad
                    // amounts.

                    if (bReverse)
                    {
                        // Past internal error, offer had bad amounts.
                        WriteLog (lsWARNING, RippleCalc)
                            << "calcNodeAdvance: PAST INTERNAL ERROR:"
                            << " OFFER NON-POSITIVE:"
                            << " saTakerPays=" << saTakerPays
                            << " saTakerGets=%s" << saTakerGets;

                        // Mark offer for always deletion.
                        rippleCalc.mUnfundedOffers.insert (uOfferIndex);
                    }
                    else if (rippleCalc.mUnfundedOffers.find (uOfferIndex) !=
                             rippleCalc.mUnfundedOffers.end ())
                    {
                        // Past internal error, offer was found failed to place
                        // this in mUnfundedOffers.
                        // Just skip it. It will be deleted.
                        WriteLog (lsDEBUG, RippleCalc)
                            << "calcNodeAdvance: PAST INTERNAL ERROR:"
                            << " OFFER NON-POSITIVE:"
                            << " saTakerPays=" << saTakerPays
                            << " saTakerGets=" << saTakerGets;

                    }
                    else
                    {
                        // Reverse should have previously put bad offer in list.
                        // An internal error previously left a bad offer.
                        WriteLog (lsWARNING, RippleCalc)
                            << "calcNodeAdvance: INTERNAL ERROR:"
                            <<" OFFER NON-POSITIVE:"
                            << " saTakerPays=" << saTakerPays
                            << " saTakerGets=" << saTakerGets;

                        // Don't process at all, things are in an unexpected
                        // state for this transactions.
                        errorCode = tefEXCEPTION;
                    }

                    continue;
                }

                // Allowed to access source from this node?
                //
                // XXX This can get called multiple times for same source in a
                // row, caching result would be nice.
                //
                // XXX Going forward could we fund something with a worse
                // quality which was previously skipped? Might need to check
                // quality.
                auto itForward = pathState.umForward.find (asLine);
                const bool bFoundForward = itForward != pathState.umForward.end ();

                // Only allow a source to be used once, in the first node
                // encountered from initial path scan.  This prevents
                // conflicting uses of the same balance when going reverse vs
                // forward.
                if (bFoundForward &&
                    itForward->second != nodeIndex &&
                    uOfrOwnerID != node.uIssuerID)
                {
                    // Temporarily unfunded. Another node uses this source,
                    // ignore in this offer.
                    WriteLog (lsTRACE, RippleCalc)
                        << "calcNodeAdvance: temporarily unfunded offer"
                        << " (forward)";
                    continue;
                }

                // This is overly strict. For contributions to past. We should
                // only count source if actually used.
                auto itReverse = pathState.umReverse.find (asLine);
                bool bFoundReverse = itReverse != pathState.umReverse.end ();

                // For this quality increment, only allow a source to be used
                // from a single node, in the first node encountered from
                // applying offers in reverse.
                if (bFoundReverse &&
                    itReverse->second != nodeIndex &&
                    uOfrOwnerID != node.uIssuerID)
                {
                    // Temporarily unfunded. Another node uses this source,
                    // ignore in this offer.
                    WriteLog (lsTRACE, RippleCalc)
                        << "calcNodeAdvance: temporarily unfunded offer"
                        <<" (reverse)";
                    continue;
                }

                // Determine if used in past.
                // We only need to know if it might need to be marked unfunded.
                auto itPast = rippleCalc.mumSource.find (asLine);
                bool bFoundPast = (itPast != rippleCalc.mumSource.end ());

                // Only the current node is allowed to use the source.

                saOfferFunds = rippleCalc.mActiveLedger.accountFunds
                        (uOfrOwnerID, saTakerGets); // Funds held.

                if (saOfferFunds <= zero)
                {
                    // Offer is unfunded.
                    WriteLog (lsTRACE, RippleCalc)
                        << "calcNodeAdvance: unfunded offer";

                    if (bReverse && !bFoundReverse && !bFoundPast)
                    {
                        // Never mentioned before, clearly just: found unfunded.
                        // That is, even if this offer fails due to fill or kill
                        // still do deletions.
                        // Mark offer for always deletion.
                        rippleCalc.mUnfundedOffers.insert (uOfferIndex);
                    }
                    else
                    {
                        // Moving forward, don't need to insert again
                        // Or, already found it.
                    }

                    // YYY Could verify offer is correct place for unfundeds.
                    continue;
                }

                if (bReverse            // Need to remember reverse mention.
                    && !bFoundPast      // Not mentioned in previous passes.
                    && !bFoundReverse)  // New to pass.
                {
                    // Consider source mentioned by current path state.
                    WriteLog (lsTRACE, RippleCalc)
                        << "calcNodeAdvance: remember="
                        <<  RippleAddress::createHumanAccountID (uOfrOwnerID)
                        << "/"
                        << STAmount::createHumanCurrency (node.uCurrencyID)
                        << "/"
                        << RippleAddress::createHumanAccountID (node.uIssuerID);

                    pathState.umReverse.insert (std::make_pair (asLine, nodeIndex));
                }

                bFundsDirty     = false;
                bEntryAdvance   = false;
            }
        }
    }
    while (errorCode == tesSUCCESS && (bEntryAdvance || bDirectAdvance));

    if (errorCode == tesSUCCESS)
    {
        WriteLog (lsTRACE, RippleCalc)
            << "calcNodeAdvance: uOfferIndex=" << uOfferIndex;
    }
    else
    {
        WriteLog (lsDEBUG, RippleCalc)
            << "calcNodeAdvance: errorCode=" << transToken (errorCode);
    }

    return errorCode;
}

}  // ripple
