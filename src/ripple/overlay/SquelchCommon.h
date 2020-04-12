//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright 2020 Ripple Labs Inc.

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

#ifndef RIPPLE_OVERLAY_SQUELCHCOMMON_H_INCLUDED
#define RIPPLE_OVERLAY_SQUELCHCOMMON_H_INCLUDED
#include <chrono>

namespace ripple {

namespace squelch {

using namespace std::chrono;

// Peer's squelch is limited in time to
// rand{MIN_UNSQUELCH_EXPIRE, MAX_UNSQUELCH_EXPIRE}
static constexpr seconds MIN_UNSQUELCH_EXPIRE = seconds{300};
static constexpr seconds MAX_UNSQUELCH_EXPIRE = seconds{600};
// No message received threshold before identifying a peer as idled
static constexpr seconds IDLED = seconds{8};
// Message count threshold to start selecting peers as the source
// of messages from the validator. We add peers who reach
// MIN_MESSAGE_THRESHOLD to considered pool once MAX_SELECTED_PEERS
// reach MAX_MESSAGE_THRESHOLD.
static constexpr uint16_t MIN_MESSAGE_THRESHOLD = 9;
static constexpr uint16_t MAX_MESSAGE_THRESHOLD = 10;
// Max selected peers to choose as the source of messages from validator
static constexpr uint16_t MAX_SELECTED_PEERS = 3;
// Wait before reduce-relay feature is enabled on boot up to let
// the server establish peer connections
static constexpr minutes WAIT_ON_BOOTUP = minutes{10};

}  // namespace squelch

}  // namespace ripple

#endif  // RIPPLED_SQUELCHCOMMON_H
