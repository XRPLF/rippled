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

#include <ripple/module/app/paths/Calculators.h>
#include <ripple/module/app/paths/RippleCalc.h>
#include <ripple/module/app/paths/Tuning.h>

namespace ripple {
namespace path {

// OPTIMIZE: When calculating path increment, note if increment consumes all
// liquidity. No need to revisit path in the future if all liquidity is used.
//
// nodeAdvance advances through offers in an order book.
// If needed, advance to next funded offer.
// - Automatically advances to first offer.
// --> bEntryAdvance: true, to advance to next entry. false, recalculate.
// <-- uOfferIndex : 0=end of list.
TER nodeAdvance (
    RippleCalc& rippleCalc,
    const unsigned int          nodeIndex,
    PathState&                  pathState,
    const bool                  bMultiQuality,
    const bool                  bReverse)
{
    auto& previousNode = pathState.nodes()[nodeIndex - 1];
    auto& node = pathState.nodes()[nodeIndex];
    TER             resultCode       = tesSUCCESS;

    // Taker is the active party against an offer in the ledger - the entity
    // that is taking advantage of an offer in the order book.
    WriteLog (lsTRACE, RippleCalc)
            << "nodeAdvance: TakerPays:"
            << node.saTakerPays << " TakerGets:" << node.saTakerGets;

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

        bool bDirectDirDirty = false;

        if (!node.uDirectTip)
        {
            // Need to initialize current node.

            node.uDirectTip = Ledger::getBookBase (
                previousNode.currency_, previousNode.issuer_,
                node.currency_,
                node.issuer_);
            node.uDirectEnd = Ledger::getQualityNext (node.uDirectTip);

            // TODO(tom): it seems impossible that any actual offers with
            // quality == 0 could occur - we should disallow them, and clear
            // sleDirectDir without the database call in the next line.
            node.sleDirectDir    = rippleCalc.mActiveLedger.entryCache (
                ltDIR_NODE, node.uDirectTip);

            // Associated vars are dirty, if found it.
            bDirectDirDirty = !!node.sleDirectDir;

            // Advance, if didn't find it. Normal not to be unable to lookup
            // firstdirectory. Maybe even skip this lookup.
            node.bDirectAdvance  = !node.sleDirectDir;
            node.bDirectRestart  = false;

            WriteLog (lsTRACE, RippleCalc)
                << "nodeAdvance: Initialize node:"
                << " node.uDirectTip=" << node.uDirectTip
                <<" node.uDirectEnd=" << node.uDirectEnd
                << " node.bDirectAdvance=" << node.bDirectAdvance;
        }

        if (node.bDirectAdvance || node.bDirectRestart)
        {
            // Get next quality.
            if (node.bDirectAdvance)
            {
                // This works because the Merkel radix tree is ordered by key so
                // we can go to the next one in O(1).
                node.uDirectTip  = rippleCalc.mActiveLedger.getNextLedgerIndex (
                    node.uDirectTip, node.uDirectEnd);
            }

            bDirectDirDirty = true;
            node.bDirectAdvance  = false;
            node.bDirectRestart  = false;

            if (node.uDirectTip != zero)
            {
                // We didn't run off the end of this order book and found
                // another quality directory.
                WriteLog (lsTRACE, RippleCalc)
                    << "nodeAdvance: Quality advance: node.uDirectTip="
                    << node.uDirectTip;

                node.sleDirectDir = rippleCalc.mActiveLedger.entryCache (ltDIR_NODE, node.uDirectTip);
            }
            else if (bReverse)
            {
                WriteLog (lsTRACE, RippleCalc)
                    << "nodeAdvance: No more offers.";

                node.offerIndex_ = 0;
                break;
            }
            else
            {
                // No more offers. Should be done rather than fall off end of
                // book.
                WriteLog (lsWARNING, RippleCalc)
                    << "nodeAdvance: Unreachable: "
                    << "Fell off end of order book.";
                // FIXME: why?
                return rippleCalc.mOpenLedger ? telFAILED_PROCESSING :
                    tecFAILED_PROCESSING;
            }
        }

        if (bDirectDirDirty)
        {
            // Our quality changed since last iteration.
            // Use the rate from the directory.
            node.saOfrRate = STAmount::setRate (Ledger::getQuality (node.uDirectTip));
            // For correct ratio
            node.uEntry          = 0;
            node.bEntryAdvance   = true;

            WriteLog (lsTRACE, RippleCalc)
                << "nodeAdvance: directory dirty: node.saOfrRate="
                << node.saOfrRate;
        }

        if (!node.bEntryAdvance)
        {
            if (node.bFundsDirty)
            {
                // We were called again probably merely to update structure
                // variables.
                node.saTakerPays = node.sleOffer->getFieldAmount (sfTakerPays);
                node.saTakerGets = node.sleOffer->getFieldAmount (sfTakerGets);

                // Funds left.
                node.saOfferFunds = rippleCalc.mActiveLedger.accountFunds (
                    node.offerOwnerAccount_, node.saTakerGets);
                node.bFundsDirty     = false;

                WriteLog (lsTRACE, RippleCalc)
                    << "nodeAdvance: funds dirty: node.saOfrRate="
                    << node.saOfrRate;
            }
            else
            {
                WriteLog (lsTRACE, RippleCalc) << "nodeAdvance: as is";
            }
        }
        else if (!rippleCalc.mActiveLedger.dirNext (
            node.uDirectTip, node.sleDirectDir, node.uEntry, node.offerIndex_))
            // This is the only place that offerIndex_ changes.
        {
            // Failed to find an entry in directory.
            // Do another cur directory iff bMultiQuality
            if (bMultiQuality)
            {
                // We are allowed to process multiple qualities if this is the
                // only path.
                WriteLog (lsTRACE, RippleCalc)
                    << "nodeAdvance: next quality";
                node.bDirectAdvance  = true;  // Process next quality.
            }
            else if (!bReverse)
            {
                // We didn't run dry going backwards - why are we running dry
                // going forwards - this should be impossible!
                // TODO(tom): these warnings occur in production!  They
                // shouldn't.
                WriteLog (lsWARNING, RippleCalc)
                    << "nodeAdvance: unreachable: ran out of offers";
                return rippleCalc.mOpenLedger ? telFAILED_PROCESSING :
                    tecFAILED_PROCESSING;
            }
            else
            {
                // Ran off end of offers.
                node.bEntryAdvance   = false;        // Done.
                node.offerIndex_ = 0;            // Report no more entries.
            }
        }
        else
        {
            // Got a new offer.
            node.sleOffer = rippleCalc.mActiveLedger.entryCache (
                ltOFFER, node.offerIndex_);

            if (!node.sleOffer)
            {
                // Corrupt directory that points to an entry that doesn't exist.
                // This has happened in production.
                WriteLog (lsWARNING, RippleCalc) <<
                    "Missing offer in directory";
                node.bEntryAdvance = true;
            }
            else
            {
                node.offerOwnerAccount_
                    = node.sleOffer->getFieldAccount160 (sfAccount);
                node.saTakerPays = node.sleOffer->getFieldAmount (sfTakerPays);
                node.saTakerGets = node.sleOffer->getFieldAmount (sfTakerGets);

                const AccountCurrencyIssuer asLine (
                    node.offerOwnerAccount_, node.currency_, node.issuer_);

                WriteLog (lsTRACE, RippleCalc)
                    << "nodeAdvance: offerOwnerAccount_="
                    << RippleAddress::createHumanAccountID (node.offerOwnerAccount_)
                    << " node.saTakerPays=" << node.saTakerPays
                    << " node.saTakerGets=" << node.saTakerGets
                    << " node.offerIndex_=" << node.offerIndex_;

                if (node.sleOffer->isFieldPresent (sfExpiration) &&
                    (node.sleOffer->getFieldU32 (sfExpiration) <=
                     rippleCalc.mActiveLedger.getLedger ()->getParentCloseTimeNC ()))
                {
                    // Offer is expired.
                    WriteLog (lsTRACE, RippleCalc)
                        << "nodeAdvance: expired offer";
                    rippleCalc.mUnfundedOffers.insert(node.offerIndex_);
                    continue;
                }

                if (node.saTakerPays <= zero || node.saTakerGets <= zero)
                {
                    // Offer has bad amounts. Offers should never have a bad
                    // amounts.

                    if (bReverse)
                    {
                        // Past internal error, offer had bad amounts.
                        // This has occurred in production.
                        WriteLog (lsWARNING, RippleCalc)
                            << "nodeAdvance: PAST INTERNAL ERROR"
                            << " REVERSE: OFFER NON-POSITIVE:"
                            << " node.saTakerPays=" << node.saTakerPays
                            << " node.saTakerGets=%s" << node.saTakerGets;

                        // Mark offer for always deletion.
                        rippleCalc.mUnfundedOffers.insert (node.offerIndex_);
                    }
                    else if (rippleCalc.mUnfundedOffers.find (node.offerIndex_)
                             != rippleCalc.mUnfundedOffers.end ())
                    {
                        // Past internal error, offer was found failed to place
                        // this in mUnfundedOffers.
                        // Just skip it. It will be deleted.
                        WriteLog (lsDEBUG, RippleCalc)
                            << "nodeAdvance: PAST INTERNAL ERROR "
                            << " FORWARD CONFIRM: OFFER NON-POSITIVE:"
                            << " node.saTakerPays=" << node.saTakerPays
                            << " node.saTakerGets=" << node.saTakerGets;

                    }
                    else
                    {
                        // Reverse should have previously put bad offer in list.
                        // An internal error previously left a bad offer.
                        WriteLog (lsWARNING, RippleCalc)
                            << "nodeAdvance: INTERNAL ERROR"

                            <<" FORWARD NEWLY FOUND: OFFER NON-POSITIVE:"
                            << " node.saTakerPays=" << node.saTakerPays
                            << " node.saTakerGets=" << node.saTakerGets;

                        // Don't process at all, things are in an unexpected
                        // state for this transactions.
                        resultCode = tefEXCEPTION;
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
                auto itForward = pathState.forward().find (asLine);
                const bool bFoundForward = itForward != pathState.forward().end ();

                // Only allow a source to be used once, in the first node
                // encountered from initial path scan.  This prevents
                // conflicting uses of the same balance when going reverse vs
                // forward.
                if (bFoundForward &&
                    itForward->second != nodeIndex &&
                    node.offerOwnerAccount_ != node.issuer_)
                {
                    // Temporarily unfunded. Another node uses this source,
                    // ignore in this offer.
                    WriteLog (lsTRACE, RippleCalc)
                        << "nodeAdvance: temporarily unfunded offer"
                        << " (forward)";
                    continue;
                }

                // This is overly strict. For contributions to past. We should
                // only count source if actually used.
                auto itReverse = pathState.reverse().find (asLine);
                bool bFoundReverse = itReverse != pathState.reverse().end ();

                // For this quality increment, only allow a source to be used
                // from a single node, in the first node encountered from
                // applying offers in reverse.
                if (bFoundReverse &&
                    itReverse->second != nodeIndex &&
                    node.offerOwnerAccount_ != node.issuer_)
                {
                    // Temporarily unfunded. Another node uses this source,
                    // ignore in this offer.
                    WriteLog (lsTRACE, RippleCalc)
                        << "nodeAdvance: temporarily unfunded offer"
                        <<" (reverse)";
                    continue;
                }

                // Determine if used in past.
                // We only need to know if it might need to be marked unfunded.
                auto itPast = rippleCalc.mumSource.find (asLine);
                bool bFoundPast = (itPast != rippleCalc.mumSource.end ());

                // Only the current node is allowed to use the source.

                node.saOfferFunds = rippleCalc.mActiveLedger.accountFunds
                        (node.offerOwnerAccount_, node.saTakerGets); // Funds held.

                if (node.saOfferFunds <= zero)
                {
                    // Offer is unfunded.
                    WriteLog (lsTRACE, RippleCalc)
                        << "nodeAdvance: unfunded offer";

                    if (bReverse && !bFoundReverse && !bFoundPast)
                    {
                        // Never mentioned before, clearly just: found unfunded.
                        // That is, even if this offer fails due to fill or kill
                        // still do deletions.
                        // Mark offer for always deletion.
                        rippleCalc.mUnfundedOffers.insert (node.offerIndex_);
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
                        << "nodeAdvance: remember="
                        <<  RippleAddress::createHumanAccountID (node.offerOwnerAccount_)
                        << "/"
                        << STAmount::createHumanCurrency (node.currency_)
                        << "/"
                        << RippleAddress::createHumanAccountID (node.issuer_);

                    pathState.reverse().insert (std::make_pair (asLine, nodeIndex));
                }

                node.bFundsDirty     = false;
                node.bEntryAdvance   = false;
            }
        }
    }
    while (resultCode == tesSUCCESS && (node.bEntryAdvance || node.bDirectAdvance));

    if (resultCode == tesSUCCESS)
    {
        WriteLog (lsTRACE, RippleCalc)
            << "nodeAdvance: node.offerIndex_=" << node.offerIndex_;
    }
    else
    {
        WriteLog (lsDEBUG, RippleCalc)
            << "nodeAdvance: resultCode=" << transToken (resultCode);
    }

    return resultCode;
}

}  // path
}  // ripple
