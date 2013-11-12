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

#ifndef RIPPLE_TESTOVERLAY_INITPOLICY_H_INCLUDED
#define RIPPLE_TESTOVERLAY_INITPOLICY_H_INCLUDED

/** A simulated peer to peer network for unit tests. */
namespace TestOverlay
{

//------------------------------------------------------------------------------
//
// InitPolicy
//
// This is called during construction to form the network.
//

/** InitPolicy which does nothing. */
class NoInitPolicy
{
public:
    template <class Network>
    void operator() (Network& network)
    {
    }
};

//------------------------------------------------------------------------------

/** Init policy for a pre-built connected network. */
template <int NumberOfPeers,
          int OutgoingConnectionsPerPeer>
class PremadeInitPolicy
{
public:
    static int const numberOfPeers = NumberOfPeers;
    static int const outgoingConnectionsPerPeer = OutgoingConnectionsPerPeer;

    template <class Network>
    void operator() (Network& network)
    {
        typedef typename Network::Peer      Peer;
        typedef typename Network::Peers     Peers;
        typedef typename Network::Config    Config;
        typedef typename Config::SizeType   SizeType;

        for (SizeType i = 0; i < numberOfPeers; ++i)
            network.createPeer ();

        Peers& peers (network.peers ());
        for (SizeType i = 0; i < numberOfPeers; ++i)
        {
            Peer& peer (*peers [i]);
            for (SizeType j = 0; j < outgoingConnectionsPerPeer; ++j)
            {
                for (;;)
                {
                    SizeType k (network.state ().random ().nextInt (numberOfPeers));
                    if (peer.connect_to (*peers [k]))
                        break;
                }
            }
        }
    }
};

}

#endif
