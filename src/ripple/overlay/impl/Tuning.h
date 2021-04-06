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

#ifndef RIPPLE_OVERLAY_TUNING_H_INCLUDED
#define RIPPLE_OVERLAY_TUNING_H_INCLUDED

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

    /** The maximum number of ledger entries in a single
        reply */
    maxReplyNodes = 8192,

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
