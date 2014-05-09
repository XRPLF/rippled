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

#include "RippleCalc.h"
#include "Tuning.h"

namespace ripple {

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

} // ripple
