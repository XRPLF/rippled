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

#ifndef RIPPLE_RIPPLELINECACHE_H
#define RIPPLE_RIPPLELINECACHE_H

namespace ripple {

// Used by Pathfinder
class RippleLineCache
{
public:
    typedef std::shared_ptr <RippleLineCache> pointer;
    typedef pointer const& ref;

    explicit RippleLineCache (Ledger::ref l);

    Ledger::ref getLedger () // VFALCO TODO const?
    {
        return mLedger;
    }

    AccountItems& getRippleLines (const uint160& accountID);

private:
    typedef RippleMutex LockType;
    typedef std::lock_guard <LockType> ScopedLockType;
    LockType mLock;
   
    Ledger::pointer mLedger;
    
    ripple::unordered_map <uint160, AccountItems::pointer> mRLMap;
};

} // ripple

#endif
