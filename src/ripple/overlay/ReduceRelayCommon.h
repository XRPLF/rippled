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
#include <stdlib.h>

namespace ripple {

namespace reduce_relay {

using namespace std::chrono;

// Peer's squelch is limited in time to
// rand{MIN_UNSQUELCH_EXPIRE, MAX_UNSQUELCH_EXPIRE}
static constexpr seconds MIN_UNSQUELCH_EXPIRE = seconds{300};
static constexpr seconds MAX_UNSQUELCH_EXPIRE = seconds{600};
// Peer's squelch is:
// max(MAX_UNSQUELCH_EXPIRE, UNSQUELCH_EXPIRE_MULTIPLIER * number_of_peers)
// but we don't expect it to be greater than OVERAL_MAX_UNSQUELCH_EXPIRE.
static constexpr std::size_t UNSQUELCH_EXPIRE_MULTIPLIER = 10;
static constexpr seconds OVERALL_MAX_UNSQUELCH_EXPIRE = seconds{3600};
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

// Reduce-relay feature values used in the HTTP handshake.
enum class ReduceRelayEnabled : std::uint8_t {
    ValidationProposal = 0x01,
};

/** Checks if the header has the specified feature enabled
   @param header value of X-Offer-Reduce-Relay header
   @param enabled feature to check
   @return true if the feature is enabled
 */
inline bool
reduceRelayEnabled(std::string const& header, ReduceRelayEnabled enabled)
{
    int i = 0;
    try
    {
        i = std::stoi(header);
    }
    catch (...)
    {
    }
    return (i & static_cast<int>(enabled)) == static_cast<int>(enabled);
}

/** Make HTTP header value depending on the current value and reduce-relay
   features configuration values. Used in making the handshake response.
   @param header value of the request's X-Offer-Reduce-Relay header
   @param vpEnabled configuration value of the validation/proposal
       reduce-relay feature
   @return X-Offer-Reduce-Relay header value
 */
inline std::string
makeHeaderValue(std::string const& header, bool vpEnabled)
{
    int value = 0;
    if (reduceRelayEnabled(header, ReduceRelayEnabled::ValidationProposal) &&
        vpEnabled)
        value |= static_cast<int>(ReduceRelayEnabled::ValidationProposal);
    return std::to_string(value);
}

/** Make HTTP header value depending on reduce-relay features configuration
   values. Used in making the handshake request.
   @param vpEnabled configuration value of the validation/proposal
       reduce-relay feature
   @return X-Offer-Reduce-Relay header value
 */
inline std::string
makeHeaderValue(bool vpEnabled)
{
    int value = 0;
    if (vpEnabled)
        value |= static_cast<int>(ReduceRelayEnabled::ValidationProposal);
    return std::to_string(value);
}

}  // namespace reduce_relay

}  // namespace ripple

#endif  // RIPPLED_REDUCERELAYCOMMON_H_INCLUDED