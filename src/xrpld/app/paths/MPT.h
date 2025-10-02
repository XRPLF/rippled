//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2025 Ripple Labs Inc.

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

#ifndef RIPPLE_APP_PATHS_MPT_H_INCLUDED
#define RIPPLE_APP_PATHS_MPT_H_INCLUDED

#include <xrpl/protocol/MPTIssue.h>

namespace ripple {

class PathFindMPT final
{
private:
    MPTID const mptID_;
    // If true then holder's balance is 0, always false for issuer
    bool const zeroBalance_;
    // OutstandingAmount is equal to MaximumAmount
    bool const maxedOut_;

public:
    PathFindMPT(MPTID const& mptID)
        : mptID_(mptID), zeroBalance_(false), maxedOut_(false)
    {
    }
    PathFindMPT(MPTID const& mptID, bool zeroBalance, bool maxedOut)
        : mptID_(mptID), zeroBalance_(zeroBalance), maxedOut_(maxedOut)
    {
    }
    operator MPTID const&() const
    {
        return mptID_;
    }
    MPTID const&
    getMptID() const
    {
        return mptID_;
    }
    bool
    isZeroBalance() const
    {
        return zeroBalance_;
    }
    bool
    isMaxedOut() const
    {
        return maxedOut_;
    }
};

}  // namespace ripple

#endif  // RIPPLE_APP_PATHS_MPT_H_INCLUDED
