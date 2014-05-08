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

namespace {
const int NODE_ADVANCE_MAX_LOOPS = 100;

// VFALCO TODO Why 40?
// NOTE is the number 40 part of protocol?
const int CALC_NODE_DELIVER_MAX_LOOPS = 40;
}

// OPTIMIZE: When calculating path increment, note if increment consumes all
// liquidity. No need to revisit path in the future if all liquidity is used.
//

SETUP_LOG (RippleCalc)

// VFALCO TODO Update the comment for this function, the argument list no
//             resembles the comment
//
//             Provide a better explanation for what this function does.

// If needed, advance to next funded offer.
// - Automatically advances to first offer.
// --> bEntryAdvance: true, to advance to next entry. false, recalculate.
// <-- uOfferIndex : 0=end of list.
TER RippleCalc::calcNodeAdvance (
    const unsigned int          uNode,              // 0 < uNode < uLast
    PathState&                  psCur,
    const bool                  bMultiQuality,
    const bool                  bReverse)
{
    PathState::Node&    pnPrv       = psCur.vpnNodes[uNode - 1];
    PathState::Node&    pnCur       = psCur.vpnNodes[uNode];

    const uint160&  uPrvCurrencyID  = pnPrv.uCurrencyID;
    const uint160&  uPrvIssuerID    = pnPrv.uIssuerID;
    const uint160&  uCurCurrencyID  = pnCur.uCurrencyID;
    const uint160&  uCurIssuerID    = pnCur.uIssuerID;

    uint256&        uDirectTip      = pnCur.uDirectTip;
    uint256&        uDirectEnd      = pnCur.uDirectEnd;
    bool&           bDirectAdvance  = pnCur.bDirectAdvance;
    bool&           bDirectRestart  = pnCur.bDirectRestart;
    SLE::pointer&   sleDirectDir    = pnCur.sleDirectDir;
    STAmount&       saOfrRate       = pnCur.saOfrRate;

    bool&           bEntryAdvance   = pnCur.bEntryAdvance;
    unsigned int&   uEntry          = pnCur.uEntry;
    uint256&        uOfferIndex     = pnCur.uOfferIndex;
    SLE::pointer&   sleOffer        = pnCur.sleOffer;
    uint160&        uOfrOwnerID     = pnCur.uOfrOwnerID;
    STAmount&       saOfferFunds    = pnCur.saOfferFunds;
    STAmount&       saTakerPays     = pnCur.saTakerPays;
    STAmount&       saTakerGets     = pnCur.saTakerGets;
    bool&           bFundsDirty     = pnCur.bFundsDirty;

    TER             terResult       = tesSUCCESS;

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
                uPrvCurrencyID, uPrvIssuerID, uCurCurrencyID, uCurIssuerID);
            uDirectEnd      = Ledger::getQualityNext (uDirectTip);

            sleDirectDir    = mActiveLedger.entryCache (ltDIR_NODE, uDirectTip);

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
                uDirectTip  = mActiveLedger.getNextLedgerIndex (
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

                sleDirectDir = mActiveLedger.entryCache (ltDIR_NODE, uDirectTip);
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
                return mOpenLedger ? telFAILED_PROCESSING :
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

                saOfferFunds = mActiveLedger.accountFunds (
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
        else if (!mActiveLedger.dirNext (
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
                return mOpenLedger ? telFAILED_PROCESSING :
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
            sleOffer = mActiveLedger.entryCache (ltOFFER, uOfferIndex);

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

                const aciSource asLine = std::make_tuple (
                    uOfrOwnerID, uCurCurrencyID, uCurIssuerID);

                WriteLog (lsTRACE, RippleCalc)
                    << "calcNodeAdvance: uOfrOwnerID="
                    << RippleAddress::createHumanAccountID (uOfrOwnerID)
                    << " saTakerPays=" << saTakerPays
                    << " saTakerGets=" << saTakerGets
                    << " uOfferIndex=" << uOfferIndex;

                if (sleOffer->isFieldPresent (sfExpiration) &&
                    (sleOffer->getFieldU32 (sfExpiration) <=
                     mActiveLedger.getLedger ()->getParentCloseTimeNC ()))
                {
                    // Offer is expired.
                    WriteLog (lsTRACE, RippleCalc)
                        << "calcNodeAdvance: expired offer";
                    mUnfundedOffers.insert(uOfferIndex);
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
                        mUnfundedOffers.insert (uOfferIndex);
                    }
                    else if (mUnfundedOffers.find (uOfferIndex) !=
                             mUnfundedOffers.end ())
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
                        terResult = tefEXCEPTION;
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
                curIssuerNodeConstIterator itForward
                    = psCur.umForward.find (asLine);
                const bool bFoundForward = itForward != psCur.umForward.end ();

                // Only allow a source to be used once, in the first node
                // encountered from initial path scan.  This prevents
                // conflicting uses of the same balance when going reverse vs
                // forward.
                if (bFoundForward &&
                    itForward->second != uNode &&
                    uOfrOwnerID != uCurIssuerID)
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
                curIssuerNodeConstIterator itReverse
                    = psCur.umReverse.find (asLine);
                bool bFoundReverse = itReverse != psCur.umReverse.end ();

                // For this quality increment, only allow a source to be used
                // from a single node, in the first node encountered from
                // applying offers in reverse.
                if (bFoundReverse &&
                    itReverse->second != uNode &&
                    uOfrOwnerID != uCurIssuerID)
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
                curIssuerNodeConstIterator itPast = mumSource.find (asLine);
                bool bFoundPast = (itPast != mumSource.end ());

                // Only the current node is allowed to use the source.

                saOfferFunds = mActiveLedger.accountFunds
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
                        mUnfundedOffers.insert (uOfferIndex);
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
                        << "/" << STAmount::createHumanCurrency (uCurCurrencyID)
                        << "/" << RippleAddress::createHumanAccountID (
                            uCurIssuerID);

                    psCur.umReverse.insert (std::make_pair (asLine, uNode));
                }

                bFundsDirty     = false;
                bEntryAdvance   = false;
            }
        }
    }
    while (tesSUCCESS == terResult && (bEntryAdvance || bDirectAdvance));

    if (tesSUCCESS == terResult)
    {
        WriteLog (lsTRACE, RippleCalc)
            << "calcNodeAdvance: uOfferIndex=" << uOfferIndex;
    }
    else
    {
        WriteLog (lsDEBUG, RippleCalc)
            << "calcNodeAdvance: terResult=" << transToken (terResult);
    }

    return terResult;
}

// At the right most node of a list of consecutive offer nodes, given the amount
// requested to be delivered, push toward node 0 the amount requested for
// previous nodes to know how much to deliver.
//
// Between offer nodes, the fee charged may vary.  Therefore, process one
// inbound offer at a time.  Propagate the inbound offer's requirements to the
// previous node.  The previous node adjusts the amount output and the amount
// spent on fees.  Continue processing until the request is satisified as long
// as the rate does not increase past the initial rate.

