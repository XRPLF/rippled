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

#ifndef RIPPLE_RPC_TUNING_H_INCLUDED
#define RIPPLE_RPC_TUNING_H_INCLUDED

namespace ripple {
namespace RPC {

/** Tuned constants. */
/** @{ */
namespace Tuning {

/** Represents RPC limit parameter values that have a min, default and max. */
struct LimitRange {
    unsigned int rmin, rdefault, rmax;
};

/** Limits for the account_lines command. */
static LimitRange constexpr accountLines = {10, 200, 400};

/** Limits for the account_channels command. */
static LimitRange constexpr accountChannels = {10, 200, 400};

/** Limits for the account_objects command. */
static LimitRange constexpr accountObjects = {10, 200, 400};

/** Limits for the account_offers command. */
static LimitRange constexpr accountOffers = {10, 200, 400};

/** Limits for the book_offers command. */
static LimitRange constexpr bookOffers = {0, 300, 400};

/** Limits for the no_ripple_check command. */
static LimitRange constexpr noRippleCheck = {10, 300, 400};

static int constexpr defaultAutoFillFeeMultiplier = 10;
static int constexpr defaultAutoFillFeeDivisor = 1;
static int constexpr maxPathfindsInProgress = 2;
static int constexpr maxPathfindJobCount = 50;
static int constexpr maxJobQueueClients = 500;
auto constexpr maxValidatedLedgerAge = std::chrono::minutes {2};
static int constexpr maxRequestSize = 1000000;

/** Maximum number of pages in one response from a binary LedgerData request. */
static int constexpr binaryPageLength = 2048;

/** Maximum number of pages in one response from a Json LedgerData request. */
static int constexpr jsonPageLength = 256;

/** Maximum number of pages in a LedgerData response. */
inline int constexpr pageLength(bool isBinary)
{
    return isBinary ? binaryPageLength : jsonPageLength;
}

/** Maximum number of source currencies allowed in a path find request. */
static int constexpr max_src_cur = 18;

/** Maximum number of auto source currencies in a path find request. */
static int constexpr max_auto_src_cur = 88;

} // Tuning
/** @} */

} // RPC
} // ripple

#endif
