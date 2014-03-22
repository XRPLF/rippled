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

#ifndef RIPPLE_TESTOVERLAY_STATEBASE_H_INCLUDED
#define RIPPLE_TESTOVERLAY_STATEBASE_H_INCLUDED

namespace TestOverlay
{

/* Base class for state information used by test objects. */
template <class Params>
class StateBase
{
public:
    // Identifies messages and peers.
    // Always starts at 1 and increases incrementally.
    //
    typedef std::uint64_t UniqueID;

    StateBase ()
        : m_random (Params::randomSeedValue)
        , m_peerID (0)
        , m_messageID (0)
    {
    }

    beast::Random& random ()
    {
        return m_random;
    }

    UniqueID nextPeerID ()
    {
        return ++m_peerID;
    }

    UniqueID nextMessageID ()
    {
        return ++m_messageID;
    }

private:
    beast::Random m_random;
    UniqueID m_peerID;
    UniqueID m_messageID;
};

}

#endif