TER RippleCalc::calcNodeDeliverRev (
    const unsigned int uNode,          // 0 < uNode < uLast
    PathState&         psCur,
    const bool         bMultiQuality,  // True, if not constrained to the same
                                       // or better quality.
    const uint160&     uOutAccountID,  // --> Output owner's account.
    const STAmount&    saOutReq,       // --> Funds requested to be
                                       // delivered for an increment.
    STAmount&          saOutAct)       // <-- Funds actually delivered for an
                                       // increment.
{
    TER terResult   = tesSUCCESS;

    PathState::Node&    pnPrv       = psCur.vpnNodes[uNode - 1];
    PathState::Node&    pnCur       = psCur.vpnNodes[uNode];

    const uint160&  uCurIssuerID    = pnCur.uIssuerID;
    const uint160&  uPrvAccountID   = pnPrv.uAccountID;
    const STAmount& saTransferRate  = pnCur.saTransferRate;
    // Transfer rate of the TakerGets issuer.

    STAmount&       saPrvDlvReq     = pnPrv.saRevDeliver;
    // Accumulation of what the previous node must deliver.

    uint256&        uDirectTip      = pnCur.uDirectTip;
    bool&           bDirectRestart  = pnCur.bDirectRestart;

    if (bMultiQuality)
        uDirectTip      = 0;                        // Restart book searching.
    else
        bDirectRestart  = true;                     // Restart at same quality.

    // YYY Note this gets zeroed on each increment, ideally only on first
    // increment, then it could be a limit on the forward pass.
    saOutAct.clear (saOutReq);

    WriteLog (lsTRACE, RippleCalc)
        << "calcNodeDeliverRev>"
        << " saOutAct=" << saOutAct
        << " saOutReq=" << saOutReq
        << " saPrvDlvReq=" << saPrvDlvReq;

    assert (!!saOutReq);

    int loopCount = 0;

    // While we did not deliver as much as requested:
    while (saOutAct < saOutReq)
    {
        if (++loopCount > CALC_NODE_DELIVER_MAX_LOOPS)
        {
            WriteLog (lsFATAL, RippleCalc) << "loop count exceeded";
            return mOpenLedger ? telFAILED_PROCESSING : tecFAILED_PROCESSING;
        }

        bool&           bEntryAdvance   = pnCur.bEntryAdvance;
        STAmount&       saOfrRate       = pnCur.saOfrRate;
        uint256&        uOfferIndex     = pnCur.uOfferIndex;
        SLE::pointer&   sleOffer        = pnCur.sleOffer;
        const uint160&  uOfrOwnerID     = pnCur.uOfrOwnerID;
        bool&           bFundsDirty     = pnCur.bFundsDirty;
        STAmount&       saOfferFunds    = pnCur.saOfferFunds;
        STAmount&       saTakerPays     = pnCur.saTakerPays;
        STAmount&       saTakerGets     = pnCur.saTakerGets;
        STAmount&       saRateMax       = pnCur.saRateMax;

        terResult = calcNodeAdvance (
            uNode, psCur, bMultiQuality || saOutAct == zero, true);
        // If needed, advance to next funded offer.

        if (tesSUCCESS != terResult || !uOfferIndex)
        {
            // Error or out of offers.
            break;
        }

        auto const hasFee = uOfrOwnerID == uCurIssuerID
            || uOutAccountID == uCurIssuerID;  // Issuer sending or receiving.
        const STAmount saOutFeeRate = hasFee
            ? saOne             // No fee.
            : saTransferRate;   // Transfer rate of issuer.

        WriteLog (lsTRACE, RippleCalc)
            << "calcNodeDeliverRev:"
            << " uOfrOwnerID="
            << RippleAddress::createHumanAccountID (uOfrOwnerID)
            << " uOutAccountID="
            << RippleAddress::createHumanAccountID (uOutAccountID)
            << " uCurIssuerID="
            << RippleAddress::createHumanAccountID (uCurIssuerID)
            << " saTransferRate=" << saTransferRate
            << " saOutFeeRate=" << saOutFeeRate;

        if (bMultiQuality)
        {
            // In multi-quality mode, ignore rate.
        }
        else if (!saRateMax)
        {
            // Set initial rate.
            saRateMax   = saOutFeeRate;

            WriteLog (lsTRACE, RippleCalc)
                << "calcNodeDeliverRev: Set initial rate:"
                << " saRateMax=" << saRateMax
                << " saOutFeeRate=" << saOutFeeRate;
        }
        else if (saOutFeeRate > saRateMax)
        {
            // Offer exceeds initial rate.
            WriteLog (lsTRACE, RippleCalc)
                << "calcNodeDeliverRev: Offer exceeds initial rate:"
                << " saRateMax=" << saRateMax
                << " saOutFeeRate=" << saOutFeeRate;

            break;  // Done. Don't bother looking for smaller saTransferRates.
        }
        else if (saOutFeeRate < saRateMax)
        {
            // Reducing rate. Additional offers will only considered for this
            // increment if they are at least this good.
            //
            // At this point, the overall rate is reducing, while the overall
            // rate is not saOutFeeRate, it would be wrong to add anything with
            // a rate above saOutFeeRate.
            //
            // The rate would be reduced if the current offer was from the
            // issuer and the previous offer wasn't.

            saRateMax   = saOutFeeRate;

            WriteLog (lsTRACE, RippleCalc)
                << "calcNodeDeliverRev: Reducing rate:"
                << " saRateMax=" << saRateMax;
        }

        // Amount that goes to the taker.
        STAmount saOutPassReq = std::min (
            std::min (saOfferFunds, saTakerGets),
            saOutReq - saOutAct);

        // Maximum out - assuming no out fees.
        STAmount saOutPassAct = saOutPassReq;

        // Amount charged to the offer owner.
        //
        // The fee goes to issuer. The fee is paid by offer owner and not passed
        // as a cost to taker.
        //
        // Round down: prefer liquidity rather than microscopic fees.
        STAmount saOutPlusFees   = STAmount::mulRound (
            saOutPassAct, saOutFeeRate, false);
        // Offer out with fees.

        WriteLog (lsTRACE, RippleCalc)
            << "calcNodeDeliverRev:"
            << " saOutReq=" << saOutReq
            << " saOutAct=" << saOutAct
            << " saTakerGets=" << saTakerGets
            << " saOutPassAct=" << saOutPassAct
            << " saOutPlusFees=" << saOutPlusFees
            << " saOfferFunds=" << saOfferFunds;

        if (saOutPlusFees > saOfferFunds)
        {
            // Offer owner can not cover all fees, compute saOutPassAct based on
            // saOfferFunds.
            saOutPlusFees   = saOfferFunds;

            // Round up: prefer liquidity rather than microscopic fees. But,
            // limit by requested.
            auto fee = STAmount::divRound (saOutPlusFees, saOutFeeRate, true);
            saOutPassAct = std::min (saOutPassReq, fee);

            WriteLog (lsTRACE, RippleCalc)
                << "calcNodeDeliverRev: Total exceeds fees:"
                << " saOutPassAct=" << saOutPassAct
                << " saOutPlusFees=" << saOutPlusFees
                << " saOfferFunds=" << saOfferFunds;
        }

        // Compute portion of input needed to cover actual output.
        auto outputFee = STAmount::mulRound (
            saOutPassAct, saOfrRate, saTakerPays, true);
        STAmount saInPassReq = std::min (saTakerPays, outputFee);
        STAmount saInPassAct;

        WriteLog (lsTRACE, RippleCalc)
            << "calcNodeDeliverRev:"
            << " outputFee=" << outputFee
            << " saInPassReq=" << saInPassReq
            << " saOfrRate=" << saOfrRate
            << " saOutPassAct=" << saOutPassAct
            << " saOutPlusFees=" << saOutPlusFees;

        if (!saInPassReq) // FIXME: This is bogus
        {
            // After rounding did not want anything.
            WriteLog (lsDEBUG, RippleCalc)
                << "calcNodeDeliverRev: micro offer is unfunded.";

            bEntryAdvance   = true;
            continue;
        }
        // Find out input amount actually available at current rate.
        else if (!!uPrvAccountID)
        {
            // account --> OFFER --> ?
            // Due to node expansion, previous is guaranteed to be the issuer.
            //
            // Previous is the issuer and receiver is an offer, so no fee or
            // quality.
            //
            // Previous is the issuer and has unlimited funds.
            //
            // Offer owner is obtaining IOUs via an offer, so credit line limits
            // are ignored.  As limits are ignored, don't need to adjust
            // previous account's balance.

            saInPassAct = saInPassReq;

            WriteLog (lsTRACE, RippleCalc)
                << "calcNodeDeliverRev: account --> OFFER --> ? :"
                << " saInPassAct=" << saInPassAct;
        }
        else
        {
            // offer --> OFFER --> ?
            // Compute in previous offer node how much could come in.

            terResult   = calcNodeDeliverRev (
                              uNode - 1,
                              psCur,
                              bMultiQuality,
                              uOfrOwnerID,
                              saInPassReq,
                              saInPassAct);

            WriteLog (lsTRACE, RippleCalc)
                << "calcNodeDeliverRev: offer --> OFFER --> ? :"
                << " saInPassAct=" << saInPassAct;
        }

        if (tesSUCCESS != terResult)
            break;

        if (saInPassAct < saInPassReq)
        {
            // Adjust output to conform to limited input.
            auto outputRequirements = STAmount::divRound (
                saInPassAct, saOfrRate, saTakerGets, true);
            saOutPassAct = std::min (saOutPassReq, outputRequirements);
            auto outputFees = STAmount::mulRound (
                saOutPassAct, saOutFeeRate, true);
            saOutPlusFees   = std::min (saOfferFunds, outputFees);

            WriteLog (lsTRACE, RippleCalc)
                << "calcNodeDeliverRev: adjusted:"
                << " saOutPassAct=" << saOutPassAct
                << " saOutPlusFees=" << saOutPlusFees;
        }
        else
        {
            // TODO(tom): more logging here.
            assert (saInPassAct == saInPassReq);
        }

        // Funds were spent.
        bFundsDirty = true;

        // Want to deduct output to limit calculations while computing reverse.
        // Don't actually need to send.
        //
        // Sending could be complicated: could fund a previous offer not yet
        // visited.  However, these deductions and adjustments are tenative.
        //
        // Must reset balances when going forward to perform actual transfers.
        terResult   = mActiveLedger.accountSend (
            uOfrOwnerID, uCurIssuerID, saOutPassAct);

        if (tesSUCCESS != terResult)
            break;

        // Adjust offer
        STAmount    saTakerGetsNew  = saTakerGets - saOutPassAct;
        STAmount    saTakerPaysNew  = saTakerPays - saInPassAct;

        if (saTakerPaysNew < zero || saTakerGetsNew < zero)
        {
            WriteLog (lsWARNING, RippleCalc)
                << "calcNodeDeliverRev: NEGATIVE:"
                << " saTakerPaysNew=" << saTakerPaysNew
                << " saTakerGetsNew=%s" << saTakerGetsNew;

            // If mOpenLedger then ledger is not final, can vote no.
            terResult   = mOpenLedger ? telFAILED_PROCESSING                                                           : tecFAILED_PROCESSING;
            break;
        }

        sleOffer->setFieldAmount (sfTakerGets, saTakerGetsNew);
        sleOffer->setFieldAmount (sfTakerPays, saTakerPaysNew);

        mActiveLedger.entryModify (sleOffer);

        if (saOutPassAct == saTakerGets)
        {
            // Offer became unfunded.
            WriteLog (lsDEBUG, RippleCalc)
                << "calcNodeDeliverRev: offer became unfunded.";

            bEntryAdvance   = true;     // XXX When don't we want to set advance?
        }
        else
        {
            assert (saOutPassAct < saTakerGets);
        }

        saOutAct    += saOutPassAct;
        // Accumulate what is to be delivered from previous node.
        saPrvDlvReq += saInPassAct;
    }

    CondLog (saOutAct > saOutReq, lsWARNING, RippleCalc)
        << "calcNodeDeliverRev: TOO MUCH:"
        << " saOutAct=" << saOutAct
        << " saOutReq=" << saOutReq;

    assert (saOutAct <= saOutReq);

    // XXX Perhaps need to check if partial is okay to relax this?
    if (tesSUCCESS == terResult && !saOutAct)
        terResult   = tecPATH_DRY;
    // Unable to meet request, consider path dry.

    WriteLog (lsTRACE, RippleCalc)
        << "calcNodeDeliverRev<"
        << " saOutAct=" << saOutAct
        << " saOutReq=" << saOutReq
        << " saPrvDlvReq=" << saPrvDlvReq;

    return terResult;
}

