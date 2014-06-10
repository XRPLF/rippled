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
#include <ripple/module/app/paths/CalcState.h>
#include <ripple/module/app/paths/RippleCalc.h>
#include <ripple/module/app/paths/Tuning.h>

namespace ripple {
namespace path {

// Calculate the next increment of a path.
//
// The increment is what can satisfy a portion or all of the requested output at
// the best quality.
//
// <-- pathState.uQuality
//
// This is the wrapper that restores a checkpointed version of the ledger so we
// can write all over it without consequence.

void pathNext (
    RippleCalc& rippleCalc,
    PathState& pathState, const bool bMultiQuality,
    const LedgerEntrySet& lesCheckpoint, LedgerEntrySet& lesCurrent)
{
    // The next state is what is available in preference order.
    // This is calculated when referenced accounts changed.
    const unsigned int  lastNodeIndex = pathState.nodes().size () - 1;
    pathState.clear();

    WriteLog (lsTRACE, RippleCalc)
        << "pathNext: Path In: " << pathState.getJson ();

    assert (pathState.nodes().size () >= 2);

    lesCurrent  = lesCheckpoint.duplicate ();  // Restore from checkpoint.

    for (unsigned int uIndex = pathState.nodes().size (); uIndex--;)
    {
        auto& node   = pathState.nodes()[uIndex];

        node.saRevRedeem.clear ();
        node.saRevIssue.clear ();
        node.saRevDeliver.clear ();
        node.saFwdDeliver.clear ();
    }

    pathState.setStatus(computeReverseLiqudity (rippleCalc, lastNodeIndex, pathState, bMultiQuality));

    WriteLog (lsTRACE, RippleCalc)
        << "pathNext: Path after reverse: " << pathState.getJson ();

    if (tesSUCCESS == pathState.status())
    {
        // Do forward.
        lesCurrent = lesCheckpoint.duplicate ();   // Restore from checkpoint.

        pathState.setStatus(computeForwardLiqudity (rippleCalc, 0, pathState, bMultiQuality));
    }

    if (tesSUCCESS == pathState.status())
    {
        CondLog (!pathState.inPass() || !pathState.outPass(), lsDEBUG, RippleCalc)
            << "pathNext: Error computeForwardLiqudity reported success for nothing:"
            << " saOutPass=" << pathState.outPass()
            << " inPass()=" << pathState.inPass();

        if (!pathState.outPass() || !pathState.inPass())
            throw std::runtime_error ("Made no progress.");

        // Calculate relative quality.
        pathState.setQuality(STAmount::getRate (
            pathState.outPass(), pathState.inPass()));

        WriteLog (lsTRACE, RippleCalc)
            << "pathNext: Path after forward: " << pathState.getJson ();
    }
    else
    {
        pathState.setQuality(0);
    }
}

} // path
} // ripple
