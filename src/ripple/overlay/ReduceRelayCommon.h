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

#ifndef RIPPLE_OVERLAY_REDUCERELAYCOMMON_H_INCLUDED
#define RIPPLE_OVERLAY_REDUCERELAYCOMMON_H_INCLUDED

#include <chrono>

namespace ripple {

// Blog post explaining the rationale behind reduction of flooding gossip
// protocol:
// https://xrpl.org/blog/2021/message-routing-optimizations-pt-1-proposal-validation-relaying.html

namespace reduce_relay {

// Peer's squelch is limited in time to
// rand{MIN_UNSQUELCH_EXPIRE, max_squelch},
// where max_squelch is
// min(max(MAX_UNSQUELCH_EXPIRE_DEFAULT, SQUELCH_PER_PEER * number_of_peers),
//     MAX_UNSQUELCH_EXPIRE_PEERS)
static constexpr auto MIN_UNSQUELCH_EXPIRE = std::chrono::seconds{300};
static constexpr auto MAX_UNSQUELCH_EXPIRE_DEFAULT = std::chrono::seconds{600};
static constexpr auto SQUELCH_PER_PEER = std::chrono::seconds(10);
static constexpr auto MAX_UNSQUELCH_EXPIRE_PEERS = std::chrono::seconds{3600};
// No message received threshold before identifying a peer as idled
static constexpr auto IDLED = std::chrono::seconds{8};
// Message count threshold to start selecting peers as the source
// of messages from the validator. We add peers who reach
// MIN_MESSAGE_THRESHOLD to considered pool once MAX_SELECTED_PEERS
// reach MAX_MESSAGE_THRESHOLD.
static constexpr uint16_t MIN_MESSAGE_THRESHOLD = 9;
static constexpr uint16_t MAX_MESSAGE_THRESHOLD = 10;
// Max selected peers to choose as the source of messages from validator
static constexpr uint16_t MAX_SELECTED_PEERS = 5;
// Wait before reduce-relay feature is enabled on boot up to let
// the server establish peer connections
static constexpr auto WAIT_ON_BOOTUP = std::chrono::minutes{10};
// Maximum size of the aggregated transaction hashes per peer.
// Once we get to high tps throughput, this cap will prevent
// TMTransactions from exceeding the current protocol message
// size limit of 64MB.
static constexpr std::size_t MAX_TX_QUEUE_SIZE = 10000;

}  // namespace reduce_relay

}  // namespace ripple

#endif  // RIPPLED_REDUCERELAYCOMMON_H_INCLUDED