// For current offer, get input from deliver/limbo and output to next account or
// deliver for next offers.
//
// <-- pnCur.saFwdDeliver: For calcNodeAccountFwd to know how much went through
// --> pnCur.saRevDeliver: Do not exceed.
TER RippleCalc::calcNodeDeliverFwd (
    const unsigned int uNode,          // 0 < uNode < uLast
    PathState&         psCur,
    const bool         bMultiQuality,
    const uint160&     uInAccountID,   // --> Input owner's account.
    const STAmount&    saInReq,        // --> Amount to deliver.
    STAmount&          saInAct,        // <-- Amount delivered, this invokation.
    STAmount&          saInFees)       // <-- Fees charged, this invokation.
{
    TER terResult   = tesSUCCESS;

    PathState::Node&    pnPrv       = psCur.vpnNodes[uNode - 1];
    PathState::Node&    pnCur       = psCur.vpnNodes[uNode];
    PathState::Node&    pnNxt       = psCur.vpnNodes[uNode + 1];

    const uint160&  uNxtAccountID   = pnNxt.uAccountID;
    const uint160&  uCurCurrencyID  = pnCur.uCurrencyID;
    const uint160&  uCurIssuerID    = pnCur.uIssuerID;
    uint256 const&  uOfferIndex     = pnCur.uOfferIndex;
    const uint160&  uPrvCurrencyID  = pnPrv.uCurrencyID;
    const uint160&  uPrvIssuerID    = pnPrv.uIssuerID;
    const STAmount& saInTransRate   = pnPrv.saTransferRate;
    const STAmount& saCurDeliverMax = pnCur.saRevDeliver;
    // Don't deliver more than wanted.

    STAmount&       saCurDeliverAct = pnCur.saFwdDeliver;
    // Zeroed in reverse pass.

    uint256&        uDirectTip      = pnCur.uDirectTip;
    bool&           bDirectRestart  = pnCur.bDirectRestart;

    if (bMultiQuality)
        uDirectTip      = 0;                        // Restart book searching.
    else
        bDirectRestart  = true;                     // Restart at same quality.

    saInAct.clear (saInReq);
    saInFees.clear (saInReq);

    int loopCount = 0;

    // XXX Perhaps make sure do not exceed saCurDeliverMax as another way to
    // stop?
    while (tesSUCCESS == terResult && saInAct + saInFees < saInReq)
    {
        // Did not spend all inbound deliver funds.
        if (++loopCount > CALC_NODE_DELIVER_MAX_LOOPS)
        {
            WriteLog (lsWARNING, RippleCalc)
                << "calcNodeDeliverFwd: max loops cndf";
            return mOpenLedger ? telFAILED_PROCESSING : tecFAILED_PROCESSING;
        }

        // Determine values for pass to adjust saInAct, saInFees, and
        // saCurDeliverAct.
        terResult   = calcNodeAdvance (
            uNode, psCur, bMultiQuality || saInAct == zero, false);
        // If needed, advance to next funded offer.

        if (tesSUCCESS != terResult)
        {
            nothing ();
        }
        else if (!uOfferIndex)
        {
            WriteLog (lsWARNING, RippleCalc)
                << "calcNodeDeliverFwd: INTERNAL ERROR: Ran out of offers.";
            return mOpenLedger ? telFAILED_PROCESSING : tecFAILED_PROCESSING;
        }
        else if (tesSUCCESS == terResult)
        {
            // Doesn't charge input. Input funds are in limbo.
            bool&           bEntryAdvance   = pnCur.bEntryAdvance;
            STAmount&       saOfrRate       = pnCur.saOfrRate;
            uint256&        uOfferIndex     = pnCur.uOfferIndex;
            SLE::pointer&   sleOffer        = pnCur.sleOffer;
            const uint160&  uOfrOwnerID     = pnCur.uOfrOwnerID;
            bool&           bFundsDirty     = pnCur.bFundsDirty;
            STAmount&       saOfferFunds    = pnCur.saOfferFunds;
            STAmount&       saTakerPays     = pnCur.saTakerPays;
            STAmount&       saTakerGets     = pnCur.saTakerGets;

            const STAmount  saInFeeRate
                = !uPrvCurrencyID                   // XRP.
                  || uInAccountID == uPrvIssuerID   // Sender is issuer.
                  || uOfrOwnerID == uPrvIssuerID    // Reciever is issuer.
                  ? saOne                           // No fee.
                  : saInTransRate;                  // Transfer rate of issuer.

            // First calculate assuming no output fees: saInPassAct,
            // saInPassFees, saOutPassAct.

            // Offer maximum out - limited by funds with out fees.
            STAmount    saOutFunded     = std::min (saOfferFunds, saTakerGets);

            // Offer maximum out - limit by most to deliver.
            STAmount    saOutPassFunded = std::min (
                saOutFunded, saCurDeliverMax - saCurDeliverAct);

            // Offer maximum in - Limited by by payout.
            STAmount    saInFunded      = STAmount::mulRound (
                saOutPassFunded, saOfrRate, saTakerPays, true);

            // Offer maximum in with fees.
            STAmount    saInTotal       = STAmount::mulRound (
                saInFunded, saInFeeRate, true);
            STAmount    saInRemaining   = saInReq - saInAct - saInFees;

            if (saInRemaining < zero)
                saInRemaining.clear();

            // In limited by remaining.
            STAmount    saInSum = std::min (saInTotal, saInRemaining);

            // In without fees.
            STAmount    saInPassAct = std::min (
                saTakerPays, STAmount::divRound (saInSum, saInFeeRate, true));

            // Out limited by in remaining.
            auto outPass = STAmount::divRound (
                saInPassAct, saOfrRate, saTakerGets, true);
            STAmount    saOutPassMax    = std::min (saOutPassFunded, outPass);

            STAmount    saInPassFeesMax = saInSum - saInPassAct;

            // Will be determined by next node.
            STAmount    saOutPassAct;

            // Will be determined by adjusted saInPassAct.
            STAmount    saInPassFees;

            WriteLog (lsTRACE, RippleCalc)
                << "calcNodeDeliverFwd:"
                << " uNode=" << uNode
                << " saOutFunded=" << saOutFunded
                << " saOutPassFunded=" << saOutPassFunded
                << " saOfferFunds=" << saOfferFunds
                << " saTakerGets=" << saTakerGets
                << " saInReq=" << saInReq
                << " saInAct=" << saInAct
                << " saInFees=" << saInFees
                << " saInFunded=" << saInFunded
                << " saInTotal=" << saInTotal
                << " saInSum=" << saInSum
                << " saInPassAct=" << saInPassAct
                << " saOutPassMax=" << saOutPassMax;

            // FIXME: We remove an offer if WE didn't want anything out of it?
            if (!saTakerPays || saInSum <= zero)
            {
                WriteLog (lsDEBUG, RippleCalc)
                    << "calcNodeDeliverFwd: Microscopic offer unfunded.";

                // After math offer is effectively unfunded.
                psCur.vUnfundedBecame.push_back (uOfferIndex);
                bEntryAdvance   = true;
                continue;
            }
            else if (!saInFunded)
            {
                // Previous check should catch this.
                WriteLog (lsWARNING, RippleCalc)
                    << "calcNodeDeliverFwd: UNREACHABLE REACHED";

                // After math offer is effectively unfunded.
                psCur.vUnfundedBecame.push_back (uOfferIndex);
                bEntryAdvance   = true;
                continue;
            }
            else if (!!uNxtAccountID)
            {
                // ? --> OFFER --> account
                // Input fees: vary based upon the consumed offer's owner.
                // Output fees: none as XRP or the destination account is the
                // issuer.

                saOutPassAct    = saOutPassMax;
                saInPassFees    = saInPassFeesMax;

                WriteLog (lsTRACE, RippleCalc)
                    << "calcNodeDeliverFwd: ? --> OFFER --> account:"
                    << " uOfrOwnerID="
                    << RippleAddress::createHumanAccountID (uOfrOwnerID)
                    << " uNxtAccountID="
                    << RippleAddress::createHumanAccountID (uNxtAccountID)
                    << " saOutPassAct=" << saOutPassAct
                    << " saOutFunded=%s" << saOutFunded;

                // Output: Debit offer owner, send XRP or non-XPR to next
                // account.
                terResult   = mActiveLedger.accountSend (
                    uOfrOwnerID, uNxtAccountID, saOutPassAct);

                if (tesSUCCESS != terResult)
                    break;
            }
            else
            {
                // ? --> OFFER --> offer
                //
                // Offer to offer means current order book's output currency and
                // issuer match next order book's input current and issuer.
                //
                // Output fees: possible if issuer has fees and is not on either
                // side.
                STAmount    saOutPassFees;

                // Output fees vary as the next nodes offer owners may vary.
                // Therefore, immediately push through output for current offer.
                terResult   = RippleCalc::calcNodeDeliverFwd (
                                  uNode + 1,
                                  psCur,
                                  bMultiQuality,
                                  uOfrOwnerID,        // --> Current holder.
                                  saOutPassMax,       // --> Amount available.
                                  saOutPassAct,       // <-- Amount delivered.
                                  saOutPassFees);     // <-- Fees charged.

                if (tesSUCCESS != terResult)
                    break;

                if (saOutPassAct == saOutPassMax)
                {
                    // No fees and entire output amount.

                    saInPassFees    = saInPassFeesMax;
                }
                else
                {
                    // Fraction of output amount.
                    // Output fees are paid by offer owner and not passed to
                    // previous.

                    assert (saOutPassAct < saOutPassMax);
                    auto inPassAct = STAmount::mulRound (
                        saOutPassAct, saOfrRate, saInReq, true);
                    saInPassAct = std::min (saTakerPays, inPassAct);
                    auto inPassFees = STAmount::mulRound (
                        saInPassAct, saInFeeRate, true);
                    saInPassFees    = std::min (saInPassFeesMax, inPassFees);
                }

                // Do outbound debiting.
                // Send to issuer/limbo total amount including fees (issuer gets
                // fees).
                auto id = !!uCurCurrencyID ? uCurIssuerID : ACCOUNT_XRP;
                auto outPassTotal = saOutPassAct + saOutPassFees;
                mActiveLedger.accountSend (uOfrOwnerID, id, outPassTotal);

                WriteLog (lsTRACE, RippleCalc)
                    << "calcNodeDeliverFwd: ? --> OFFER --> offer:"
                    << " saOutPassAct=" << saOutPassAct
                    << " saOutPassFees=" << saOutPassFees;
            }

            WriteLog (lsTRACE, RippleCalc)
                << "calcNodeDeliverFwd: "
                << " uNode=" << uNode
                << " saTakerGets=" << saTakerGets
                << " saTakerPays=" << saTakerPays
                << " saInPassAct=" << saInPassAct
                << " saInPassFees=" << saInPassFees
                << " saOutPassAct=" << saOutPassAct
                << " saOutFunded=" << saOutFunded;

            // Funds were spent.
            bFundsDirty     = true;

            // Do inbound crediting.
            //
            // Credit offer owner from in issuer/limbo (input transfer fees left
            // with owner).  Don't attempt to have someone credit themselves, it
            // is redundant.
            if (!uPrvCurrencyID                 // Always credit XRP from limbo.
                || uInAccountID != uOfrOwnerID) // Never send non-XRP to the
                                                // same account.
            {
                auto id = !!uPrvCurrencyID ? uInAccountID : ACCOUNT_XRP;
                terResult = mActiveLedger.accountSend (
                    id, uOfrOwnerID, saInPassAct);

                if (tesSUCCESS != terResult)
                    break;
            }

            // Adjust offer.
            //
            // Fees are considered paid from a seperate budget and are not named
            // in the offer.
            STAmount    saTakerGetsNew  = saTakerGets - saOutPassAct;
            STAmount    saTakerPaysNew  = saTakerPays - saInPassAct;

            if (saTakerPaysNew < zero || saTakerGetsNew < zero)
            {
                WriteLog (lsWARNING, RippleCalc)
                    << "calcNodeDeliverFwd: NEGATIVE:"
                    << " saTakerPaysNew=" << saTakerPaysNew
                    << " saTakerGetsNew=" << saTakerGetsNew;

                // If mOpenLedger, then ledger is not final, can vote no.
                terResult   = mOpenLedger
                              ? telFAILED_PROCESSING                                                          : tecFAILED_PROCESSING;
                break;
            }

            sleOffer->setFieldAmount (sfTakerGets, saTakerGetsNew);
            sleOffer->setFieldAmount (sfTakerPays, saTakerPaysNew);

            mActiveLedger.entryModify (sleOffer);

            if (saOutPassAct == saOutFunded || saTakerGetsNew == zero)
            {
                // Offer became unfunded.

                WriteLog (lsWARNING, RippleCalc)
                    << "calcNodeDeliverFwd: unfunded:"
                    << " saOutPassAct=" << saOutPassAct
                    << " saOutFunded=" << saOutFunded;

                psCur.vUnfundedBecame.push_back (uOfferIndex);
                bEntryAdvance   = true;
            }
            else
            {
                CondLog (saOutPassAct >= saOutFunded, lsWARNING, RippleCalc)
                    << "calcNodeDeliverFwd: TOO MUCH:"
                    << " saOutPassAct=" << saOutPassAct
                    << " saOutFunded=" << saOutFunded;

                assert (saOutPassAct < saOutFunded);
            }

            saInAct         += saInPassAct;
            saInFees        += saInPassFees;

            // Adjust amount available to next node.
            saCurDeliverAct = std::min (saCurDeliverMax,
                                        saCurDeliverAct + saOutPassAct);
        }
    }

    WriteLog (lsTRACE, RippleCalc)
        << "calcNodeDeliverFwd<"
        << " uNode=" << uNode
        << " saInAct=" << saInAct
        << " saInFees=" << saInFees;

    return terResult;
}

// Called to drive from the last offer node in a chain.
TER RippleCalc::calcNodeOfferRev (
    const unsigned int          uNode,              // 0 < uNode < uLast
    PathState&                  psCur,
    const bool                  bMultiQuality)
{
    TER             terResult;

    PathState::Node& pnCur = psCur.vpnNodes [uNode];
    PathState::Node& pnNxt = psCur.vpnNodes [uNode + 1];

    if (!!pnNxt.uAccountID)
    {
        // Next is an account node, resolve current offer node's deliver.
        STAmount        saDeliverAct;

        WriteLog (lsTRACE, RippleCalc)
            << "calcNodeOfferRev: OFFER --> account:"
            << " uNode=" << uNode
            << " saRevDeliver=" << pnCur.saRevDeliver;

        terResult   = calcNodeDeliverRev (
            uNode,
            psCur,
            bMultiQuality,
            pnNxt.uAccountID,

            // The next node wants the current node to deliver this much:
            pnCur.saRevDeliver,
            saDeliverAct);
    }
    else
    {
        WriteLog (lsTRACE, RippleCalc)
            << "calcNodeOfferRev: OFFER --> offer: uNode=" << uNode;

        // Next is an offer. Deliver has already been resolved.
        terResult   = tesSUCCESS;
    }

    return terResult;
}

// Called to drive the from the first offer node in a chain.
//
// - Offer input is in issuer/limbo.
// - Current offers consumed.
//   - Current offer owners debited.
//   - Transfer fees credited to issuer.
//   - Payout to issuer or limbo.
// - Deliver is set without transfer fees.
TER RippleCalc::calcNodeOfferFwd (
    const unsigned int          uNode,              // 0 < uNode < uLast
    PathState&                  psCur,
    const bool                  bMultiQuality
)
{
    TER             terResult;
    PathState::Node& pnPrv = psCur.vpnNodes [uNode - 1];

    if (!!pnPrv.uAccountID)
    {
        // Previous is an account node, resolve its deliver.
        STAmount        saInAct;
        STAmount        saInFees;

        terResult   = calcNodeDeliverFwd (
                          uNode,
                          psCur,
                          bMultiQuality,
                          pnPrv.uAccountID,
                          pnPrv.saFwdDeliver, // Previous is sending this much.
                          saInAct,
                          saInFees);

        assert (tesSUCCESS != terResult ||
                pnPrv.saFwdDeliver == saInAct + saInFees);
    }
    else
    {
        // Previous is an offer. Deliver has already been resolved.
        terResult   = tesSUCCESS;
    }

    return terResult;

}

