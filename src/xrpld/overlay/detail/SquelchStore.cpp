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

#include <xrpld/overlay/Peer.h>
#include <xrpld/overlay/ReduceRelayCommon.h>
#include <xrpld/overlay/SquelchStore.h>

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/protocol/PublicKey.h>

#include <chrono>
#include <unordered_map>
#include <vector>

namespace ripple {

namespace reduce_relay {

void
SquelchStore::handleSquelch(
    PublicKey const& validator,
    bool squelch,
    std::chrono::seconds duration)
{
    // Remove all expired squelches. This call is here, as it is on the least
    // critical execution path, that does not require periodic cleanup calls.
    removeExpired();

    if (squelch)
    {
        // This should never trigger. The squelh duration is validated in
        // PeerImp.onMessage(TMSquelch). However, if somehow invalid duration is
        // passed, log is as an error
        if ((duration < reduce_relay::MIN_UNSQUELCH_EXPIRE ||
             duration > reduce_relay::MAX_UNSQUELCH_EXPIRE_PEERS))
        {
            JLOG(journal_.error())
                << "SquelchStore: invalid squelch duration validator: "
                << Slice(validator) << " duration: " << duration.count();
            return;
        }

        add(validator, duration);
        return;
    }

    remove(validator);
}

bool
SquelchStore::isSquelched(PublicKey const& validator) const
{
    auto const now = clock_.now();

    auto const it = squelched_.find(validator);
    if (it == squelched_.end())
        return false;

    return it->second > now;
}

void
SquelchStore::add(
    PublicKey const& validator,
    std::chrono::seconds const& duration)
{
    squelched_[validator] = clock_.now() + duration;
}

void
SquelchStore::remove(PublicKey const& validator)
{
    squelched_.erase(validator);
}

void
SquelchStore::removeExpired()
{
    auto const now = clock_.now();
    std::erase_if(
        squelched_, [&](auto const& entry) { return entry.second < now; });
}

}  // namespace reduce_relay

}  // namespace ripple
