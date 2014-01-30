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

#ifndef RIPPLE_PEERFINDER_SLOTS_H_INCLUDED
#define RIPPLE_PEERFINDER_SLOTS_H_INCLUDED

namespace ripple {
namespace PeerFinder {

class Slots
{
public:
    explicit Slots (DiscreteClock <DiscreteTime> clock)
        : m_clock (clock)
        , m_inboundSlots (0)
        , m_inboundActive (0)
        , m_outboundSlots (0)
        , m_outboundActive (0)
        , m_fixedPeerConnections (0)
        , m_clusterPeerConnections (0)
        , m_acceptCount (0)
        , m_connectCount (0)
        , m_closingCount (0)
    {
#if 0
        std::random_device rd;
        std::mt19937 gen (rd());
        m_roundingThreshold =
            std::generate_canonical <double, 10> (gen);
#else
        m_roundingThreshold = Random::getSystemRandom().nextDouble();
#endif
    }

    /** Called when the config is set or changed. */
    void onConfig (Config const& config)
    {
        // Calculate the number of outbound peers we want. If we dont want or can't
        // accept incoming, this will simply be equal to maxPeers. Otherwise
        // we calculate a fractional amount based on percentages and pseudo-randomly
        // round up or down.
        //
        if (config.wantIncoming)
        {
            // Round outPeers upwards using a Bernoulli distribution
            m_outboundSlots = std::floor (config.outPeers);
            if (m_roundingThreshold < (config.outPeers - m_outboundSlots))
                ++m_outboundSlots;
        }
        else
        {
            m_outboundSlots = config.maxPeers;
        }

        // Calculate the largest number of inbound connections we could take.
        if (config.maxPeers >= m_outboundSlots)
            m_inboundSlots = config.maxPeers - m_outboundSlots;
        else
            m_inboundSlots = 0;
    }

    /** Returns the number of accepted connections that haven't handshaked. */
    int acceptCount() const
    {
        return m_acceptCount;
    }

    /** Returns the number of connection attempts currently active. */
    int connectCount() const
    {
        return m_connectCount;
    }

    /** Returns the number of connections that are gracefully closing. */
    int closingCount () const
    {
        return m_closingCount;
    }

    /** Returns the total number of inbound slots. */
    int inboundSlots () const
    {
        return m_inboundSlots;
    }

    /** Returns the total number of outbound slots. */
    int outboundSlots () const
    {
        return m_outboundSlots;
    }

    /** Returns the number of inbound peers assigned an open slot. */
    int inboundActive () const
    {
        return m_inboundActive;
    }

    /** Returns the number of outbound peers assigned an open slot.
        Fixed peers do not count towards outbound slots used.
    */
    int outboundActive () const
    {
        return m_outboundActive;
    }

    /** Returns the total number of active peers excluding fixed peers. */
    int totalActive () const
    {
        return m_inboundActive + m_outboundActive;
    }

    /** Returns the number of unused inbound slots.
        Fixed peers do not deduct from inbound slots or count towards totals.
    */
    int inboundSlotsFree () const
    {
        if (m_inboundActive < m_inboundSlots)
            return m_inboundSlots - m_inboundActive;
        return 0;
    }

    /** Returns the number of unused outbound slots.
        Fixed peers do not deduct from outbound slots or count towards totals.
    */
    int outboundSlotsFree () const
    {
        if (m_outboundActive < m_outboundSlots)
            return m_outboundSlots - m_outboundActive;
        return 0;
    }

    /** Returns the number of fixed peers we have connections to
        Fixed peers do not deduct from outbound or inbound slots or count 
        towards totals.
    */
    int fixedPeers () const
    {
        return m_fixedPeerConnections;
    }

    /** Returns the number of cluster peers we have connections to
        Cluster nodes do not deduct from outbound or inbound slots or
        count towards totals, but they are tracked if they are also
        configured as fixed peers.
    */
    int clusterPeers () const
    {
        return m_clusterPeerConnections;
    }
    
    //--------------------------------------------------------------------------

    /** Called when an inbound connection is accepted. */
    void onPeerAccept ()
    {
        ++m_acceptCount;
    }

    /** Called when a new outbound connection is attempted. */
    void onPeerConnect ()
    {
        ++m_connectCount;
    }

    /** Determines if an outbound slot is available and assigns it */
    HandshakeAction grabOutboundSlot(bool self, bool fixed, 
        bool available, bool cluster)
    {
        // If this is a connection to ourselves, we bail.
        if (self)
        {
            ++m_closingCount;
            return doClose;
        }

        // Fixed and cluster peers are tracked but are not subject
        // to limits and don't consume slots. They are always allowed
        // to connect.
        if (fixed || cluster)
        {
            if (fixed)
                ++m_fixedPeerConnections;
                
            if (cluster)
                ++m_clusterPeerConnections;
            
            return doActivate;
        }
        
        // If we don't have any slots for this peer then reject the
        // connection.
        if (!available)
        {
            ++m_closingCount;
            return doClose;
        }

        ++m_outboundActive;
        return doActivate;
    }

    /** Determines if an inbound slot is available and assigns it */
    HandshakeAction grabInboundSlot(bool self, bool fixed, 
        bool available, bool cluster)
    {
        // If this is a connection to ourselves, we bail.
        if (self)
        {
            ++m_closingCount;
            return doClose;
        }

        // Fixed and cluster peers are tracked but are not subject
        // to limits and don't consume slots. They are always allowed
        // to connect.
        if (fixed || cluster)
        {
            if (fixed)
                ++m_fixedPeerConnections;
                
            if (cluster)
                ++m_clusterPeerConnections;
            
            return doActivate;
        }
        
        // If we don't have any slots for this peer then reject the
        // connection and redirect them.
        if (!available)
        {
            ++m_closingCount;
            return doRedirect;
        }
        
        ++m_inboundActive;
        return doActivate;
    }