// Compute how much might flow for the node for the pass. Does not actually
// adjust balances.
//
// uQualityIn -> uQualityOut
//   saPrvReq -> saCurReq
//   sqPrvAct -> saCurAct
//
// This is a minimizing routine: moving in reverse it propagates the send limit
// to the sender, moving forward it propagates the actual send toward the
// receiver.
//
// This routine works backwards:
// - cur is the driver: it calculates previous wants based on previous credit
//   limits and current wants.
//
// This routine works forwards:
// - prv is the driver: it calculates current deliver based on previous delivery
//   limits and current wants.
//
// This routine is called one or two times for a node in a pass. If called once,
// it will work and set a rate.  If called again, the new work must not worsen
// the previous rate.
void RippleCalc::calcNodeRipple (
    const std::uint32_t uQualityIn,
    const std::uint32_t uQualityOut,
    const STAmount& saPrvReq,   // --> in limit including fees, <0 = unlimited
    const STAmount& saCurReq,   // --> out limit (driver)
    STAmount& saPrvAct,  // <-> in limit including achieved so far: <-- <= -->
    STAmount& saCurAct,  // <-> out limit including achieved : <-- <= -->
    std::uint64_t& uRateMax)
{
    WriteLog (lsTRACE, RippleCalc)
        << "calcNodeRipple>"
        << " uQualityIn=" << uQualityIn
        << " uQualityOut=" << uQualityOut
        << " saPrvReq=" << saPrvReq
        << " saCurReq=" << saCurReq
        << " saPrvAct=" << saPrvAct
        << " saCurAct=" << saCurAct;

    assert (saCurReq > zero); // FIXME: saCurReq was zero
    assert (saPrvReq.getCurrency () == saCurReq.getCurrency ());
    assert (saPrvReq.getCurrency () == saPrvAct.getCurrency ());
    assert (saPrvReq.getIssuer () == saPrvAct.getIssuer ());

    const bool      bPrvUnlimited   = saPrvReq < zero;
    const STAmount  saPrv           = bPrvUnlimited ? STAmount (saPrvReq)
        : saPrvReq - saPrvAct;
    const STAmount  saCur           = saCurReq - saCurAct;

    WriteLog (lsTRACE, RippleCalc)
        << "calcNodeRipple: "
        << " bPrvUnlimited=" << bPrvUnlimited
        << " saPrv=" << saPrv
        << " saCur=" << saCur;

    if (uQualityIn >= uQualityOut)
    {
        // No fee.
        WriteLog (lsTRACE, RippleCalc) << "calcNodeRipple: No fees";

        // Only process if we are not worsing previously processed.
        if (!uRateMax || STAmount::uRateOne <= uRateMax)
        {
            // Limit amount to transfer if need.
            STAmount saTransfer = bPrvUnlimited ? saCur
                : std::min (saPrv, saCur);

            // In reverse, we want to propagate the limited cur to prv and set
            // actual cur.
            //
            // In forward, we want to propagate the limited prv to cur and set
            // actual prv.
            saPrvAct += saTransfer;
            saCurAct += saTransfer;

            // If no rate limit, set rate limit to avoid combining with
            // something with a worse rate.
            if (!uRateMax)
                uRateMax = STAmount::uRateOne;
        }
    }
    else
    {
        // Fee.
        WriteLog (lsTRACE, RippleCalc) << "calcNodeRipple: Fee";

        std::uint64_t uRate = STAmount::getRate (
            STAmount (uQualityOut), STAmount (uQualityIn));

        if (!uRateMax || uRate <= uRateMax)
        {
            const uint160   uCurrencyID     = saCur.getCurrency ();
            const uint160   uCurIssuerID    = saCur.getIssuer ();

            // TODO(tom): what's this?
            auto someFee = STAmount::mulRound (
                saCur, uQualityOut, uCurrencyID, uCurIssuerID, true);

            STAmount saCurIn = STAmount::divRound (
                someFee, uQualityIn, uCurrencyID, uCurIssuerID, true);

            WriteLog (lsTRACE, RippleCalc)
                << "calcNodeRipple:"
                << " bPrvUnlimited=" << bPrvUnlimited
                << " saPrv=" << saPrv
                << " saCurIn=" << saCurIn;

            if (bPrvUnlimited || saCurIn <= saPrv)
            {
                // All of cur. Some amount of prv.
                saCurAct += saCur;
                saPrvAct += saCurIn;
                WriteLog (lsTRACE, RippleCalc)
                    << "calcNodeRipple:3c:"
                    << " saCurReq=" << saCurReq
                    << " saPrvAct=" << saPrvAct;
            }
            else
            {
                // TODO(tom): what's this?
                auto someFee = STAmount::mulRound (
                    saPrv, uQualityIn, uCurrencyID, uCurIssuerID, true);
                // A part of cur. All of prv. (cur as driver)
                STAmount    saCurOut    = STAmount::divRound (
                    someFee, uQualityOut, uCurrencyID, uCurIssuerID, true);
                WriteLog (lsTRACE, RippleCalc)
                    << "calcNodeRipple:4: saCurReq=" << saCurReq;

                saCurAct    += saCurOut;
                saPrvAct    = saPrvReq;

            }
            if (!uRateMax)
                uRateMax    = uRate;
        }
    }

    WriteLog (lsTRACE, RippleCalc)
        << "calcNodeRipple<"
        << " uQualityIn=" << uQualityIn
        << " uQualityOut=" << uQualityOut
        << " saPrvReq=" << saPrvReq
        << " saCurReq=" << saCurReq
        << " saPrvAct=" << saPrvAct
        << " saCurAct=" << saCurAct;
}

// Calculate saPrvRedeemReq, saPrvIssueReq, saPrvDeliver from saCur, based on
// required deliverable, propagate redeem, issue, and deliver requests to the
// previous node.
//
// Inflate amount requested by required fees.
// Reedems are limited based on IOUs previous has on hand.
// Issues are limited based on credit limits and amount owed.
//
// No account balance adjustments as we don't know how much is going to actually
// be pushed through yet.
//
// <-- tesSUCCESS or tecPATH_DRY

