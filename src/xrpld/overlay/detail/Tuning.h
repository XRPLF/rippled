#pragma once

namespace xrpl::Tuning {

/** How many ledgers off a server can be and we will
    still consider it converged */
static constexpr auto kConvergedLedgerLimit = 24;

/** How many ledgers off a server has to be before we
    consider it diverged */
static constexpr auto kDivergedLedgerLimit = 128;

/** The soft cap on the number of ledger entries in a single reply. */
static constexpr auto kSoftMaxReplyNodes = 8192;

/** The hard cap on the number of ledger entries in a single reply. */
static constexpr auto kHardMaxReplyNodes = 12288;

/** How many timer intervals a sendq has to stay large before we disconnect */
static constexpr auto kSendqIntervals = 4;

/** How many messages on a send queue before we refuse queries */
static constexpr auto kDropSendQueue = 192;

/** How many messages we consider reasonable sustained on a send queue */
static constexpr auto kTargetSendQueue = 128;

/** How often to log send queue size */
static constexpr auto kSendQueueLogFreq = 64;

/** How often we check for idle peers (seconds) */
static constexpr auto kCheckIdlePeers = 4;

/** The maximum number of levels to search */
static constexpr auto kMaxQueryDepth = 3;

/** Size of buffer used to read from the socket. */
constexpr std::size_t kReadBufferBytes = 16384;

}  // namespace xrpl::Tuning