    /** Called when a peer handshakes.
        Returns the disposition for this peer, including whether we should
        activate the connection, issue a redirect or simply close it.
    */
    HandshakeAction onPeerHandshake (bool inbound, bool self, bool fixed, bool cluster)
    {
        if (cluster)
            return doActivate;
                
        if (inbound)
        {
            // Must not be zero!
            consistency_check (m_acceptCount > 0);
            --m_acceptCount;

            return grabInboundSlot (self, fixed, 
                inboundSlotsFree () > 0, cluster);
        }

        // Must not be zero!
        consistency_check (m_connectCount > 0);
        --m_connectCount;

        return grabOutboundSlot (self, fixed, 
            outboundSlotsFree () > 0, cluster);
    }

    /** Called when a peer socket is closed gracefully. */
    void onPeerGracefulClose ()
    {
        // Must not be zero!
        consistency_check (m_closingCount > 0);
        --m_closingCount;
    }

    /** Called when a peer socket is closed.
        A value of `true` for active means the peer was assigned an open slot.
    */
    void onPeerClosed (bool inbound, bool active, bool fixed, bool cluster)
    {
        if (active)
        {
            if (inbound)
            {
                // Fixed peer connections are tracked but don't count towards slots
                if (fixed || cluster)
                {
                    if (fixed)
                    {
                        consistency_check (m_fixedPeerConnections > 0);
                        --m_fixedPeerConnections;
                    }
                    
                    if (cluster)
                    {
                        consistency_check (m_clusterPeerConnections > 0);
                        --m_clusterPeerConnections;
                    }
                }
                else
                {
                    // Must not be zero!
                    consistency_check (m_inboundActive > 0);
                    --m_inboundActive;
                }
            }
            else
            {
                // Fixed peer connections are tracked but don't count towards slots
                if (fixed || cluster)
                {
                    if (fixed)
                    {
                        consistency_check (m_fixedPeerConnections > 0);
                        --m_fixedPeerConnections;
                    }
                    
                    if (cluster)
                    {
                        consistency_check (m_clusterPeerConnections > 0);
                        --m_clusterPeerConnections;
                    }
                }
                else
                {
                    // Must not be zero!
                    consistency_check (m_outboundActive > 0);
                    --m_outboundActive;
                }
            }
        }
        else if (inbound)
        {
            // Must not be zero!
            consistency_check (m_acceptCount > 0);
            --m_acceptCount;
        }
        else
        {
            // Must not be zero!
            consistency_check (m_connectCount > 0);
            --m_connectCount;
        }
    }

    //--------------------------------------------------------------------------

    /** Returns the number of new connection attempts we should make. */
    int additionalAttemptsNeeded () const
    {
        // Don't go over the maximum concurrent attempt limit
        if (m_connectCount >= Tuning::maxConnectAttempts)
            return 0;
        int needed (outboundSlotsFree ());
        // This is the most we could attempt right now
        int const available (
            Tuning::maxConnectAttempts - m_connectCount);
        return std::min (needed, available);
    }

    /** Returns true if the slot logic considers us "connected" to the network. */
    bool isConnectedToNetwork () const
    {
        // We will consider ourselves connected if we have reached
        // the number of outgoing connections desired, or if connect
        // automatically is false.
        //
        // Fixed peers do not count towards the active outgoing total.

        if (m_outboundSlots > 0)
            return false;

        return true;
    }

    /** Output statistics. */
    void onWrite (PropertyStream::Map& map)
    {
        map ["accept"]  = acceptCount();
        map ["connect"] = connectCount();
        map ["close"]   = closingCount();
        map ["in"]      << inboundActive() << "/" << inboundSlots();
        map ["out"]     << outboundActive() << "/" << outboundSlots();
        map ["fixed"]   = fixedPeers();
    }

    /** Records the state for diagnostics. */
    std::string state_string () const
    {
        std::stringstream ss;
        ss <<
            outboundActive() << "/" << outboundSlots() << " out, " <<
            inboundActive() << "/" << inboundSlots() << " in, " <<
            connectCount() << " connecting, " <<
            closingCount() << " closing"
            ;
        return ss.str();
    }

    //--------------------------------------------------------------------------

private:
    DiscreteClock <DiscreteTime> m_clock;

    /** Total number of inbound slots. */
    int m_inboundSlots;

    /** Number of inbound slots assigned to active peers. */
    int m_inboundActive;

    /** Total number of outbound slots. */
    int m_outboundSlots;

    /** Number of outbound slots assigned to active peers. */
    int m_outboundActive;

    /** Number of fixed peer connections that we have. */
    int m_fixedPeerConnections;

    /** Number of cluster peer connections that we have. */
    int m_clusterPeerConnections;
    
    // Number of inbound connections that are
    // not active or gracefully closing.
    int m_acceptCount;

    // Number of outgoing connections that are
    // not active or gracefully closing.
    //
    int m_connectCount;

    // Number of connections that are gracefully closing.
    int m_closingCount;

    // Number of connections that are currently assigned an open slot
    //int m_activeCount;

    /** Fractional threshold below which we round down.
        This is used to round the value of Config::outPeers up or down in
        such a way that the network-wide average number of outgoing
        connections approximates the recommended, fractional value.
    */
    double m_roundingThreshold;
};

}
}

#endif
