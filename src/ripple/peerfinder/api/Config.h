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

#ifndef RIPPLE_PEERFINDER_CONFIG_H_INCLUDED
#define RIPPLE_PEERFINDER_CONFIG_H_INCLUDED

namespace ripple {
namespace PeerFinder {

/** Configuration for the Manager. */
struct Config
{
    Config ();

    static int const minOutCount = 10;
    static int const outPercent = 15;
    int maxPeerCount;

    /** True if we want to accept incoming connections. */
    bool wantIncoming;

    /** True if we want to establish connections automatically */
    bool connectAutomatically;

    uint16 listeningPort;
    std::string featureList;

    /** Write the configuration into a property stream */
    void onWrite(PropertyStream::Map& map);
};

}
}

#endif
