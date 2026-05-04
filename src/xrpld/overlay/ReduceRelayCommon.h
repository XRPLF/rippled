#pragma once

#include <chrono>

// Blog post explaining the rationale behind reduction of flooding gossip
// protocol:
// https://xrpl.org/blog/2021/message-routing-optimizations-pt-1-proposal-validation-relaying.html

namespace xrpl::reduce_relay {

// Peer's squelch is limited in time to
// rand{MIN_UNSQUELCH_EXPIRE, max_squelch},
// where max_squelch is
// min(max(MAX_UNSQUELCH_EXPIRE_DEFAULT, SQUELCH_PER_PEER * number_of_peers),
//     MAX_UNSQUELCH_EXPIRE_PEERS)
static constexpr auto kMIN_UNSQUELCH_EXPIRE = std::chrono::seconds{300};
static constexpr auto kMAX_UNSQUELCH_EXPIRE_DEFAULT = std::chrono::seconds{600};
static constexpr auto kSQUELCH_PER_PEER = std::chrono::seconds(10);
static constexpr auto kMAX_UNSQUELCH_EXPIRE_PEERS = std::chrono::seconds{3600};
// No message received threshold before identifying a peer as idled
static constexpr auto kIDLED = std::chrono::seconds{8};
// Message count threshold to start selecting peers as the source
// of messages from the validator. We add peers who reach
// kMIN_MESSAGE_THRESHOLD to considered pool once kMAX_SELECTED_PEERS
// reach kMAX_MESSAGE_THRESHOLD.
static constexpr uint16_t kMIN_MESSAGE_THRESHOLD = 19;
static constexpr uint16_t kMAX_MESSAGE_THRESHOLD = 20;
// Max selected peers to choose as the source of messages from validator
static constexpr uint16_t kMAX_SELECTED_PEERS = 5;
// Wait before reduce-relay feature is enabled on boot up to let
// the server establish peer connections
static constexpr auto kWAIT_ON_BOOTUP = std::chrono::minutes{10};
// Maximum size of the aggregated transaction hashes per peer.
// Once we get to high tps throughput, this cap will prevent
// TMTransactions from exceeding the current protocol message
// size limit of 64MB.
static constexpr std::size_t kMAX_TX_QUEUE_SIZE = 10000;

}  // namespace xrpl::reduce_relay
