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

AccountItems& RippleLineCache::getRippleLines (const uint160& accountID)
{
    ScopedLockType sl (mLock);

    ripple::unordered_map <uint160, AccountItems::pointer>::iterator it = mRLMap.find (accountID);

    if (it == mRLMap.end ())
        it = mRLMap.insert (std::make_pair (accountID, std::make_shared<AccountItems>
                                            (std::cref (accountID), std::cref (mLedger), AccountItem::pointer (new RippleState ())))).first;

    return *it->second;
}

} // ripple
