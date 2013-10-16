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

#ifndef RIPPLE_PEERFINDER_LEGACYENDPOINT_H_INCLUDED
#define RIPPLE_PEERFINDER_LEGACYENDPOINT_H_INCLUDED

namespace ripple {
namespace PeerFinder {

struct LegacyEndpoint
{
    LegacyEndpoint ()
        : whenInserted (0)
        , lastGet(0)
        ,checked (false)
        , canAccept (false)
        { }

    LegacyEndpoint (IPEndpoint const& address_, DiscreteTime now)
        : address (address_)
        , whenInserted (now)
        , lastGet(0)
        { }

    IPEndpoint address;

    // When we inserted the endpoint into the cache
    DiscreteTime mutable whenInserted;

    // When we last used the endpoint for outging connection attempts
    DiscreteTime mutable lastGet;

    // True if we ever tried to connect
    bool mutable checked;

    // The result of the last connect attempt
    bool mutable canAccept;
};

}
}

#endif