TER RippleCalc::calcNodeAccountRev (
    const unsigned int uNode, PathState& psCur, const bool bMultiQuality)
{
    TER                 terResult       = tesSUCCESS;
    const unsigned int  uLast           = psCur.vpnNodes.size () - 1;

    std::uint64_t           uRateMax        = 0;

    PathState::Node& pnPrv = psCur.vpnNodes[uNode ? uNode - 1 : 0];
    PathState::Node& pnCur = psCur.vpnNodes[uNode];
    PathState::Node& pnNxt = psCur.vpnNodes[uNode == uLast ? uLast : uNode + 1];

    // Current is allowed to redeem to next.
    const bool bPrvAccount = !uNode ||
        is_bit_set (pnPrv.uFlags, STPathElement::typeAccount);
    const bool bNxtAccount = uNode == uLast ||
        is_bit_set (pnNxt.uFlags, STPathElement::typeAccount);

    const uint160& uCurAccountID = pnCur.uAccountID;
    const uint160& uPrvAccountID = bPrvAccount ? pnPrv.uAccountID
        : uCurAccountID;
    const uint160& uNxtAccountID = bNxtAccount ? pnNxt.uAccountID
        : uCurAccountID;   // Offers are always issue.

    const uint160& uCurrencyID = pnCur.uCurrencyID;

    // XXX Don't look up quality for XRP
    const std::uint32_t uQualityIn = uNode
        ? mActiveLedger.rippleQualityIn (
            uCurAccountID, uPrvAccountID, uCurrencyID)
        : QUALITY_ONE;
    const std::uint32_t uQualityOut = (uNode != uLast)
        ? mActiveLedger.rippleQualityOut (
            uCurAccountID, uNxtAccountID, uCurrencyID)
        : QUALITY_ONE;

    // For bPrvAccount:
    // Previous account is owed.
    const STAmount saPrvOwed = (bPrvAccount && uNode)
        ? mActiveLedger.rippleOwed (uCurAccountID, uPrvAccountID, uCurrencyID)
        : STAmount (uCurrencyID, uCurAccountID);

    // Previous account may owe.
    const STAmount saPrvLimit = (bPrvAccount && uNode)
        ? mActiveLedger.rippleLimit (uCurAccountID, uPrvAccountID, uCurrencyID)
        : STAmount (uCurrencyID, uCurAccountID);

    // Next account is owed.
    const STAmount saNxtOwed = (bNxtAccount && uNode != uLast)
        ? mActiveLedger.rippleOwed (uCurAccountID, uNxtAccountID, uCurrencyID)
        : STAmount (uCurrencyID, uCurAccountID);

    WriteLog (lsTRACE, RippleCalc)
        << "calcNodeAccountRev>"
        << " uNode=%d/%d" << uNode << "/" << uLast
        << " uPrvAccountID="
        << RippleAddress::createHumanAccountID (uPrvAccountID)
        << " uCurAccountID="
        << RippleAddress::createHumanAccountID (uCurAccountID)
        << " uNxtAccountID="
        << RippleAddress::createHumanAccountID (uNxtAccountID)
        << " uCurrencyID="
        << STAmount::createHumanCurrency (uCurrencyID)
        << " uQualityIn=" << uQualityIn
        << " uQualityOut=" << uQualityOut
        << " saPrvOwed=" << saPrvOwed
        << " saPrvLimit=" << saPrvLimit;

    // Previous can redeem the owed IOUs it holds.
    const STAmount  saPrvRedeemReq  = (saPrvOwed > zero)
        ? saPrvOwed
        : STAmount (saPrvOwed.getCurrency (), saPrvOwed.getIssuer ());
    STAmount& saPrvRedeemAct = pnPrv.saRevRedeem;

    // Previous can issue up to limit minus whatever portion of limit already
    // used (not including redeemable amount).
    const STAmount  saPrvIssueReq = (saPrvOwed < zero)
        ? saPrvLimit + saPrvOwed : saPrvLimit;
    STAmount& saPrvIssueAct = pnPrv.saRevIssue;

    // For !bPrvAccount
    auto deliverCurrency = pnPrv.saRevDeliver.getCurrency ();
    const STAmount  saPrvDeliverReq (
        deliverCurrency, pnPrv.saRevDeliver.getIssuer (), -1);  // Unlimited.
    STAmount&       saPrvDeliverAct = pnPrv.saRevDeliver;

    // For bNxtAccount
    const STAmount& saCurRedeemReq  = pnCur.saRevRedeem;
    STAmount saCurRedeemAct (
        saCurRedeemReq.getCurrency (), saCurRedeemReq.getIssuer ());

    const STAmount& saCurIssueReq   = pnCur.saRevIssue;
    // Track progress.
    STAmount saCurIssueAct (
        saCurIssueReq.getCurrency (), saCurIssueReq.getIssuer ());

    // For !bNxtAccount
    const STAmount& saCurDeliverReq = pnCur.saRevDeliver;
    STAmount saCurDeliverAct (
        saCurDeliverReq.getCurrency (), saCurDeliverReq.getIssuer ());

    WriteLog (lsTRACE, RippleCalc)
        << "calcNodeAccountRev:"
        << " saPrvRedeemReq:" << saPrvRedeemReq
        << " saPrvIssueReq:" << saPrvIssueReq
        << " saPrvDeliverAct:" << saPrvDeliverAct
        << " saPrvDeliverReq:" << saPrvDeliverReq
        << " saCurRedeemReq:" << saCurRedeemReq
        << " saCurIssueReq:" << saCurIssueReq
        << " saNxtOwed:" << saNxtOwed;

    WriteLog (lsTRACE, RippleCalc) << psCur.getJson ();

    // Current redeem req can't be more than IOUs on hand.
    assert (!saCurRedeemReq || (-saNxtOwed) >= saCurRedeemReq);
    assert (!saCurIssueReq  // If not issuing, fine.
            || saNxtOwed >= zero
            // saNxtOwed >= 0: Sender not holding next IOUs, saNxtOwed < 0:
            // Sender holding next IOUs.
            || -saNxtOwed == saCurRedeemReq);
    // If issue req, then redeem req must consume all owed.

    if (!uNode)
    {
        // ^ --> ACCOUNT -->  account|offer
        // Nothing to do, there is no previous to adjust.

        nothing ();
    }
    else if (bPrvAccount && bNxtAccount)
    {
        if (uNode == uLast)
        {
            // account --> ACCOUNT --> $
            // Overall deliverable.
             // If previous is an account, limit.
            const STAmount saCurWantedReq = std::min (
                psCur.saOutReq - psCur.saOutAct, saPrvLimit + saPrvOwed);
            STAmount        saCurWantedAct (
                saCurWantedReq.getCurrency (), saCurWantedReq.getIssuer ());

            WriteLog (lsTRACE, RippleCalc)
                << "calcNodeAccountRev: account --> ACCOUNT --> $ :"
                << " saCurWantedReq=" << saCurWantedReq;

            // Calculate redeem
            if (saPrvRedeemReq) // Previous has IOUs to redeem.
            {
                // Redeem at 1:1

                saCurWantedAct = std::min (saPrvRedeemReq, saCurWantedReq);
                saPrvRedeemAct = saCurWantedAct;

                uRateMax = STAmount::uRateOne;

                WriteLog (lsTRACE, RippleCalc)
                    << "calcNodeAccountRev: Redeem at 1:1"
                    << " saPrvRedeemReq=" << saPrvRedeemReq
                    << " (available) saPrvRedeemAct=" << saPrvRedeemAct
                    << " uRateMax="
                    << STAmount::saFromRate (uRateMax).getText ();
            }
            else
            {
                saPrvRedeemAct.clear (saPrvRedeemReq);
            }

            // Calculate issuing.
            saPrvIssueAct.clear (saPrvIssueReq);

            if (saCurWantedReq != saCurWantedAct // Need more.
                && saPrvIssueReq)  // Will accept IOUs from prevous.
            {
                // Rate: quality in : 1.0

                // If we previously redeemed and this has a poorer rate, this
                // won't be included the current increment.
                calcNodeRipple (
                    uQualityIn, QUALITY_ONE, saPrvIssueReq, saCurWantedReq,
                    saPrvIssueAct, saCurWantedAct, uRateMax);

                WriteLog (lsTRACE, RippleCalc)
                    << "calcNodeAccountRev: Issuing: Rate: quality in : 1.0"
                    << " saPrvIssueAct:" << saPrvIssueAct
                    << " saCurWantedAct:" << saCurWantedAct;
            }

            if (!saCurWantedAct)
            {
                // Must have processed something.
                terResult   = tecPATH_DRY;
            }
        }
        else
        {
            // ^|account --> ACCOUNT --> account
            saPrvRedeemAct.clear (saPrvRedeemReq);
            saPrvIssueAct.clear (saPrvIssueReq);

            // redeem (part 1) -> redeem
            if (saCurRedeemReq      // Next wants IOUs redeemed.
                && saPrvRedeemReq)  // Previous has IOUs to redeem.
            {
                // Rate : 1.0 : quality out
                calcNodeRipple (
                    QUALITY_ONE, uQualityOut, saPrvRedeemReq, saCurRedeemReq,
                    saPrvRedeemAct, saCurRedeemAct, uRateMax);

                WriteLog (lsTRACE, RippleCalc)
                    << "calcNodeAccountRev: Rate : 1.0 : quality out"
                    << " saPrvRedeemAct:" << saPrvRedeemAct
                    << " saCurRedeemAct:" << saCurRedeemAct;
            }

            // issue (part 1) -> redeem
            if (saCurRedeemReq != saCurRedeemAct
                // Next wants more IOUs redeemed.
                && saPrvRedeemAct == saPrvRedeemReq)
                // Previous has no IOUs to redeem remaining.
            {
                // Rate: quality in : quality out
                calcNodeRipple (
                    uQualityIn, uQualityOut, saPrvIssueReq, saCurRedeemReq,
                    saPrvIssueAct, saCurRedeemAct, uRateMax);

                WriteLog (lsTRACE, RippleCalc)
                    << "calcNodeAccountRev: Rate: quality in : quality out:"
                    << " saPrvIssueAct:" << saPrvIssueAct
                    << " saCurRedeemAct:" << saCurRedeemAct;
            }

            // redeem (part 2) -> issue.
            if (saCurIssueReq   // Next wants IOUs issued.
                && saCurRedeemAct == saCurRedeemReq
                // Can only issue if completed redeeming.
                && saPrvRedeemAct != saPrvRedeemReq)
                // Did not complete redeeming previous IOUs.
            {
                // Rate : 1.0 : transfer_rate
                calcNodeRipple (
                    QUALITY_ONE,
                    mActiveLedger.rippleTransferRate (uCurAccountID),
                    saPrvRedeemReq, saCurIssueReq, saPrvRedeemAct,
                    saCurIssueAct, uRateMax);

                WriteLog (lsDEBUG, RippleCalc)
                    << "calcNodeAccountRev: Rate : 1.0 : transfer_rate:"
                    << " saPrvRedeemAct:" << saPrvRedeemAct
                    << " saCurIssueAct:" << saCurIssueAct;
            }

            // issue (part 2) -> issue
            if (saCurIssueReq != saCurIssueAct
                // Need wants more IOUs issued.
                && saCurRedeemAct == saCurRedeemReq
                // Can only issue if completed redeeming.
                && saPrvRedeemReq == saPrvRedeemAct
                // Previously redeemed all owed IOUs.
                && saPrvIssueReq)
                // Previous can issue.
            {
                // Rate: quality in : 1.0
                calcNodeRipple (
                    uQualityIn, QUALITY_ONE, saPrvIssueReq, saCurIssueReq,
                    saPrvIssueAct, saCurIssueAct, uRateMax);

                WriteLog (lsTRACE, RippleCalc)
                    << "calcNodeAccountRev: Rate: quality in : 1.0:"
                    << " saPrvIssueAct:" << saPrvIssueAct
                    << " saCurIssueAct:" << saCurIssueAct;
            }

            if (!saCurRedeemAct && !saCurIssueAct)
            {
                // Did not make progress.
                terResult   = tecPATH_DRY;
            }

            WriteLog (lsTRACE, RippleCalc)
                << "calcNodeAccountRev: ^|account --> ACCOUNT --> account :"
                << " saCurRedeemReq:" << saCurRedeemReq
                << " saCurIssueReq:" << saCurIssueReq
                << " saPrvOwed:" << saPrvOwed
                << " saCurRedeemAct:" << saCurRedeemAct
                << " saCurIssueAct:" << saCurIssueAct;
        }
    }
    else if (bPrvAccount && !bNxtAccount)
    {
        // account --> ACCOUNT --> offer
        // Note: deliver is always issue as ACCOUNT is the issuer for the offer
        // input.
        WriteLog (lsTRACE, RippleCalc)
            << "calcNodeAccountRev: account --> ACCOUNT --> offer";

        saPrvRedeemAct.clear (saPrvRedeemReq);
        saPrvIssueAct.clear (saPrvIssueReq);

        // redeem -> deliver/issue.
        if (saPrvOwed > zero                    // Previous has IOUs to redeem.
            && saCurDeliverReq)                 // Need some issued.
        {
            // Rate : 1.0 : transfer_rate
            calcNodeRipple (
                QUALITY_ONE, mActiveLedger.rippleTransferRate (uCurAccountID),
                saPrvRedeemReq, saCurDeliverReq, saPrvRedeemAct,
                saCurDeliverAct, uRateMax);
        }

        // issue -> deliver/issue
        if (saPrvRedeemReq == saPrvRedeemAct   // Previously redeemed all owed.
            && saCurDeliverReq != saCurDeliverAct)  // Still need some issued.
        {
            // Rate: quality in : 1.0
            calcNodeRipple (
                uQualityIn, QUALITY_ONE, saPrvIssueReq, saCurDeliverReq,
                saPrvIssueAct, saCurDeliverAct, uRateMax);
        }

        if (!saCurDeliverAct)
        {
            // Must want something.
            terResult   = tecPATH_DRY;
        }

        WriteLog (lsTRACE, RippleCalc)
            << "calcNodeAccountRev: "
            << " saCurDeliverReq:" << saCurDeliverReq
            << " saCurDeliverAct:" << saCurDeliverAct
            << " saPrvOwed:" << saPrvOwed;
    }
    else if (!bPrvAccount && bNxtAccount)
    {
        if (uNode == uLast)
        {
            // offer --> ACCOUNT --> $
            // Previous is an offer, no limit: redeem own IOUs.
            const STAmount& saCurWantedReq  = psCur.saOutReq - psCur.saOutAct;
            STAmount saCurWantedAct (
                saCurWantedReq.getCurrency (), saCurWantedReq.getIssuer ());

            WriteLog (lsTRACE, RippleCalc)
                << "calcNodeAccountRev: offer --> ACCOUNT --> $ :"
                << " saCurWantedReq:" << saCurWantedReq
                << " saOutAct:" << psCur.saOutAct
                << " saOutReq:" << psCur.saOutReq;

            if (saCurWantedReq <= zero)
            {
                // TEMPORARY emergency fix
                WriteLog (lsFATAL, RippleCalc) << "CurWantReq was not positive";
                return tefEXCEPTION;
            }

            assert (saCurWantedReq > zero); // FIXME: We got one of these
            // TODO(tom): can only be a race condition if true!

            // Rate: quality in : 1.0
            calcNodeRipple (
                uQualityIn, QUALITY_ONE, saPrvDeliverReq, saCurWantedReq,
                saPrvDeliverAct, saCurWantedAct, uRateMax);

            if (!saCurWantedAct)
            {
                // Must have processed something.
                terResult   = tecPATH_DRY;
            }

            WriteLog (lsTRACE, RippleCalc)
                << "calcNodeAccountRev:"
                << " saPrvDeliverAct:" << saPrvDeliverAct
                << " saPrvDeliverReq:" << saPrvDeliverReq
                << " saCurWantedAct:" << saCurWantedAct
                << " saCurWantedReq:" << saCurWantedReq;
        }
        else
        {
            // offer --> ACCOUNT --> account
            // Note: offer is always delivering(redeeming) as account is issuer.
            WriteLog (lsTRACE, RippleCalc)
                << "calcNodeAccountRev: offer --> ACCOUNT --> account :"
                << " saCurRedeemReq:" << saCurRedeemReq
                << " saCurIssueReq:" << saCurIssueReq;

            // deliver -> redeem
            if (saCurRedeemReq)  // Next wants us to redeem.
            {
                // Rate : 1.0 : quality out
                calcNodeRipple (
                    QUALITY_ONE, uQualityOut, saPrvDeliverReq, saCurRedeemReq,
                    saPrvDeliverAct, saCurRedeemAct, uRateMax);
            }

            // deliver -> issue.
            if (saCurRedeemReq == saCurRedeemAct
                // Can only issue if previously redeemed all.
                && saCurIssueReq)
                // Need some issued.
            {
                // Rate : 1.0 : transfer_rate
                calcNodeRipple (
                    QUALITY_ONE,
                    mActiveLedger.rippleTransferRate (uCurAccountID),
                    saPrvDeliverReq, saCurIssueReq, saPrvDeliverAct,
                    saCurIssueAct, uRateMax);
            }

            WriteLog (lsTRACE, RippleCalc)
                << "calcNodeAccountRev:"
                << " saCurRedeemAct:" << saCurRedeemAct
                << " saCurRedeemReq:" << saCurRedeemReq
                << " saPrvDeliverAct:" << saPrvDeliverAct
                << " saCurIssueReq:" << saCurIssueReq;

            if (!saPrvDeliverAct)
            {
                // Must want something.
                terResult   = tecPATH_DRY;
            }
        }
    }
    else
    {
        // offer --> ACCOUNT --> offer
        // deliver/redeem -> deliver/issue.
        WriteLog (lsTRACE, RippleCalc)
            << "calcNodeAccountRev: offer --> ACCOUNT --> offer";

        // Rate : 1.0 : transfer_rate
        calcNodeRipple (
            QUALITY_ONE, mActiveLedger.rippleTransferRate (uCurAccountID),
            saPrvDeliverReq, saCurDeliverReq, saPrvDeliverAct,
            saCurDeliverAct, uRateMax);

        if (!saCurDeliverAct)
        {
            // Must want something.
            terResult   = tecPATH_DRY;
        }
    }

    return terResult;
}

