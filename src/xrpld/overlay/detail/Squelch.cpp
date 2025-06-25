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

#include <xrpld/overlay/ReduceRelayCommon.h>
#include <xrpld/overlay/Squelch.h>

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/protocol/PublicKey.h>

#include <chrono>

namespace ripple {

namespace reduce_relay {

bool
Squelch::addSquelch(
    PublicKey const& validator,
    std::chrono::seconds const& squelchDuration)
{
    if (squelchDuration >= MIN_UNSQUELCH_EXPIRE &&
        squelchDuration <= MAX_UNSQUELCH_EXPIRE_PEERS)
    {
        squelched_[validator] = clock_.now() + squelchDuration;
        return true;
    }

    JLOG(journal_.error()) << "squelch: invalid squelch duration "
                           << squelchDuration.count();

    // unsquelch if invalid duration
    removeSquelch(validator);

    return false;
}

void
Squelch::removeSquelch(PublicKey const& validator)
{
    squelched_.erase(validator);
}

bool
Squelch::expireSquelch(PublicKey const& validator)
{
    auto const now = clock_.now();

    auto const& it = squelched_.find(validator);
    if (it == squelched_.end())
        return true;
    else if (it->second > now)
        return false;

    // squelch expired
    squelched_.erase(it);

    return true;
}

}  // namespace reduce_relay

}  // namespace ripple
