#ifndef XRPL_OVERLAY_TUNING_H_INCLUDED
#define XRPL_OVERLAY_TUNING_H_INCLUDED

#include <chrono>

namespace ripple {

namespace Tuning {

enum {
    /** How many ledgers off a server can be and we will
        still consider it converged */
    convergedLedgerLimit = 24,

    /** How many ledgers off a server has to be before we
        consider it diverged */
    divergedLedgerLimit = 128,

    /** The soft cap on the number of ledger entries in a single reply. */
    softMaxReplyNodes = 8192,

    /** The hard cap on the number of ledger entries in a single reply. */
    hardMaxReplyNodes = 12288,

    /** How many timer intervals a sendq has to stay large before we disconnect
     */
    sendqIntervals = 4,

    /** How many messages on a send queue before we refuse queries */
    dropSendQueue = 192,

    /** How many messages we consider reasonable sustained on a send queue */
    targetSendQueue = 128,

    /** How often to log send queue size */
    sendQueueLogFreq = 64,

    /** How often we check for idle peers (seconds) */
    checkIdlePeers = 4,

    /** The maximum number of levels to search */
    maxQueryDepth = 3,
};

/** Size of buffer used to read from the socket. */
std::size_t constexpr readBufferBytes = 16384;

}  // namespace Tuning

}  // namespace ripple

#endif
