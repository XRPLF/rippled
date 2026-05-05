#pragma once

#include <chrono>

namespace xrpl::Tuning {

// Need to be named before converting
// NOLINTNEXTLINE(cppcoreguidelines-use-enum-class)
enum {
    /** How many ledgers off a server can be and we will
        still consider it converged */
    ConvergedLedgerLimit = 24,

    /** How many ledgers off a server has to be before we
        consider it diverged */
    DivergedLedgerLimit = 128,

    /** The soft cap on the number of ledger entries in a single reply. */
    SoftMaxReplyNodes = 8192,

    /** The hard cap on the number of ledger entries in a single reply. */
    HardMaxReplyNodes = 12288,

    /** How many timer intervals a sendq has to stay large before we disconnect
     */
    SendqIntervals = 4,

    /** How many messages on a send queue before we refuse queries */
    DropSendQueue = 192,

    /** How many messages we consider reasonable sustained on a send queue */
    TargetSendQueue = 128,

    /** How often to log send queue size */
    SendQueueLogFreq = 64,

    /** How often we check for idle peers (seconds) */
    CheckIdlePeers = 4,

    /** The maximum number of levels to search */
    MaxQueryDepth = 3,
};

/** Size of buffer used to read from the socket. */
std::size_t constexpr kREAD_BUFFER_BYTES = 16384;

}  // namespace xrpl::Tuning
