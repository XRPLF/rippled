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

#include <BeastConfig.h>
#include <ripple/app/misc/HashRouter.h>
#include <ripple/protocol/STTx.h>
#include <ripple/basics/UptimeTimer.h>
#include <map>
#include <mutex>

namespace ripple {

std::function<bool(STTx const&, std::function<bool(STTx const&)>)>
HashRouter::sigVerify()
{
    return
    [&] (STTx const& tx, std::function<bool(STTx const&)> sigCheck)
    {
        auto const id = tx.getTransactionID();
        auto const flags = getFlags(id);
        if (flags & SF_SIGGOOD)
            return true;
        if (flags & SF_BAD)
            return false;
        if (! sigCheck(tx))
        {
            setFlags(id, SF_BAD);
            return false;
        }
        setFlags(id, SF_SIGGOOD);
        return true;
    };
}

HashRouter::Entry& HashRouter::findCreateEntry (uint256 const& index, bool& created)
{
    hash_map<uint256, Entry>::iterator fit = mSuppressionMap.find (index);

    if (fit != mSuppressionMap.end ())
    {
        created = false;
        return fit->second;
    }

    created = true;

    int now = UptimeTimer::getInstance ().getElapsedSeconds ();
    int expireTime = now - mHoldTime;

    // See if any supressions need to be expired
    auto it = mSuppressionTimes.begin ();

    while ((it != mSuppressionTimes.end ()) && (it->first <= expireTime))
    {
        for(auto const& lit : it->second)
            mSuppressionMap.erase (lit);
        it = mSuppressionTimes.erase (it);
    }

    mSuppressionTimes[now].push_back (index);
    return mSuppressionMap.emplace (index, Entry ()).first->second;
}

bool HashRouter::addSuppression (uint256 const& index)
{
    ScopedLockType lock (mMutex);

    bool created;
    findCreateEntry (index, created);
    return created;
}

HashRouter::Entry HashRouter::getEntry (uint256 const& index)
{
    ScopedLockType lock (mMutex);

    bool created;
    return findCreateEntry (index, created);
}

bool HashRouter::addSuppressionPeer (uint256 const& index, PeerShortID peer)
{
    ScopedLockType lock (mMutex);

    bool created;
    findCreateEntry (index, created).addPeer (peer);
    return created;
}

bool HashRouter::addSuppressionPeer (uint256 const& index, PeerShortID peer, int& flags)
{
    ScopedLockType lock (mMutex);

    bool created;
    Entry& s = findCreateEntry (index, created);
    s.addPeer (peer);
    flags = s.getFlags ();
    return created;
}

int HashRouter::getFlags (uint256 const& index)
{
    ScopedLockType lock (mMutex);

    bool created;
    return findCreateEntry (index, created).getFlags ();
}

bool HashRouter::addSuppressionFlags (uint256 const& index, int flag)
{
    ScopedLockType lock (mMutex);

    bool created;
    findCreateEntry (index, created).setFlags (flag);
    return created;
}

bool HashRouter::setFlags (uint256 const& index, int flags)
{
    // VFALCO NOTE Comments like this belong in the HEADER file,
    //             and more importantly in a Javadoc comment so
    //             they appear in the generated documentation.
    //
    // return: true = changed, false = unchanged
    assert (flags != 0);

    ScopedLockType lock (mMutex);

    bool created;
    Entry& s = findCreateEntry (index, created);

    if ((s.getFlags () & flags) == flags)
        return false;

    s.setFlags (flags);
    return true;
}

bool HashRouter::swapSet (uint256 const& index, std::set<PeerShortID>& peers, int flag)
{
    ScopedLockType lock (mMutex);

    bool created;
    Entry& s = findCreateEntry (index, created);

    if ((s.getFlags () & flag) == flag)
        return false;

    s.swapSet (peers);
    s.setFlags (flag);

    return true;
}

} // ripple
