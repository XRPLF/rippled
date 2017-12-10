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

#ifndef RIPPLE_RESOURCE_TUNING_H_INCLUDED
#define RIPPLE_RESOURCE_TUNING_H_INCLUDED

namespace ripple {
namespace Resource {

/** Tunable constants. */
enum
{
    // Balance at which a warning is issued
     warningThreshold           = 5000

    // Balance at which the consumer is disconnected
    ,dropThreshold              = 15000

    // The number of seconds until an inactive table item is removed
    ,secondsUntilExpiration     = 300

    // The number of seconds in the exponential decay window
    // (This should be a power of two)
    ,decayWindowSeconds         = 32

    // The minimum balance required in order to include a load source in gossip
    ,minimumGossipBalance       = 1000

    // Number of seconds until imported gossip expires
    ,gossipExpirationSeconds    = 30
};

}
}

#endif
