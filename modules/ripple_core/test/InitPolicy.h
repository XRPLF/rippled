//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_CORE_TEST_INITPOLICY_H_INCLUDED
#define RIPPLE_CORE_TEST_INITPOLICY_H_INCLUDED

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