// The reverse pass has been narrowing by credit available and inflating by fees
// as it worked backwards.  Now, for the current account node, take the actual
// amount from previous and adjust forward balances.
//
// Perform balance adjustments between previous and current node.
// - The previous node: specifies what to push through to current.
// - All of previous output is consumed.
//
// Then, compute current node's output for next node.
// - Current node: specify what to push through to next.
// - Output to next node is computed as input minus quality or transfer fee.
// - If next node is an offer and output is non-XRP then we are the issuer and
//   do not need to push funds.
// - If next node is an offer and output is XRP then we need to deliver funds to
//   limbo.
TER RippleCalc::calcNodeAccountFwd (
    const unsigned int uNode,   // 0 <= uNode <= uLast
    PathState& psCur,
    const bool bMultiQuality)
{
    TER                 terResult   = tesSUCCESS;
    const unsigned int  uLast       = psCur.vpnNodes.size () - 1;

    std::uint64_t       uRateMax    = 0;

    PathState::Node& pnPrv = psCur.vpnNodes[uNode ? uNode - 1 : 0];
    PathState::Node& pnCur = psCur.vpnNodes[uNode];
    PathState::Node& pnNxt = psCur.vpnNodes[uNode == uLast ? uLast : uNode + 1];

    const bool bPrvAccount
        = is_bit_set (pnPrv.uFlags, STPathElement::typeAccount);
    const bool bNxtAccount
        = is_bit_set (pnNxt.uFlags, STPathElement::typeAccount);

    const uint160& uCurAccountID = pnCur.uAccountID;
    const uint160& uPrvAccountID
        = bPrvAccount ? pnPrv.uAccountID : uCurAccountID;
    // Offers are always issue.
    const uint160& uNxtAccountID
            = bNxtAccount ? pnNxt.uAccountID : uCurAccountID;

    const uint160&  uCurrencyID     = pnCur.uCurrencyID;

    std::uint32_t uQualityIn = uNode
        ? mActiveLedger.rippleQualityIn (
            uCurAccountID, uPrvAccountID, uCurrencyID)
        : QUALITY_ONE;
    std::uint32_t  uQualityOut = (uNode == uLast)
        ? mActiveLedger.rippleQualityOut (
            uCurAccountID, uNxtAccountID, uCurrencyID)
        : QUALITY_ONE;

    // When looking backward (prv) for req we care about what we just
    // calculated: use fwd.
    // When looking forward (cur) for req we care about what was desired: use
    // rev.

    // For bNxtAccount
    const STAmount& saPrvRedeemReq  = pnPrv.saFwdRedeem;
    STAmount saPrvRedeemAct (
        saPrvRedeemReq.getCurrency (), saPrvRedeemReq.getIssuer ());

    const STAmount& saPrvIssueReq   = pnPrv.saFwdIssue;
    STAmount saPrvIssueAct (
        saPrvIssueReq.getCurrency (), saPrvIssueReq.getIssuer ());

    // For !bPrvAccount
    const STAmount& saPrvDeliverReq = pnPrv.saFwdDeliver;
    STAmount saPrvDeliverAct (
        saPrvDeliverReq.getCurrency (), saPrvDeliverReq.getIssuer ());

    // For bNxtAccount
    const STAmount& saCurRedeemReq  = pnCur.saRevRedeem;
    STAmount& saCurRedeemAct  = pnCur.saFwdRedeem;

    const STAmount& saCurIssueReq   = pnCur.saRevIssue;
    STAmount& saCurIssueAct   = pnCur.saFwdIssue;

    // For !bNxtAccount
    const STAmount& saCurDeliverReq = pnCur.saRevDeliver;
    STAmount& saCurDeliverAct = pnCur.saFwdDeliver;

    // For !uNode
    // Report how much pass sends.
    STAmount& saCurSendMaxPass = psCur.saInPass;

    WriteLog (lsTRACE, RippleCalc)
        << "calcNodeAccountFwd> "
        << "uNode=" << uNode << "/" << uLast
        << " saPrvRedeemReq:" << saPrvRedeemReq
        << " saPrvIssueReq:" << saPrvIssueReq
        << " saPrvDeliverReq:" << saPrvDeliverReq
        << " saCurRedeemReq:" << saCurRedeemReq
        << " saCurIssueReq:" << saCurIssueReq
        << " saCurDeliverReq:" << saCurDeliverReq;

    // Ripple through account.

    if (bPrvAccount && bNxtAccount)
    {
        // Next is an account, must be rippling.

        if (!uNode)
        {
            // ^ --> ACCOUNT --> account

            // For the first node, calculate amount to ripple based on what is
            // available.
            saCurRedeemAct      = saCurRedeemReq;

            if (psCur.saInReq >= zero)
            {
                // Limit by send max.
                saCurRedeemAct = std::min (
                    saCurRedeemAct, psCur.saInReq - psCur.saInAct);
            }

            saCurSendMaxPass    = saCurRedeemAct;

            saCurIssueAct = saCurRedeemAct == saCurRedeemReq
                // Fully redeemed.
                ? saCurIssueReq : STAmount (saCurIssueReq);

            if (!!saCurIssueAct && psCur.saInReq >= zero)
            {
                // Limit by send max.
                saCurIssueAct = std::min (
                    saCurIssueAct,
                    psCur.saInReq - psCur.saInAct - saCurRedeemAct);
            }

            saCurSendMaxPass += saCurIssueAct;

            WriteLog (lsTRACE, RippleCalc)
                << "calcNodeAccountFwd: ^ --> ACCOUNT --> account :"
                << " saInReq=" << psCur.saInReq
                << " saInAct=" << psCur.saInAct
                << " saCurRedeemAct:" << saCurRedeemAct
                << " saCurIssueReq:" << saCurIssueReq
                << " saCurIssueAct:" << saCurIssueAct
                << " saCurSendMaxPass:" << saCurSendMaxPass;
        }
        else if (uNode == uLast)
        {
            // account --> ACCOUNT --> $
            WriteLog (lsTRACE, RippleCalc)
                << "calcNodeAccountFwd: account --> ACCOUNT --> $ :"
                << " uPrvAccountID="
                << RippleAddress::createHumanAccountID (uPrvAccountID)
                << " uCurAccountID="
                << RippleAddress::createHumanAccountID (uCurAccountID)
                << " saPrvRedeemReq:" << saPrvRedeemReq
                << " saPrvIssueReq:" << saPrvIssueReq;

            // Last node. Accept all funds. Calculate amount actually to credit.

            STAmount& saCurReceive = psCur.saOutPass;

            STAmount saIssueCrd = uQualityIn >= QUALITY_ONE
                    ? saPrvIssueReq  // No fee.
                    : STAmount::mulRound (
                          saPrvIssueReq,
                          STAmount (CURRENCY_ONE, ACCOUNT_ONE, uQualityIn, -9),
                          true); // Amount to credit.

            // Amount to credit. Credit for less than received as a surcharge.
            saCurReceive    = saPrvRedeemReq + saIssueCrd;

            if (saCurReceive)
            {
                // Actually receive.
                terResult = mActiveLedger.rippleCredit (
                    uPrvAccountID, uCurAccountID,
                    saPrvRedeemReq + saPrvIssueReq, false);
            }
            else
            {
                // After applying quality, total payment was microscopic.
                terResult   = tecPATH_DRY;
            }
        }
        else
        {
            // account --> ACCOUNT --> account
            WriteLog (lsTRACE, RippleCalc)
                << "calcNodeAccountFwd: account --> ACCOUNT --> account";

            saCurRedeemAct.clear (saCurRedeemReq);
            saCurIssueAct.clear (saCurIssueReq);

            // Previous redeem part 1: redeem -> redeem
            if (saPrvRedeemReq && saCurRedeemReq)
                // Previous wants to redeem.
            {
                // Rate : 1.0 : quality out
                calcNodeRipple (
                    QUALITY_ONE, uQualityOut, saPrvRedeemReq, saCurRedeemReq,
                    saPrvRedeemAct, saCurRedeemAct, uRateMax);
            }

            // Previous issue part 1: issue -> redeem
            if (saPrvIssueReq != saPrvIssueAct
                // Previous wants to issue.
                && saCurRedeemReq != saCurRedeemAct)
                // Current has more to redeem to next.
            {
                // Rate: quality in : quality out
                calcNodeRipple (
                    uQualityIn, uQualityOut, saPrvIssueReq, saCurRedeemReq,
                    saPrvIssueAct, saCurRedeemAct, uRateMax);
            }

            // Previous redeem part 2: redeem -> issue.
            if (saPrvRedeemReq != saPrvRedeemAct
                // Previous still wants to redeem.
                && saCurRedeemReq == saCurRedeemAct
                // Current redeeming is done can issue.
                && saCurIssueReq)
                // Current wants to issue.
            {
                // Rate : 1.0 : transfer_rate
                calcNodeRipple (
                    QUALITY_ONE,
                    mActiveLedger.rippleTransferRate (uCurAccountID),
                    saPrvRedeemReq, saCurIssueReq, saPrvRedeemAct,
                    saCurIssueAct, uRateMax);
            }

            // Previous issue part 2 : issue -> issue
            if (saPrvIssueReq != saPrvIssueAct
                // Previous wants to issue.
                && saCurRedeemReq == saCurRedeemAct
                // Current redeeming is done can issue.
                && saCurIssueReq)
                // Current wants to issue.
            {
                // Rate: quality in : 1.0
                calcNodeRipple (
                    uQualityIn, QUALITY_ONE, saPrvIssueReq, saCurIssueReq,
                    saPrvIssueAct, saCurIssueAct, uRateMax);
            }

            STAmount saProvide = saCurRedeemAct + saCurIssueAct;

            // Adjust prv --> cur balance : take all inbound
            terResult   = saProvide
                ? mActiveLedger.rippleCredit (uPrvAccountID, uCurAccountID,
                                          saPrvRedeemReq + saPrvIssueReq, false)
                : tecPATH_DRY;
        }
    }
    else if (bPrvAccount && !bNxtAccount)
    {
        // Current account is issuer to next offer.
        // Determine deliver to offer amount.
        // Don't adjust outbound balances- keep funds with issuer as limbo.
        // If issuer hold's an offer owners inbound IOUs, there is no fee and
        // redeem/issue will transparently happen.

        if (uNode)
        {
            // Non-XRP, current node is the issuer.
            WriteLog (lsTRACE, RippleCalc)
                << "calcNodeAccountFwd: account --> ACCOUNT --> offer";

            saCurDeliverAct.clear (saCurDeliverReq);

            // redeem -> issue/deliver.
            // Previous wants to redeem.
            // Current is issuing to an offer so leave funds in account as
            // "limbo".
            if (saPrvRedeemReq)
                // Previous wants to redeem.
            {
                // Rate : 1.0 : transfer_rate
                // XXX Is having the transfer rate here correct?
                calcNodeRipple (
                    QUALITY_ONE,
                    mActiveLedger.rippleTransferRate (uCurAccountID),
                    saPrvRedeemReq, saCurDeliverReq, saPrvRedeemAct,
                    saCurDeliverAct, uRateMax);
            }

            // issue -> issue/deliver
            if (saPrvRedeemReq == saPrvRedeemAct
                // Previous done redeeming: Previous has no IOUs.
                && saPrvIssueReq)
                // Previous wants to issue. To next must be ok.
            {
                // Rate: quality in : 1.0
                calcNodeRipple (
                    uQualityIn, QUALITY_ONE, saPrvIssueReq, saCurDeliverReq,
                    saPrvIssueAct, saCurDeliverAct, uRateMax);
            }

            // Adjust prv --> cur balance : take all inbound
            terResult   = saCurDeliverAct
                ? mActiveLedger.rippleCredit (uPrvAccountID, uCurAccountID,
                                          saPrvRedeemReq + saPrvIssueReq, false)
                : tecPATH_DRY;  // Didn't actually deliver anything.
        }
        else
        {
            // Delivering amount requested from downstream.
            saCurDeliverAct = saCurDeliverReq;

            // If limited, then limit by send max and available.
            if (psCur.saInReq >= zero)
            {
                // Limit by send max.
                saCurDeliverAct = std::min (saCurDeliverAct,
                                            psCur.saInReq - psCur.saInAct);

                // Limit XRP by available. No limit for non-XRP as issuer.
                if (uCurrencyID.isZero ())
                    saCurDeliverAct = std::min (
                        saCurDeliverAct,
                        mActiveLedger.accountHolds (uCurAccountID, CURRENCY_XRP,
                                                ACCOUNT_XRP));

            }

            // Record amount sent for pass.
            saCurSendMaxPass    = saCurDeliverAct;

            if (!saCurDeliverAct)
            {
                terResult   = tecPATH_DRY;
            }
            else if (!!uCurrencyID)
            {
                // Non-XRP, current node is the issuer.
                // We could be delivering to multiple accounts, so we don't know
                // which ripple balance will be adjusted.  Assume just issuing.

                WriteLog (lsTRACE, RippleCalc)
                    << "calcNodeAccountFwd: ^ --> ACCOUNT -- !XRP --> offer";

                // As the issuer, would only issue.
                // Don't need to actually deliver. As from delivering leave in
                // the issuer as limbo.
                nothing ();
            }
            else
            {
                WriteLog (lsTRACE, RippleCalc)
                    << "calcNodeAccountFwd: ^ --> ACCOUNT -- XRP --> offer";

                // Deliver XRP to limbo.
                terResult = mActiveLedger.accountSend (
                    uCurAccountID, ACCOUNT_XRP, saCurDeliverAct);
            }
        }
    }
    else if (!bPrvAccount && bNxtAccount)
    {
        if (uNode == uLast)
        {
            // offer --> ACCOUNT --> $
            WriteLog (lsTRACE, RippleCalc)
                << "calcNodeAccountFwd: offer --> ACCOUNT --> $ : "
                << saPrvDeliverReq;

            STAmount& saCurReceive = psCur.saOutPass;

            // Amount to credit.
            saCurReceive    = saPrvDeliverReq;

            // No income balance adjustments necessary.  The paying side inside
            // the offer paid to this account.
        }
        else
        {
            // offer --> ACCOUNT --> account
            WriteLog (lsTRACE, RippleCalc)
                << "calcNodeAccountFwd: offer --> ACCOUNT --> account";

            saCurRedeemAct.clear (saCurRedeemReq);
            saCurIssueAct.clear (saCurIssueReq);

            // deliver -> redeem
            if (saPrvDeliverReq && saCurRedeemReq)
                // Previous wants to deliver and can current redeem.
            {
                // Rate : 1.0 : quality out
                calcNodeRipple (
                    QUALITY_ONE, uQualityOut, saPrvDeliverReq, saCurRedeemReq,
                    saPrvDeliverAct, saCurRedeemAct, uRateMax);
            }

            // deliver -> issue
            // Wants to redeem and current would and can issue.
            if (saPrvDeliverReq != saPrvDeliverAct
                // Previous still wants to deliver.
                && saCurRedeemReq == saCurRedeemAct
                // Current has more to redeem to next.
                && saCurIssueReq)
                // Current wants issue.
            {
                // Rate : 1.0 : transfer_rate
                calcNodeRipple (
                    QUALITY_ONE,
                    mActiveLedger.rippleTransferRate (uCurAccountID),
                    saPrvDeliverReq, saCurIssueReq, saPrvDeliverAct,
                    saCurIssueAct, uRateMax);
            }

            // No income balance adjustments necessary.  The paying side inside
            // the offer paid and the next link will receive.
            STAmount saProvide = saCurRedeemAct + saCurIssueAct;

            if (!saProvide)
                terResult = tecPATH_DRY;
        }
    }
    else
    {
        // offer --> ACCOUNT --> offer
        // deliver/redeem -> deliver/issue.
        WriteLog (lsTRACE, RippleCalc)
            << "calcNodeAccountFwd: offer --> ACCOUNT --> offer";

        saCurDeliverAct.clear (saCurDeliverReq);

        if (saPrvDeliverReq
            // Previous wants to deliver
            && saCurIssueReq)
            // Current wants issue.
        {
            // Rate : 1.0 : transfer_rate
            calcNodeRipple (
                QUALITY_ONE, mActiveLedger.rippleTransferRate (uCurAccountID),
                saPrvDeliverReq, saCurDeliverReq, saPrvDeliverAct,
                saCurDeliverAct, uRateMax);
        }

        // No income balance adjustments necessary.  The paying side inside the
        // offer paid and the next link will receive.
        if (!saCurDeliverAct)
            terResult   = tecPATH_DRY;
    }

    return terResult;
}

