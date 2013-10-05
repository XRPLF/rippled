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

#ifndef RIPPLE_PEERFINDER_LOGIC_H_INCLUDED
#define RIPPLE_PEERFINDER_LOGIC_H_INCLUDED

namespace ripple {
namespace PeerFinder {

typedef boost::multi_index_container <
    PeerInfo, boost::multi_index::indexed_by <
        boost::multi_index::hashed_unique <
            BOOST_MULTI_INDEX_MEMBER(PeerFinder::PeerInfo,PeerID,id),
                PeerID::hasher>,
        boost::multi_index::hashed_unique <
            BOOST_MULTI_INDEX_MEMBER(PeerFinder::PeerInfo,beast::IPEndpoint,address),
                IPEndpoint::hasher>
    >
> Peers;

//------------------------------------------------------------------------------

/** The Logic for maintaining the list of Peer addresses.
    We keep this in a separate class so it can be instantiated
    for unit tests.
*/
class Logic
{
public:
    struct State
    {
        State ()
        {
        }

        // Fresh endpoints are ones we have seen recently via mtENDPOINTS.
        // These are best to give out to someone who needs additional
        // connections as quickly as possible, since it is very likely
        // that the fresh endpoints have open incoming slots.
        //
        //EndpointCache fresh;

        // Reliable endpoints are ones which are highly likely to be
        // connectible over long periods of time. They might not necessarily
        // have an incoming slot, but they are good for bootstrapping when
        // there are no peers yet. Typically these are what we would want
        // to store in a database or local config file for a future launch.
        //Endpoints reliable;
    };

    //----------------------------------------------------------------------

    Callback& m_callback;
    Store& m_store;
    Checker& m_checker;
    Journal m_journal;
    Config m_config;

    // A list of dynamic sources consulted as a fallback
    // VFALCO TODO Replace with SharedPtr <Source>
    std::vector <ScopedPointer <Source> > m_sources;

    // The current tally of peer slot statistics
    Slots m_slots;

    // Our view of the current set of connected peers.
    Peers m_peers;

    LegacyEndpointCache m_legacyCache;

    //----------------------------------------------------------------------

    Logic (
        Callback& callback,
        Store& store,
        Checker& checker,
        Journal journal)
        : m_callback (callback)
        , m_store (store)
        , m_checker (checker)
        , m_journal (journal)
        , m_legacyCache (store, journal)
    {
    }

    //----------------------------------------------------------------------

    // Load persistent state information from the Store
    //
    void load ()
    {
        m_legacyCache.load();
    }

    // Returns a suitable Endpoint representing us.
    //
    Endpoint thisEndpoint ()
    {
        // Why would someone call this if we don't want incoming?
        bassert (m_config.wantIncoming);

        Endpoint ep;
        // ep.address = ?
        ep.port = m_config.listeningPort;
        ep.hops = 0;
        ep.incomingSlotsAvailable = m_slots.inboundSlots;
        ep.incomingSlotsMax = m_slots.inboundSlotsMaximum;
        ep.uptimeMinutes = m_slots.uptimeMinutes();

        return ep;
    }

    // Returns true if the IPEndpoint contains no invalid data.
    //
    bool validIPEndpoint (IPEndpoint const& address)
    {
        if (! address.isPublic())
            return false;
        if (address.port() == 0)
            return false;
        return true;
    }

    // Returns true if the Endpoint contains no invalid data.
    //
    bool validEndpoint (Endpoint const& endpoint)
    {
        return validIPEndpoint (
            endpoint.address.withPort (endpoint.port));
    }

    // Prunes invalid endpoints from a list
    //
    void pruneEndpoints (std::vector <Endpoint>& list)
    {
        for (std::vector <Endpoint>::iterator iter (list.begin());
            iter != list.end(); ++iter)
        {
            while (! validEndpoint (*iter))
            {
                m_journal.error << "Pruned invalid endpoint " << iter->address;
                iter = list.erase (iter);
                if (iter == list.end())
                    break;
            }
        }
    }

