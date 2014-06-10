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

#include <ripple/module/app/paths/CalcNodeAdvance.cpp>
#include <ripple/module/app/paths/CalcNodeDeliverFwd.cpp>
#include <ripple/module/app/paths/CalcNodeDeliverRev.cpp>
#include <ripple/module/app/paths/ComputeRippleLiquidity.cpp>
#include <ripple/module/app/paths/ComputeAccountLiquidityForward.cpp>
#include <ripple/module/app/paths/ComputeAccountLiquidityReverse.cpp>
#include <ripple/module/app/paths/ComputeLiquidity.cpp>
#include <ripple/module/app/paths/ComputeOfferLiquidity.cpp>
#include <ripple/module/app/paths/Node.cpp>
#include <ripple/module/app/paths/PathNext.cpp>
#include <ripple/module/app/paths/Types.cpp>

namespace ripple {

SETUP_LOG (RippleCalc)

namespace path {

// OPTIMIZE: When calculating path increment, note if increment consumes all
// liquidity. No need to revisit path in the future if all liquidity is used.

// <-- TER: Only returns tepPATH_PARTIAL if !bPartialPayment.
TER rippleCalculate (
    // Compute paths vs this ledger entry set.  Up to caller to actually apply
    // to ledger.
    LedgerEntrySet& activeLedger,
    // <-> --> = Fee already applied to src balance.

    STAmount&       saMaxAmountAct,         // <-- The computed input amount.
    STAmount&       saDstAmountAct,         // <-- The computed output amount.

    // Expanded path with all the actual nodes in it.
    // TODO(tom): does it put in default paths?

    // A path starts with the source account, ends with the destination account
    // and goes through other acounts or order books.
    PathState::List&  pathStateList,

    // Issuer:
    //      XRP: XRP_ACCOUNT
    //  non-XRP: uSrcAccountID (for any issuer) or another account with trust
    //           node.
    const STAmount&     saMaxAmountReq,             // --> -1 = no limit.

    // Issuer:
    //      XRP: XRP_ACCOUNT
    //  non-XRP: uDstAccountID (for any issuer) or another account with trust
    //           node.
    const STAmount&     saDstAmountReq,

    const uint160&      uDstAccountID,
    const uint160&      uSrcAccountID,

    // A set of paths that are included in the transaction that we'll explore
    // for liquidity.
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

    TER         resultCode   = temUNCERTAIN;

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

        auto pspDirect = std::make_shared<PathState> (
            saDstAmountReq, saMaxAmountReq);

        if (!pspDirect)
            return temUNKNOWN;

        pspDirect->expandPath (
            activeLedger, STPath (), uDstAccountID, uSrcAccountID);

        if (tesSUCCESS == pspDirect->status())
           pspDirect->checkNoRipple (uDstAccountID, uSrcAccountID);

        pspDirect->setIndex (pathStateList.size ());

        WriteLog (lsDEBUG, RippleCalc)
            << "rippleCalc: Build direct:"
            << " status: " << transToken (pspDirect->status());

        // Return if malformed.
        if (isTemMalformed (pspDirect->status()))
            return pspDirect->status();

        if (tesSUCCESS == pspDirect->status())
        {
            resultCode   = tesSUCCESS;
            pathStateList.push_back (pspDirect);
        }
        else if (terNO_LINE != pspDirect->status())
        {
            resultCode   = pspDirect->status();
        }
    }

    WriteLog (lsTRACE, RippleCalc)
        << "rippleCalc: Paths in set: " << spsPaths.size ();

