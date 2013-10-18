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

// Fresh endpoints are ones we have seen recently via mtENDPOINTS.
// These are best to give out to someone who needs additional
// connections as quickly as possible, since it is very likely
// that the fresh endpoints have open incoming slots.
//
// Reliable endpoints are ones which are highly likely to be
// connectible over long periods of time. They might not necessarily
// have an incoming slot, but they are good for bootstrapping when
// there are no peers yet. Typically these are what we would want
// to store in a database or local config file for a future launch.

//------------------------------------------------------------------------------

typedef boost::multi_index_container <
    PeerInfo, boost::multi_index::indexed_by <
        boost::multi_index::hashed_unique <
            BOOST_MULTI_INDEX_MEMBER(PeerFinder::PeerInfo,PeerID,id),
                PeerID::hasher>,
        boost::multi_index::hashed_non_unique <
            BOOST_MULTI_INDEX_MEMBER(PeerFinder::PeerInfo,IPAddress,address),
                IPAddress::hasher>
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
            : stopping (false)
            { }

        /** True if we are stopping. */
        bool stopping;

        /** The source we are currently fetching.
            This is used to cancel I/O during program exit.
        */
        SharedPtr <Source> fetchSource;
    };

    typedef SharedData <State> SharedState;

    SharedState m_state;

    //--------------------------------------------------------------------------

    Callback& m_callback;
    Store& m_store;
    Checker& m_checker;
    Journal m_journal;
    Config m_config;

    // The number of fixed peers that are currently connected
    int m_fixedPeersConnected;

    // A list of peers that should always be connected
    typedef std::set <IPAddress> FixedPeers;
    FixedPeers m_fixedPeers;

    // A list of dynamic sources to consult as a fallback
    std::vector <SharedPtr <Source> > m_sources;

    // The current tally of peer slot statistics
    Slots m_slots;

    // Our view of the current set of connected peers.
    Peers m_peers;

    Cache m_cache;

    LegacyEndpointCache m_legacyCache;

    //--------------------------------------------------------------------------

    Logic (
        Callback& callback,
        Store& store,
        Checker& checker,
        Journal journal)
        : m_callback (callback)
        , m_store (store)
        , m_checker (checker)
        , m_journal (journal)
        , m_fixedPeersConnected (0)
        , m_cache (journal)
        , m_legacyCache (store, journal)
    {
    }

    /** Stop the logic.
        This will cancel the current fetch and set the stopping flag
        to `true` to prevent further fetches.
        Thread safety:
            Safe to call from any thread.
    */
    void stop ()
    {
        SharedState::Access state (m_state);
        state->stopping = true;
        if (state->fetchSource != nullptr)
            state->fetchSource->cancel ();
    }

    //--------------------------------------------------------------------------

    // Load persistent state information from the Store
    //
    void load ()
    {
        m_legacyCache.load (get_now());
    }

    // Returns a suitable Endpoint representing us.
    //
    Endpoint thisEndpoint ()
    {
        // Why would someone call this if we don't want incoming?
        bassert (m_config.wantIncoming);

        Endpoint ep;
        ep.address = IPAddress (
            IPAddress::V4 ()).withPort (m_config.listeningPort);
        ep.hops = 0;
        ep.incomingSlotsAvailable = m_slots.inboundSlots;
        ep.incomingSlotsMax = m_slots.inboundSlotsMaximum;
        ep.uptimeMinutes = m_slots.uptimeMinutes();

        return ep;
    }

    // Returns true if the IPAddress contains no invalid data.
    //
    bool validIPAddress (IPAddress const& address)
    {
        if (! address.isPublic())
            return false;
        if (address.port() == 0)
            return false;
        return true;
    }

    // If configured to make outgoing connections, do us in order
    // to bring us up to desired out count.
    //
    void makeOutgoingConnections ()
    {
        if (m_config.connectAutomatically)
            return;

        std::vector <IPAddress> list;

        if (m_slots.outDesired > m_slots.outboundCount)
        {
            int const needed (std::min (
                m_slots.outDesired - m_slots.outboundCount,
                    int (maxAddressesPerAttempt)));
            m_legacyCache.get (needed, list, get_now());
        }

        if (m_fixedPeersConnected < m_fixedPeers.size())
        {
            list.reserve (list.size() + m_fixedPeers.size() - m_fixedPeersConnected);

            for (FixedPeers::const_iterator iter (m_fixedPeers.begin());
                iter != m_fixedPeers.end(); ++iter)
            {
                if (m_peers.get<1>().find (*iter) != m_peers.get<1>().end())
                {
                    list.push_back (*iter);
                }
            }
        }

        if (! list.empty())
            m_callback.connectPeerEndpoints (list);
    }

    // Returns the number of seconds that have elapsed since some baseline
    // event.
    //
    virtual DiscreteTime get_now()
    {
            return 0;
    }

    //--------------------------------------------------------------------------
    //
    // Logic
    //
    //--------------------------------------------------------------------------

    void setConfig (Config const& config)
    {
        m_config = config;
        m_slots.update (m_config);
    }

    void addFixedPeers (std::vector <std::string> const& strings)
    {
        for (std::vector <std::string>::const_iterator iter (strings.begin());
            iter != strings.end(); ++iter)
        {
            IPAddress ep (IPAddress::from_string (*iter));
            if (! ep.empty ())
            {
                m_fixedPeers.insert (ep);
            }
            else
            {
                // VFALCO TODO Attempt name resolution
            }
        }
    }

    void addStaticSource (SharedPtr <Source> const& source)
    {
        fetch (source);
    }

    void addSource (SharedPtr <Source> const& source)
    {
        m_sources.push_back (source);
    }

    // Called periodically to cycle and age the various caches.
    //
    void cycleCache()
    {
        m_cache.cycle(get_now());

        for (Peers::iterator iter (m_peers.begin());
            iter != m_peers.end(); ++iter)
            iter->received.cycle();
    }

    void onPeerConnecting ()
    {
    }

    // Called when a peer connection is established.
    // We are guaranteed that the PeerID is not already in our map.
    // but we are *NOT* guaranteed that the IP isn't. So we need
    // to be careful.
    void onPeerConnected (PeerID const& id,
        IPAddress const& address, bool inbound)
    {
        m_journal.debug << "Peer connected: " << address;
        // If this is outgoing, record the success
        if (! inbound)
            m_legacyCache.checked (address, true);

        std::pair <Peers::iterator, bool> result (
            m_peers.insert (
                PeerInfo (id, address, inbound, get_now())));
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

    //--------------------------------------------------------------------------
    //
    // CachedEndpoints
    //
    //--------------------------------------------------------------------------

    // Returns true if the Endpoint contains no invalid data.
    //
    bool validEndpoint (Endpoint const& endpoint)
    {
        // This function is here in case we add more stuff
        // we want to validate to the Endpoint struct.
        //
        return validIPAddress (endpoint.address);
    }

    // Prunes invalid endpoints from a list.
    //
    void pruneEndpoints (
        std::string const& source, std::vector <Endpoint>& list)
    {
        for (std::vector <Endpoint>::iterator iter (list.begin());
            iter != list.end();)
        {
            if (! validEndpoint (*iter))
            {
                iter = list.erase (iter);
                m_journal.error <<
                    "Invalid endpoint " << iter->address <<
                    " from " << source;
            }
            else
            {
                ++iter;
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

    // Send mtENDPOINTS for each peer as needed
    //
    void sendEndpoints ()
    {
        if (! m_peers.empty())
        {
            m_journal.trace << "Sending endpoints to our peers";

            DiscreteTime const now (get_now());

            for (Peers::iterator iter (m_peers.begin());
                iter != m_peers.end(); ++iter)
            {
                PeerInfo const& peer (*iter);
                if (peer.whenSendEndpoints <= now)
                {
                    sendEndpoints (peer);
                    peer.whenSendEndpoints = now +
                        secondsPerMessage;
                }
            }
        }
    }

    // Called when the Checker completes a connectivity test
    //
    void onCheckEndpoint (PeerID const& id,
        IPAddress address, Checker::Result const& result)
    {
        if (result.error == boost::asio::error::operation_aborted)
            return;

        Peers::iterator iter (m_peers.find (id));
        if (iter != m_peers.end())
        {
            PeerInfo const& peer (*iter);

            // Mark that a check for this peer is finished.
            peer.connectivityCheckInProgress = false;

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

    // Called when a peer sends us the mtENDPOINTS message.
    //
    void onPeerEndpoints (PeerID const& id, std::vector <Endpoint> list)
    {
        Peers::iterator iter (m_peers.find (id));
        bassert (iter != m_peers.end());

        DiscreteTime const now (get_now());
        PeerInfo const& peer (*iter);

        pruneEndpoints (peer.address.to_string(), list);

        // Log at higher severity if this is the first time
        m_journal.stream (peer.whenAcceptEndpoints == 0 ?
            Journal::kInfo : Journal::kTrace) <<
            "Received " << list.size() <<
            " endpoints from " << peer.address;

        // We charge a load penalty if the peer sends us more than 
        // numberOfEndpoints peers in a single message
        if (list.size() > numberOfEndpoints)
        {
            m_journal.warning << "Charging peer " << peer.address <<
                " for sending too many endpoints";

            m_callback.chargePeerLoadPenalty(id);
        }

        m_journal.debug << "Peer " << peer.address <<
            " sent us " << list.size() << " endpoints.";

        // Process each entry
        //
        int neighborCount (0);
        for (std::vector <Endpoint>::const_iterator iter (list.begin());
            iter != list.end(); ++iter)
        {
            Endpoint const& message (*iter);

            // Remember that this peer gave us this address
            peer.received.insert (message.address);

            m_journal.debug << "Received peer " << message.address <<
                " at " << message.hops << " hops.";

            if (message.hops == 0)
            {
                ++neighborCount;
                if (neighborCount == 1)
                {
                    if (peer.connectivityCheckInProgress)
                    {
                        m_journal.warning << "Connectivity check for " <<
                            message.address << "already in progress.";
                    }
                    else if (! peer.checked)
                    {
                        // Mark that a check for this peer is now in progress.
                        peer.connectivityCheckInProgress = true;

                        // Test the peer's listening port before
                        // adding it to the cache for the first time.
                        //                     
                        m_checker.async_test (message.address, bind (
                            &Logic::onCheckEndpoint, this, id,
                                message.address, _1));

                        // Note that we simply discard the first Endpoint
                        // that the neighbor sends when we perform the
                        // listening test. They will just send us another
                        // one in a few seconds.
                    }
                    else if (peer.canAccept)
                    {
                        // We only add to the cache if the neighbor passed the
                        // listening test, else we silently drop their message
                        // since their listening port is misconfigured.
                        //
                        m_cache.insert (message, get_now());
                    }
                }
            }
            else
            {
                m_cache.insert (message, get_now());
            }
        }

        if (neighborCount > 1)
        {
            m_journal.warning << "Peer " << peer.address <<
                " sent " << neighborCount << " entries with hops=0";
            // VFALCO TODO Should we apply load charges?
        }

        peer.whenAcceptEndpoints = now + secondsPerMessage;
    }

    //--------------------------------------------------------------------------
    //
    // LegacyEndpoint
    //
    //--------------------------------------------------------------------------

    // Fetch addresses into the LegacyEndpointCache for bootstrapping
    //
    void fetch (SharedPtr <Source> const& source)
    {
        Source::Results results;

        {
            {
                SharedState::Access state (m_state);
                if (state->stopping)
                    return;
                state->fetchSource = source;
            }

            source->fetch (results, m_journal);

            {
                SharedState::Access state (m_state);
                if (state->stopping)
                    return;
                state->fetchSource = nullptr;
            }
        }

        if (! results.error)
        {
            std::size_t newEntries (0);
            DiscreteTime now (get_now());

            for (std::vector <IPAddress>::const_iterator iter (results.list.begin());
                iter != results.list.end(); ++iter)
            {
                std::pair <LegacyEndpoint const&, bool> result (
                    m_legacyCache.insert (*iter, now));
                if (result.second)
                    ++newEntries;
            }

            m_journal.debug <<
                "Fetched " << results.list.size() <<
                " legacy endpoints (" << newEntries << " new) "
                "from " << source->name();
        }
        else
        {
            m_journal.error <<
                "Fetch " << source->name() << "failed: " <<
                results.error.message();
        }
    }

    // Completion handler for a LegacyEndpoint listening test.
    //
    void onCheckLegacyEndpoint (IPAddress const& endpoint,
        Checker::Result const& result)
    {
        if (result.error == boost::asio::error::operation_aborted)
            return;

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

    void onPeerLegacyEndpoint (IPAddress const& address)
    {
        if (! validIPAddress (address))
            return;
        std::pair <LegacyEndpoint const&, bool> result (
            m_legacyCache.insert (address, get_now()));
        if (result.second)
        {
            // its new
            m_journal.trace << "New legacy endpoint: " << address;

#if 0
            // VFALCO NOTE Temporarily we are doing a check on each
            //             legacy endpoint to test the async code
            //
            m_checker.async_test (address, bind (
                &Logic::onCheckLegacyEndpoint,
                    this, address, _1));
#endif
        }
    }
};

}
}

#endif
