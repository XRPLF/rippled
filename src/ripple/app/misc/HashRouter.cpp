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
#include <ripple/basics/UptimeTimer.h>
#include <map>
#include <mutex>

namespace ripple {

auto
HashRouter::emplace (uint256 const& index)
    -> std::pair<Entry&, bool>
{
    auto fit = mSuppressionMap.find (index);

    if (fit != mSuppressionMap.end ())
    {
        return std::make_pair(
            std::ref(fit->second), false);
    }

    int elapsed = UptimeTimer::getInstance ().getElapsedSeconds ();
    int expireCutoff = elapsed - mHoldTime;

    // See if any supressions need to be expired
    auto it = mSuppressionTimes.begin ();

    while ((it != mSuppressionTimes.end ()) && (it->first <= expireCutoff))
    {
        for(auto const& lit : it->second)
            mSuppressionMap.erase (lit);
        it = mSuppressionTimes.erase (it);
    }

    mSuppressionTimes[elapsed].push_back (index);
    return std::make_pair(std::ref(
        mSuppressionMap.emplace (
            index, Entry ()).first->second),
                true);
}

void HashRouter::addSuppression (uint256 const& index)
{
    std::lock_guard <std::mutex> lock (mMutex);

    emplace (index);
}

bool HashRouter::addSuppressionPeer (uint256 const& index, PeerShortID peer)
{
    std::lock_guard <std::mutex> lock (mMutex);

    auto result = emplace(index);
    result.first.addPeer(peer);
    return result.second;
}

bool HashRouter::addSuppressionPeer (uint256 const& index, PeerShortID peer, int& flags)
{
    std::lock_guard <std::mutex> lock (mMutex);

    auto result = emplace(index);
    auto& s = result.first;
    s.addPeer (peer);
    flags = s.getFlags ();
    return result.second;
}

int HashRouter::getFlags (uint256 const& index)
{
    std::lock_guard <std::mutex> lock (mMutex);

    return emplace(index).first.getFlags ();
}

bool HashRouter::setFlags (uint256 const& index, int flags)
{
    assert (flags != 0);

    std::lock_guard <std::mutex> lock (mMutex);

    auto& s = emplace(index).first;

    if ((s.getFlags () & flags) == flags)
        return false;

    s.setFlags (flags);
    return true;
}

bool HashRouter::swapSet (uint256 const& index, std::set<PeerShortID>& peers, int flag)
{
    std::lock_guard <std::mutex> lock (mMutex);

    auto& s = emplace(index).first;

    if ((s.getFlags () & flag) == flag)
        return false;

    s.swapSet (peers);
    s.setFlags (flag);

    return true;
}

} // ripple
