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

RippleLineCache::RippleLineCache (Ledger::ref l)
    : mLedger (l)
{
}

std::vector<RippleState::pointer> const&
RippleLineCache::getRippleLines (Account const& accountID)
{

    {
        ScopedLockType sl (mLock);

        auto it = mRLMap.find (accountID);
        if (it != mRLMap.end ())
            return it->second;
    }

    // It's not in the cache, so build it
    auto lines = ripple::getRippleStateItems (accountID, mLedger);

    ScopedLockType sl (mLock);

    // We must return a reference to the cached version
    return mRLMap.emplace (accountID, std::move (lines)).first->second;
}

} // ripple