TER RippleCalc::calcNodeFwd (
    const unsigned int uNode, PathState& psCur, const bool bMultiQuality)
{
    const PathState::Node& pnCur = psCur.vpnNodes[uNode];
    const bool bCurAccount
        = is_bit_set (pnCur.uFlags,  STPathElement::typeAccount);

    WriteLog (lsTRACE, RippleCalc)
        << "calcNodeFwd> uNode=" << uNode;

    TER terResult = bCurAccount
        ? calcNodeAccountFwd (uNode, psCur, bMultiQuality)
        : calcNodeOfferFwd (uNode, psCur, bMultiQuality);

    if (tesSUCCESS == terResult && uNode + 1 != psCur.vpnNodes.size ())
        terResult   = calcNodeFwd (uNode + 1, psCur, bMultiQuality);

    if (tesSUCCESS == terResult && (!psCur.saInPass || !psCur.saOutPass))
        terResult = tecPATH_DRY;

    WriteLog (lsTRACE, RippleCalc)
        << "calcNodeFwd<"
        << " uNode:" << uNode
        << " terResult:" << terResult;

    return terResult;
}

// Calculate a node and its previous nodes.
//
// From the destination work in reverse towards the source calculating how much
// must be asked for.
//
// Then work forward, figuring out how much can actually be delivered.
// <-- terResult: tesSUCCESS or tecPATH_DRY
// <-> pnNodes:
//     --> [end]saWanted.mAmount
//     --> [all]saWanted.mCurrency
//     --> [all]saAccount
//     <-> [0]saWanted.mAmount : --> limit, <-- actual
TER RippleCalc::calcNodeRev (
    const unsigned int uNode, PathState& psCur, const bool bMultiQuality)
{
    PathState::Node& pnCur = psCur.vpnNodes[uNode];
    bool const bCurAccount
        = is_bit_set (pnCur.uFlags,  STPathElement::typeAccount);
    TER terResult;

    // Do current node reverse.
    const uint160&  uCurIssuerID    = pnCur.uIssuerID;
    STAmount& saTransferRate  = pnCur.saTransferRate;

    saTransferRate = STAmount::saFromRate (
        mActiveLedger.rippleTransferRate (uCurIssuerID));

    WriteLog (lsTRACE, RippleCalc)
        << "calcNodeRev>"
        << " uNode=" << uNode
        << " bCurAccount=" << bCurAccount
        << " uIssuerID=" << RippleAddress::createHumanAccountID (uCurIssuerID)
        << " saTransferRate=" << saTransferRate;

    terResult = bCurAccount
        ? calcNodeAccountRev (uNode, psCur, bMultiQuality)
        : calcNodeOfferRev (uNode, psCur, bMultiQuality);

    // Do previous.
    if (tesSUCCESS != terResult)
    {
        // Error, don't continue.
        nothing ();
    }
    else if (uNode)
    {
        // Continue in reverse.
        terResult = calcNodeRev (uNode - 1, psCur, bMultiQuality);
    }

    WriteLog (lsTRACE, RippleCalc)
        << "calcNodeRev< "
        << "uNode=" << uNode
        << " terResult=%s" << transToken (terResult)
        << "/" << terResult;

    return terResult;
}

// Calculate the next increment of a path.
//
// The increment is what can satisfy a portion or all of the requested output at
// the best quality.
//
// <-- psCur.uQuality

void RippleCalc::pathNext (
    PathState::ref psrCur, const bool bMultiQuality,
    const LedgerEntrySet& lesCheckpoint, LedgerEntrySet& lesCurrent)
{
    // The next state is what is available in preference order.
    // This is calculated when referenced accounts changed.
    const unsigned int  uLast           = psrCur->vpnNodes.size () - 1;

    psrCur->bConsumed   = false;

    // YYY This clearing should only be needed for nice logging.
    psrCur->saInPass = STAmount (
        psrCur->saInReq.getCurrency (), psrCur->saInReq.getIssuer ());
    psrCur->saOutPass = STAmount (
        psrCur->saOutReq.getCurrency (), psrCur->saOutReq.getIssuer ());

    psrCur->vUnfundedBecame.clear ();
    psrCur->umReverse.clear ();

    WriteLog (lsTRACE, RippleCalc)
        << "pathNext: Path In: " << psrCur->getJson ();

    assert (psrCur->vpnNodes.size () >= 2);

    lesCurrent  = lesCheckpoint.duplicate ();  // Restore from checkpoint.

    for (unsigned int uIndex = psrCur->vpnNodes.size (); uIndex--;)
    {
        PathState::Node& pnCur   = psrCur->vpnNodes[uIndex];

        pnCur.saRevRedeem.clear ();
        pnCur.saRevIssue.clear ();
        pnCur.saRevDeliver.clear ();
        pnCur.saFwdDeliver.clear ();
    }

    psrCur->terStatus = calcNodeRev (uLast, *psrCur, bMultiQuality);

    WriteLog (lsTRACE, RippleCalc)
        << "pathNext: Path after reverse: " << psrCur->getJson ();

    if (tesSUCCESS == psrCur->terStatus)
    {
        // Do forward.
        lesCurrent = lesCheckpoint.duplicate ();   // Restore from checkpoint.

        psrCur->terStatus = calcNodeFwd (0, *psrCur, bMultiQuality);
    }

    if (tesSUCCESS == psrCur->terStatus)
    {
        CondLog (!psrCur->saInPass || !psrCur->saOutPass, lsDEBUG, RippleCalc)
            << "pathNext: Error calcNodeFwd reported success for nothing:"
            << " saOutPass=" << psrCur->saOutPass
            << " saInPass=" << psrCur->saInPass;

        if (!psrCur->saOutPass || !psrCur->saInPass)
            throw std::runtime_error ("Made no progress.");

        // Calculate relative quality.
        psrCur->uQuality = STAmount::getRate (
            psrCur->saOutPass, psrCur->saInPass);

        WriteLog (lsTRACE, RippleCalc)
            << "pathNext: Path after forward: " << psrCur->getJson ();
    }
    else
    {
        psrCur->uQuality    = 0;
    }
}

