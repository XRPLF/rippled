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

#include "CalcNode.cpp"
#include "CalcNodeAccountFwd.cpp"
#include "CalcNodeAccountRev.cpp"
#include "CalcNodeAdvance.cpp"
#include "CalcNodeDeliverFwd.cpp"
#include "CalcNodeDeliverRev.cpp"
#include "CalcNodeOffer.cpp"
#include "CalcNodeRipple.cpp"
#include "PathNext.cpp"

namespace ripple {

SETUP_LOG (RippleCalc)

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

    TER         errorCode   = temUNCERTAIN;

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
            errorCode   = tesSUCCESS;

            vpsExpanded.push_back (pspDirect);
        }
        else if (terNO_LINE != pspDirect->terStatus)
        {
            errorCode   = pspDirect->terStatus;
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
            errorCode   = tesSUCCESS;           // Had a success.

            pspExpanded->setIndex (vpsExpanded.size ());
            vpsExpanded.push_back (pspExpanded);
        }
        else if (terNO_LINE != pspExpanded->terStatus)
        {
            errorCode   = pspExpanded->terStatus;
        }
    }

    if (errorCode != tesSUCCESS)
        return errorCode == temUNCERTAIN ? terNO_LINE : errorCode;
    else
        errorCode   = temUNCERTAIN;

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

    while (errorCode == temUNCERTAIN)
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

                errorCode   = tesSUCCESS;
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

                errorCode   = tecPATH_PARTIAL;
            }
            else
            {
                // Have sent maximum allowed. Partial payment allowed.  Success.

                errorCode   = tesSUCCESS;
            }
        }
        // Not done and ran out of paths.
        else if (!bPartialPayment)
        {
            // Partial payment not allowed.
            errorCode   = tecPATH_PARTIAL;
        }
        // Partial payment ok.
        else if (!saDstAmountAct)
        {
            // No payment at all.
            errorCode   = tecPATH_DRY;
        }
        else
        {
            errorCode   = tesSUCCESS;
        }
    }

    if (!bStandAlone)
    {
        if (errorCode == tesSUCCESS)
        {
            // Delete became unfunded offers.
            for (auto const& uOfferIndex: vuUnfundedBecame)
            {
                if (errorCode == tesSUCCESS)
                {
                    WriteLog (lsDEBUG, RippleCalc)
                        << "Became unfunded " << to_string (uOfferIndex);
                    errorCode = activeLedger.offerDelete (uOfferIndex);
                }
            }
        }

        // Delete found unfunded offers.
        for (auto const& uOfferIndex: rc.mUnfundedOffers)
        {
            if (errorCode == tesSUCCESS)
            {
                WriteLog (lsDEBUG, RippleCalc)
                    << "Delete unfunded " << to_string (uOfferIndex);
                errorCode = activeLedger.offerDelete (uOfferIndex);
            }
        }
    }

    return errorCode;
}

} // ripple
