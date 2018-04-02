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

#include <ripple/app/paths/cursor/RippleLiquidity.h>
#include <ripple/basics/contract.h>
#include <ripple/basics/Log.h>

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

void PathCursor::nextIncrement () const
{
    // The next state is what is available in preference order.
    // This is calculated when referenced accounts changed.

    auto status = liquidity();

    if (status == tesSUCCESS)
    {
        if (pathState_.isDry())
        {
            JLOG (j_.debug())
                << "nextIncrement: success on dry path:"
                << " outPass=" << pathState_.outPass()
                << " inPass=" << pathState_.inPass();
            Throw<std::runtime_error> ("Made no progress.");
        }

        // Calculate relative quality.
        pathState_.setQuality(getRate (
            pathState_.outPass(), pathState_.inPass()));
    }
    else
    {
        pathState_.setQuality(0);
    }
    pathState_.setStatus (status);
}

} // path
} // ripple
