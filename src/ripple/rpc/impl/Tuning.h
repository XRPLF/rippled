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
static LimitRange const accountLines = {10, 200, 400};

/** Limits for the account_channels command. */
static LimitRange const accountChannels = {10, 200, 400};

/** Limits for the account_objects command. */
static LimitRange const accountObjects = {10, 200, 400};

/** Limits for the account_offers command. */
static LimitRange const accountOffers = {10, 200, 400};

/** Limits for the book_offers command. */
static LimitRange const bookOffers = {0, 300, 400};

/** Limits for the no_ripple_check command. */
static LimitRange const noRippleCheck = {10, 300, 400};

static int const defaultAutoFillFeeMultiplier = 10;
static int const defaultAutoFillFeeDivisor = 1;
static int const maxPathfindsInProgress = 2;
static int const maxPathfindJobCount = 50;
static int const maxJobQueueClients = 500;
auto constexpr maxValidatedLedgerAge = std::chrono::minutes {2};
static int const maxRequestSize = 1000000;

/** Maximum number of pages in one response from a binary LedgerData request. */
static int const binaryPageLength = 2048;

/** Maximum number of pages in one response from a Json LedgerData request. */
static int const jsonPageLength = 256;

/** Maximum number of pages in a LedgerData response. */
inline int pageLength(bool isBinary)
{
    return isBinary ? binaryPageLength : jsonPageLength;
}

/** Maximum number of source currencies allowed in a path find request. */
static int const max_src_cur = 18;

/** Maximum number of auto source currencies in a path find request. */
static int const max_auto_src_cur = 88;

} // Tuning
/** @} */

} // RPC
} // ripple

#endif