// <-- TER: Only returns tepPATH_PARTIAL if !bPartialPayment.
TER RippleCalc::rippleCalc (
    // Compute paths vs this ledger entry set.  Up to caller to actually apply
    // to ledger.
    LedgerEntrySet& activeLedger,
    // <-> --> = Fee already applied to src balance.

    STAmount&       saMaxAmountAct,         // <-- The computed input amount.
    STAmount&       saDstAmountAct,         // <-- The computed output amount.
    std::vector<PathState::pointer>&  vpsExpanded,
    // Issuer:
    //      XRP: ACCOUNT_XRP
    //  non-XRP: uSrcAccountID (for any issuer) or another account with trust
    //           node.
    const STAmount&     saMaxAmountReq,             // --> -1 = no limit.

    // Issuer:
    //      XRP: ACCOUNT_XRP
    //  non-XRP: uDstAccountID (for any issuer) or another account with trust
    //           node.
    const STAmount&     saDstAmountReq,

    const uint160&      uDstAccountID,
    const uint160&      uSrcAccountID,
    const STPathSet&    spsPaths,
    const bool          bPartialPayment,
    const bool          bLimitQuality,
    const bool          bNoRippleDirect,
    const bool          bStandAlone,
    // True, not to delete unfundeds.
    const bool          bOpenLedger)
{
    assert (activeLedger.isValid ());
    RippleCalc  rc (activeLedger, bOpenLedger);

    WriteLog (lsTRACE, RippleCalc)
        << "rippleCalc>"
        << " saMaxAmountReq:" << saMaxAmountReq
        << " saDstAmountReq:" << saDstAmountReq;

    TER         terResult   = temUNCERTAIN;

    // YYY Might do basic checks on src and dst validity as per doPayment.

    if (bNoRippleDirect && spsPaths.isEmpty ())
    {
        WriteLog (lsDEBUG, RippleCalc)
            << "rippleCalc: Invalid transaction:"
            << "No paths and direct ripple not allowed.";

        return temRIPPLE_EMPTY;
    }

    // Incrementally search paths.

    // bNoRippleDirect is a slight misnomer, it really means make no ripple
    // default path.
    if (!bNoRippleDirect)
    {
        // Build a default path.  Use saDstAmountReq and saMaxAmountReq to imply
        // nodes.
        // XXX Might also make a XRP bridge by default.

        PathState::pointer pspDirect = boost::make_shared<PathState> (
            saDstAmountReq, saMaxAmountReq);

        if (!pspDirect)
            return temUNKNOWN;

        pspDirect->setExpanded (
            activeLedger, STPath (), uDstAccountID, uSrcAccountID);

        if (tesSUCCESS == pspDirect->terStatus)
           pspDirect->checkNoRipple (uDstAccountID, uSrcAccountID);

        pspDirect->setIndex (vpsExpanded.size ());

        WriteLog (lsDEBUG, RippleCalc)
            << "rippleCalc: Build direct:"
            << " status: " << transToken (pspDirect->terStatus);

        // Return if malformed.
        if (isTemMalformed (pspDirect->terStatus))
            return pspDirect->terStatus;

        if (tesSUCCESS == pspDirect->terStatus)
        {
            // Had a success.
            terResult   = tesSUCCESS;

            vpsExpanded.push_back (pspDirect);
        }
        else if (terNO_LINE != pspDirect->terStatus)
        {
            terResult   = pspDirect->terStatus;
        }
    }

    WriteLog (lsTRACE, RippleCalc)
        << "rippleCalc: Paths in set: " << spsPaths.size ();

    int iIndex  = 0;
    for (auto const& spPath: spsPaths)
    {
        PathState::pointer pspExpanded = boost::make_shared<PathState> (
            saDstAmountReq, saMaxAmountReq);

        if (!pspExpanded)
            return temUNKNOWN;

        WriteLog (lsTRACE, RippleCalc)
            << "rippleCalc: EXPAND: "
            << " saDstAmountReq:" << saDstAmountReq
            << " saMaxAmountReq:" << saMaxAmountReq
            << " uDstAccountID:"
            << RippleAddress::createHumanAccountID (uDstAccountID)
            << " uSrcAccountID:"
            << RippleAddress::createHumanAccountID (uSrcAccountID);

        pspExpanded->setExpanded (
            activeLedger, spPath, uDstAccountID, uSrcAccountID);

        if (tesSUCCESS == pspExpanded->terStatus)
           pspExpanded->checkNoRipple (uDstAccountID, uSrcAccountID);

        WriteLog (lsDEBUG, RippleCalc)
            << "rippleCalc:"
            << " Build path:" << ++iIndex
            << " status: " << transToken (pspExpanded->terStatus);

        // Return, if the path specification was malformed.
        if (isTemMalformed (pspExpanded->terStatus))
            return pspExpanded->terStatus;

        if (tesSUCCESS == pspExpanded->terStatus)
        {
            terResult   = tesSUCCESS;           // Had a success.

            pspExpanded->setIndex (vpsExpanded.size ());
            vpsExpanded.push_back (pspExpanded);
        }
        else if (terNO_LINE != pspExpanded->terStatus)
        {
            terResult   = pspExpanded->terStatus;
        }
    }

    if (tesSUCCESS != terResult)
        return terResult == temUNCERTAIN ? terNO_LINE : terResult;
    else
        terResult   = temUNCERTAIN;

    saMaxAmountAct  = STAmount (
        saMaxAmountReq.getCurrency (), saMaxAmountReq.getIssuer ());
    saDstAmountAct  = STAmount (
        saDstAmountReq.getCurrency (), saDstAmountReq.getIssuer ());

    // Checkpoint with just fees paid.
    const LedgerEntrySet lesBase = activeLedger;

    // When processing, we don't want to complicate directory walking with
    // deletion.
    const std::uint64_t uQualityLimit = bLimitQuality
        ? STAmount::getRate (saDstAmountReq, saMaxAmountReq) : 0;

    // Offers that became unfunded.
    std::vector<uint256>    vuUnfundedBecame;

    int iPass   = 0;

    while (temUNCERTAIN == terResult)
    {
        int iBest = -1;
        const LedgerEntrySet lesCheckpoint = activeLedger;
        int iDry = 0;

        // True, if ever computed multi-quality.
        bool bMultiQuality   = false;

        // Find the best path.
        for (auto pspCur: vpsExpanded)
        {
            if (pspCur->uQuality)
                // Only do active paths.
            {
                bMultiQuality       = 1 == vpsExpanded.size () - iDry;
                // Computing the only non-dry path, compute multi-quality.

                pspCur->saInAct     = saMaxAmountAct;
                // Update to current amount processed.

                pspCur->saOutAct    = saDstAmountAct;

                CondLog (pspCur->saInReq > zero
                         && pspCur->saInAct >= pspCur->saInReq,
                         lsWARNING, RippleCalc)
                    << "rippleCalc: DONE:"
                    << " saInAct=" << pspCur->saInAct
                    << " saInReq=" << pspCur->saInReq;

                assert (pspCur->saInReq < zero ||
                        pspCur->saInAct < pspCur->saInReq); // Error if done.

                CondLog (pspCur->saOutAct >= pspCur->saOutReq,
                         lsWARNING, RippleCalc)
                    << "rippleCalc: ALREADY DONE:"
                    << " saOutAct=" << pspCur->saOutAct
                    << " saOutReq=%s" << pspCur->saOutReq;

                assert (pspCur->saOutAct < pspCur->saOutReq);
                // Error if done, output met.

                rc.pathNext (pspCur, bMultiQuality, lesCheckpoint, activeLedger);
                // Compute increment.
                WriteLog (lsDEBUG, RippleCalc)
                    << "rippleCalc: AFTER:"
                    << " mIndex=" << pspCur->mIndex
                    << " uQuality=" << pspCur->uQuality
                    << " rate=%s" << STAmount::saFromRate (pspCur->uQuality);

                if (!pspCur->uQuality)
                {
                    // Path was dry.

                    ++iDry;
                }
                else
                {
                    CondLog (!pspCur->saInPass || !pspCur->saOutPass,
                             lsDEBUG, RippleCalc)
                        << "rippleCalc: better:"
                        << " uQuality="
                        << STAmount::saFromRate (pspCur->uQuality)
                        << " saInPass=" << pspCur->saInPass
                        << " saOutPass=" << pspCur->saOutPass;

                    assert (!!pspCur->saInPass && !!pspCur->saOutPass);

                    if ((!bLimitQuality || pspCur->uQuality <= uQualityLimit)
                        // Quality is not limited or increment has allowed
                        // quality.
                        && (iBest < 0
                            // Best is not yet set.
                            || PathState::lessPriority (*vpsExpanded[iBest],
                                                        *pspCur)))
                        // Current is better than set.
                    {
                        WriteLog (lsDEBUG, RippleCalc)
                            << "rippleCalc: better:"
                            << " mIndex=" << pspCur->mIndex
                            << " uQuality=" << pspCur->uQuality
                            << " rate="
                            << STAmount::saFromRate (pspCur->uQuality)
                            << " saInPass=" << pspCur->saInPass
                            << " saOutPass=" << pspCur->saOutPass;

                        assert (activeLedger.isValid ());
                        activeLedger.swapWith (pspCur->lesEntries);
                        // For the path, save ledger state.
                        activeLedger.invalidate ();

                        iBest   = pspCur->getIndex ();
                    }
                }
            }
        }

        if (ShouldLog (lsDEBUG, RippleCalc))
        {
            WriteLog (lsDEBUG, RippleCalc)
                << "rippleCalc: Summary:"
                << " Pass: " << ++iPass
                << " Dry: " << iDry
                << " Paths: " << vpsExpanded.size ();
            for (auto pspCur: vpsExpanded)
            {
                WriteLog (lsDEBUG, RippleCalc)
                    << "rippleCalc: "
                    << "Summary: " << pspCur->mIndex
                    << " rate: "
                    << STAmount::saFromRate (pspCur->uQuality)
                    << " quality:" << pspCur->uQuality
                    << " best: " << (iBest == pspCur->getIndex ())
                    << " consumed: " << pspCur->bConsumed;
            }
        }

        if (iBest >= 0)
        {
            // Apply best path.
            PathState::pointer  pspBest = vpsExpanded[iBest];

            WriteLog (lsDEBUG, RippleCalc)
                << "rippleCalc: best:"
                << " uQuality="
                << STAmount::saFromRate (pspBest->uQuality)
                << " saInPass=" << pspBest->saInPass
                << " saOutPass=" << pspBest->saOutPass;

            // Record best pass' offers that became unfunded for deletion on
            // success.
            vuUnfundedBecame.insert (
                vuUnfundedBecame.end (),
                pspBest->vUnfundedBecame.begin (),
                pspBest->vUnfundedBecame.end ());

            // Record best pass' LedgerEntrySet to build off of and potentially
            // return.
            assert (pspBest->lesEntries.isValid ());
            activeLedger.swapWith (pspBest->lesEntries);
            pspBest->lesEntries.invalidate ();

            saMaxAmountAct  += pspBest->saInPass;
            saDstAmountAct  += pspBest->saOutPass;

            if (pspBest->bConsumed || bMultiQuality)
            {
                ++iDry;
                pspBest->uQuality   = 0;
            }

            if (saDstAmountAct == saDstAmountReq)
            {
                // Done. Delivered requested amount.

                terResult   = tesSUCCESS;
            }
            else if (saDstAmountAct > saDstAmountReq)
            {
                WriteLog (lsFATAL, RippleCalc)
                    << "rippleCalc: TOO MUCH:"
                    << " saDstAmountAct:" << saDstAmountAct
                    << " saDstAmountReq:" << saDstAmountReq;

                return tefEXCEPTION;  // TEMPORARY
                assert (false);
            }
            else if (saMaxAmountAct != saMaxAmountReq &&
                     iDry != vpsExpanded.size ())
            {
                // Have not met requested amount or max send, try to do
                // more. Prepare for next pass.
                //
                // Merge best pass' umReverse.
                rc.mumSource.insert (
                    pspBest->umReverse.begin (), pspBest->umReverse.end ());

            }
            else if (!bPartialPayment)
            {
                // Have sent maximum allowed. Partial payment not allowed.

                terResult   = tecPATH_PARTIAL;
            }
            else
            {
                // Have sent maximum allowed. Partial payment allowed.  Success.

                terResult   = tesSUCCESS;
            }
        }
        // Not done and ran out of paths.
        else if (!bPartialPayment)
        {
            // Partial payment not allowed.
            terResult   = tecPATH_PARTIAL;
        }
        // Partial payment ok.
        else if (!saDstAmountAct)
        {
            // No payment at all.
            terResult   = tecPATH_DRY;
        }
        else
        {
            terResult   = tesSUCCESS;
        }
    }

    if (!bStandAlone)
    {
        if (tesSUCCESS == terResult)
        {
            // Delete became unfunded offers.
            for (auto const& uOfferIndex: vuUnfundedBecame)
            {
                if (tesSUCCESS == terResult)
                {
                    WriteLog (lsDEBUG, RippleCalc)
                        << "Became unfunded " << to_string (uOfferIndex);
                    terResult = activeLedger.offerDelete (uOfferIndex);
                }
            }
        }

        // Delete found unfunded offers.
        for (auto const& uOfferIndex: rc.mUnfundedOffers)
        {
            if (tesSUCCESS == terResult)
            {
                WriteLog (lsDEBUG, RippleCalc)
                    << "Delete unfunded " << to_string (uOfferIndex);
                terResult = activeLedger.offerDelete (uOfferIndex);
            }
        }
    }

    return terResult;
}

} // ripple
