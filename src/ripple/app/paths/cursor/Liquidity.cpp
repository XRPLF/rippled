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

#include <tuple>

#include <ripple/app/paths/cursor/RippleLiquidity.h>

namespace ripple {
namespace path {

TER PathCursor::liquidity (LedgerEntrySet const& lesCheckpoint) const
{
    TER resultCode;
    PathCursor pc = *this;

    ledger() = lesCheckpoint.duplicate ();
    for (pc.nodeIndex_ = pc.nodeSize(); pc.nodeIndex_--; )
    {
        WriteLog (lsTRACE, RippleCalc)
            << "reverseLiquidity>"
            << " nodeIndex=" << pc.nodeIndex_
            << ".issue_.account=" << to_string (pc.node().issue_.account);

        resultCode  = pc.reverseLiquidity();

        WriteLog (lsTRACE, RippleCalc)
            << "reverseLiquidity< "
            << "nodeIndex=" << pc.nodeIndex_
            << " resultCode=%s" << transToken (resultCode)
            << " transferRate_=" << pc.node().transferRate_
            << "/" << resultCode;

        if (resultCode != tesSUCCESS)
            break;
    }

    WriteLog (lsTRACE, RippleCalc)
        << "nextIncrement: Path after reverse: " << pathState_.getJson ();

    if (resultCode != tesSUCCESS)
        return resultCode;

    // Do forward.
    ledger() = lesCheckpoint.duplicate ();
    for (pc.nodeIndex_ = 0; pc.nodeIndex_ < pc.nodeSize(); ++pc.nodeIndex_)
    {
        WriteLog (lsTRACE, RippleCalc)
            << "forwardLiquidity> nodeIndex=" << nodeIndex_;

        resultCode = pc.forwardLiquidity();
        if (resultCode != tesSUCCESS)
            return resultCode;

        WriteLog (lsTRACE, RippleCalc)
            << "forwardLiquidity<"
            << " nodeIndex:" << pc.nodeIndex_
            << " resultCode:" << resultCode;

        if (pathState_.isDry())
            resultCode = tecPATH_DRY;
    }
    return resultCode;
}

} // path
} // ripple