    // Send mtENDPOINTS for the specified peer
    //
    void sendEndpoints (PeerInfo const& peer)
    {
        typedef std::vector <Endpoint> List;
        std::vector <Endpoint> endpoints;

        // fill in endpoints

        // Add us to the list if we want incoming
        if (m_slots.inboundSlots > 0)
            endpoints.push_back (thisEndpoint ());

        if (! endpoints.empty())
            m_callback.sendPeerEndpoints (peer.id, endpoints);
    }

    // Assembles a list from the legacy endpoint container
    //
    void createLegacyEndpointList (std::vector <IPEndpoint>& list)
    {
    }

    // Make outgoing connections to bring us up to desired out count
    //
    void makeOutgoingConnections ()
    {
        if (m_slots.outDesired > m_slots.outboundCount)
        {
            int const needed (std::min (
                m_slots.outDesired - m_slots.outboundCount,
                    int (maxAddressesPerAttempt)));
            std::vector <IPEndpoint> list;
            m_legacyCache.get (needed, list);

#if RIPPLE_USE_PEERFINDER
            m_callback.connectPeerEndpoints (list);
#endif
        }
    }

    // Fetch the list of IPEndpoint from the specified source
    //
    void fetch (Source& source)
    {
        m_journal.debug << "Fetching " << source.name();

#if 0
        Source::IPEndpoints endpoints;
        source.fetch (endpoints, m_journal);

        if (! endpoints.empty())
        {
            for (Source::IPEndpoints::const_iterator iter (endpoints.begin());
                iter != endpoints.end(); ++iter)
                m_legacyCache->insert (*iter);

            if (m_legacyCache->size() > (legacyEndpointCacheSize/2))
            {
                m_legacyCache.swap();
                m_legacyCache->clear();
            }
        }
#endif
    }

    //----------------------------------------------------------------------
    //
    // Logic
    //

    void setConfig (Config const& config)
    {
        m_config = config;
        m_slots.update (m_config);
    }

    void addStaticSource (Source* source)
    {
        ScopedPointer <Source> p (source);
        fetch (*source);
    }

    void addSource (Source* source)
    {
        m_sources.push_back (source);
    }

    void onUpdate ()
    {
        m_journal.debug << "Processing Update";
    }

    //--------------------------------------------------------------------------
    //
    // LegacyEndpoint
    //

    // Completion handler for a LegacyEndpoint listening test.
    //
    void onCheckLegacyEndpoint (IPEndpoint const& endpoint,
        Checker::Result const& result)
    {
        if (result.error == boost::asio::error::operation_aborted)
            return;

        RelativeTime const now (RelativeTime::fromStartup());

        if (! result.error)
        {
            if (result.canAccept)
                m_journal.info << "Legacy address " << endpoint <<
                    " passed listening test";
            else
                m_journal.warning << "Legacy address " << endpoint <<
                    " cannot accept incoming connections";
        }
        else
        {
            m_journal.error << "Listening test for legacy address " <<
                endpoint << " failed: " << result.error.message();
        }
    }

    void onPeerLegacyEndpoint (IPEndpoint const& address)
    {
        if (! validIPEndpoint (address))
            return;
        std::pair <LegacyEndpoint&, bool> result (
            m_legacyCache.insert (address));
        if (result.second)
        {
            // its new
            m_journal.trace << "New legacy endpoint: " << address;

            // VFALCO NOTE Temporarily we are doing a check on each
            //             legacy endpoint to test the async code
            //
            m_checker.async_test (address, bind (
                &Logic::onCheckLegacyEndpoint,
                    this, address, _1));
        }
    }

    //--------------------------------------------------------------------------

    // Send mtENDPOINTS for each peer as needed
    //
    void sendEndpoints ()
    {
        if (! m_peers.empty())
        {
            m_journal.debug << "Sending mtENDPOINTS";

            RelativeTime const now (RelativeTime::fromStartup());

            for (Peers::iterator iter (m_peers.begin());
                iter != m_peers.end(); ++iter)
            {
                PeerInfo const& peer (*iter);
                if (peer.whenSendEndpoints <= now)
                {
                    sendEndpoints (peer);
                    peer.whenSendEndpoints = now +
                        RelativeTime (secondsPerEndpoints);
                }
            }
        }
    }