    int iIndex  = 0;
    for (auto const& spPath: spsPaths)
    {
        auto pspExpanded = std::make_shared<PathState> (
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

        pspExpanded->expandPath (
            activeLedger, spPath, uDstAccountID, uSrcAccountID);

        if (tesSUCCESS == pspExpanded->status())
           pspExpanded->checkNoRipple (uDstAccountID, uSrcAccountID);

        WriteLog (lsDEBUG, RippleCalc)
            << "rippleCalc:"
            << " Build path:" << ++iIndex
            << " status: " << transToken (pspExpanded->status());

        // Return, if the path specification was malformed.
        if (isTemMalformed (pspExpanded->status()))
            return pspExpanded->status();

        if (tesSUCCESS == pspExpanded->status())
        {
            resultCode   = tesSUCCESS;           // Had a success.

            pspExpanded->setIndex (pathStateList.size ());
            pathStateList.push_back (pspExpanded);
        }
        else if (terNO_LINE != pspExpanded->status())
        {
            resultCode   = pspExpanded->status();
        }
    }

    if (resultCode != tesSUCCESS)
        return resultCode == temUNCERTAIN ? terNO_LINE : resultCode;
    else
        resultCode   = temUNCERTAIN;

    saMaxAmountAct = saMaxAmountReq.zeroed();
    saDstAmountAct = saDstAmountReq.zeroed();

    // When processing, we don't want to complicate directory walking with
    // deletion.
    const std::uint64_t uQualityLimit = bLimitQuality
        ? STAmount::getRate (saDstAmountReq, saMaxAmountReq) : 0;

    // Offers that became unfunded.
    std::vector<uint256>    vuUnfundedBecame;

    int iPass   = 0;

    while (resultCode == temUNCERTAIN)
    {
        int iBest = -1;
        const LedgerEntrySet lesCheckpoint = activeLedger;
        int iDry = 0;

        // True, if ever computed multi-quality.
        bool bMultiQuality   = false;

        // Find the best path.
        for (auto pspCur: pathStateList)
        {
            if (pspCur->quality())
                // Only do active paths.
            {
                bMultiQuality       = 1 == pathStateList.size () - iDry;
                // Computing the only non-dry path, compute multi-quality.

                pspCur->inAct() = saMaxAmountAct;
                // Update to current amount processed.

                pspCur->outAct() = saDstAmountAct;

                CondLog (pspCur->inReq() > zero
                         && pspCur->inAct() >= pspCur->inReq(),
                         lsWARNING, RippleCalc)
                    << "rippleCalc: DONE:"
                    << " inAct()=" << pspCur->inAct()
                    << " inReq()=" << pspCur->inReq();

                assert (pspCur->inReq() < zero ||
                        pspCur->inAct() < pspCur->inReq()); // Error if done.

                CondLog (pspCur->outAct() >= pspCur->outReq(),
                         lsWARNING, RippleCalc)
                    << "rippleCalc: ALREADY DONE:"
                    << " saOutAct=" << pspCur->outAct()
                    << " saOutReq=%s" << pspCur->outReq();

                assert (pspCur->outAct() < pspCur->outReq());
                // Error if done, output met.

                pathNext (rc, *pspCur, bMultiQuality, lesCheckpoint, rc.mActiveLedger);
                // Compute increment.
                WriteLog (lsDEBUG, RippleCalc)
                    << "rippleCalc: AFTER:"
                    << " mIndex=" << pspCur->index()
                    << " uQuality=" << pspCur->quality()
                    << " rate=%s" << STAmount::saFromRate (pspCur->quality());

                if (!pspCur->quality())
                {
                    // Path was dry.

                    ++iDry;
                }
                else
                {
                    CondLog (!pspCur->inPass() || !pspCur->outPass(),
                             lsDEBUG, RippleCalc)
                        << "rippleCalc: better:"
                        << " uQuality="
                        << STAmount::saFromRate (pspCur->quality())
                        << " inPass()=" << pspCur->inPass()
                        << " saOutPass=" << pspCur->outPass();

                    assert (!!pspCur->inPass() && !!pspCur->outPass());

                    if ((!bLimitQuality || pspCur->quality() <= uQualityLimit)
                        // Quality is not limited or increment has allowed
                        // quality.
                        && (iBest < 0
                            // Best is not yet set.
                            || PathState::lessPriority (*pathStateList[iBest],
                                                        *pspCur)))
                        // Current is better than set.
                    {
                        WriteLog (lsDEBUG, RippleCalc)
                            << "rippleCalc: better:"
                            << " mIndex=" << pspCur->index()
                            << " uQuality=" << pspCur->quality()
                            << " rate="
                            << STAmount::saFromRate (pspCur->quality())
                            << " inPass()=" << pspCur->inPass()
                            << " saOutPass=" << pspCur->outPass();

                        assert (activeLedger.isValid ());
                        activeLedger.swapWith (pspCur->ledgerEntries());
                        // For the path, save ledger state.
                        activeLedger.invalidate ();

                        iBest   = pspCur->index ();
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
                << " Paths: " << pathStateList.size ();
            for (auto pspCur: pathStateList)
            {
                WriteLog (lsDEBUG, RippleCalc)
                    << "rippleCalc: "
                    << "Summary: " << pspCur->index()
                    << " rate: "
                    << STAmount::saFromRate (pspCur->quality())
                    << " quality:" << pspCur->quality()
                    << " best: " << (iBest == pspCur->index ());
            }
        }

        if (iBest >= 0)
        {
            // Apply best path.
            auto pspBest = pathStateList[iBest];

            WriteLog (lsDEBUG, RippleCalc)
                << "rippleCalc: best:"
                << " uQuality="
                << STAmount::saFromRate (pspBest->quality())
                << " inPass()=" << pspBest->inPass()
                << " saOutPass=" << pspBest->outPass();

            // Record best pass' offers that became unfunded for deletion on
            // success.
            vuUnfundedBecame.insert (
                vuUnfundedBecame.end (),
                pspBest->becameUnfunded().begin (),
                pspBest->becameUnfunded().end ());

            // Record best pass' LedgerEntrySet to build off of and potentially
            // return.
            assert (pspBest->ledgerEntries().isValid ());
            activeLedger.swapWith (pspBest->ledgerEntries());
            pspBest->ledgerEntries().invalidate ();

            saMaxAmountAct  += pspBest->inPass();
            saDstAmountAct  += pspBest->outPass();

            if (pspBest->allLiquidityConsumed() || bMultiQuality)
            {
                ++iDry;
                pspBest->setQuality(0);
            }

            if (saDstAmountAct == saDstAmountReq)
            {
                // Done. Delivered requested amount.

                resultCode   = tesSUCCESS;
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
                     iDry != pathStateList.size ())
            {
                // Have not met requested amount or max send, try to do
                // more. Prepare for next pass.
                //
                // Merge best pass' umReverse.
                rc.mumSource.insert (
                    pspBest->reverse().begin (), pspBest->reverse().end ());

            }
            else if (!bPartialPayment)
            {
                // Have sent maximum allowed. Partial payment not allowed.

                resultCode   = tecPATH_PARTIAL;
            }
            else
            {
                // Have sent maximum allowed. Partial payment allowed.  Success.

                resultCode   = tesSUCCESS;
            }
        }
        // Not done and ran out of paths.
        else if (!bPartialPayment)
        {
            // Partial payment not allowed.
            resultCode   = tecPATH_PARTIAL;
        }
        // Partial payment ok.
        else if (!saDstAmountAct)
        {
            // No payment at all.
            resultCode   = tecPATH_DRY;
        }
        else
        {
            resultCode   = tesSUCCESS;
        }
    }

    if (!bStandAlone)
    {
        if (resultCode == tesSUCCESS)
        {
            // Delete became unfunded offers.
            for (auto const& offerIndex: vuUnfundedBecame)
            {
                if (resultCode == tesSUCCESS)
                {
                    WriteLog (lsDEBUG, RippleCalc)
                        << "Became unfunded " << to_string (offerIndex);
                    resultCode = activeLedger.offerDelete (offerIndex);
                }
            }
        }

        // Delete found unfunded offers.
        for (auto const& offerIndex: rc.mUnfundedOffers)
        {
            if (resultCode == tesSUCCESS)
            {
                WriteLog (lsDEBUG, RippleCalc)
                    << "Delete unfunded " << to_string (offerIndex);
                resultCode = activeLedger.offerDelete (offerIndex);
            }
        }
    }

    return resultCode;
}

} // path
} // ripple