    // Called when a peer connection is established.
    // We are guaranteed that the PeerID is not already in our map.
    //
    void onPeerConnected (PeerID const& id,
        IPEndpoint const& address, bool inbound)
    {
        m_journal.debug << "Peer connected: " << address;
        // If this is outgoing, record the success
        if (! inbound)
            m_legacyCache.checked (address, true);
        std::pair <Peers::iterator, bool> result (
            m_peers.insert (
                PeerInfo (id, address, inbound)));
        bassert (result.second);
        m_slots.addPeer (m_config, inbound);
    }

    // Called when a peer is disconnected.
    // We are guaranteed to get this exactly once for each
    // corresponding call to onPeerConnected.
    //
    void onPeerDisconnected (PeerID const& id)
    {
        Peers::iterator iter (m_peers.find (id));
        bassert (iter != m_peers.end());
        PeerInfo const& peer (*iter);
        m_journal.debug << "Peer disconnected: " << peer.address;
        m_slots.dropPeer (m_config, peer.inbound);
        m_peers.erase (iter);
    }

    // Called when the Checker completes a connectivity test
    //
    void onCheckEndpoint (PeerID const& id,
        IPEndpoint address, Checker::Result const& result)
    {
        if (result.error == boost::asio::error::operation_aborted)
            return;

        Peers::iterator iter (m_peers.find (id));
        if (iter != m_peers.end())
        {
            PeerInfo const& peer (*iter);

            if (! result.error)
            {
                peer.checked = true;
                peer.canAccept = result.canAccept;

                if (peer.canAccept)
                    m_journal.info << "Peer " << peer.address <<
                        " passed listening test";
                else
                    m_journal.warning << "Peer " << peer.address <<
                        " cannot accept incoming connections";
            }
            else
            {
                // VFALCO TODO Should we retry depending on the error?
                peer.checked = true;
                peer.canAccept = false;

                m_journal.error << "Listening test for " <<
                    peer.address << " failed: " <<
                    result.error.message();
            }
        }
        else
        {
            // The peer disconnected before we finished the check
            m_journal.debug << "Finished listening test for " <<
                id << " but the peer disconnected. ";
        }
    }

    // Processes a list of Endpoint received from a peer.
    //
    void onPeerEndpoints (PeerID const& id, std::vector <Endpoint> endpoints)
    {
        pruneEndpoints (endpoints);

        Peers::iterator iter (m_peers.find (id));
        bassert (iter != m_peers.end());

        RelativeTime const now (RelativeTime::fromStartup());
        PeerInfo const& peer (*iter);

        if (now >= peer.whenReceiveEndpoints)
        {
            m_journal.debug << "Received " << endpoints.size() <<
                "Endpoint descriptors from " << peer.address;

            // We charge a load penalty if the peer sends us more than 
            // numberOfEndpoints peers in a single message
            if (endpoints.size() > numberOfEndpoints)
            {
                m_journal.warning << "Charging peer " << peer.address <<
                    " for sending too many endpoints";
                        
                m_callback.chargePeerLoadPenalty(id);
            }

            // process the list
            {
                bool foundZeroHops (false);
                bool chargedPenalty (false);
                for (std::vector <Endpoint>::const_iterator iter (endpoints.begin());
                    iter != endpoints.end(); ++iter)
                {
                    Endpoint const& endpoint (*iter);
                    if (endpoint.hops == 0)
                    {
                        if (! foundZeroHops)
                        {
                            foundZeroHops = true;
                            IPEndpoint const address (
                                endpoint.address.withPort (endpoint.port));
                            m_checker.async_test (address, bind (&Logic::onCheckEndpoint,
                                this, id, address, _1));
                        }
                        else if (! chargedPenalty)
                        {
                            // Only charge them once (?)
                            chargedPenalty = true;
                            // More than one zero-hops message?!
                            m_journal.warning << "Charging peer " << peer.address <<
                                " for sending more than one hops==0 endpoint";
                            m_callback.chargePeerLoadPenalty (id);
                        }
                    }
                }
            }

            peer.whenReceiveEndpoints = now + secondsPerEndpoints;
        }
        else
        {
            m_journal.warning << "Charging peer " << peer.address <<
                " for sending too quickly";
            m_callback.chargePeerLoadPenalty (id);
        }
    }
};

}
}

#endif
