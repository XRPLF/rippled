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

#define FIX_ME 0

#include "SlotImp.h"

#include <unordered_map>

namespace ripple {
namespace PeerFinder {

/** The Logic for maintaining the list of Peer addresses.
    We keep this in a separate class so it can be instantiated
    for unit tests.
*/
class Logic
{
public:
    // Maps remote endpoints to slots. Since a slot has a
    // remote endpoint upon construction, this holds all slots.
    // 
    typedef std::unordered_map <IP::Endpoint, Slot::ptr> SlotsByIP;



    // Maps live sockets to the metadata
    typedef boost::unordered_map <IPAddress, Peer> Peers;

    // A set of unique Ripple public keys
    typedef std::set <PeerID> Keys;

    // A set of non-unique IPAddresses without ports, used
    // to filter duplicates when making outgoing connections.
    typedef std::multiset <IP::Endpoint> ConnectedAddresses;

    struct State
    {
        State (
            Store* store,
            DiscreteClock <DiscreteTime> clock,
            Journal journal)
            : stopping (false)
            , slots (clock)
            , livecache (clock, Journal (
                journal, Reporting::livecache))
            , bootcache (*store, clock, Journal (
                journal, Reporting::bootcache))
        {
        }

        // True if we are stopping.
        bool stopping;

        // The source we are currently fetching.
        // This is used to cancel I/O during program exit.
        SharedPtr <Source> fetchSource;

        // Configuration settings
        Config config;

        // Slot counts and other aggregate statistics.
        Slots slots;

        // Live livecache from mtENDPOINTS messages
        Livecache livecache;

        // LiveCache of addresses suitable for gaining initial connections
        Bootcache bootcache;

        // Metadata for live sockets
        Peers peers;

        // Holds all slots
        SlotsByIP slots_by_ip;

        // The addresses (but not port) we are connected to. This includes
        // outgoing connection attempts. Note that this set can contain
        // duplicates (since the port is not set)
        ConnectedAddresses connected_addresses; 

        // Set of public keys belonging to active peers
        Keys keys;
    };

    typedef SharedData <State> SharedState;

    Journal m_journal;
    SharedState m_state;
    DiscreteClock <DiscreteTime> m_clock;
    Callback& m_callback;
    Store& m_store;
    Checker& m_checker;

    // A list of peers that should always be connected
    typedef boost::unordered_map <std::string, FixedPeer> FixedPeers;
    FixedPeers m_fixedPeers;

    // A list of dynamic sources to consult as a fallback
    std::vector <SharedPtr <Source>> m_sources;

    //--------------------------------------------------------------------------

    Logic (
        DiscreteClock <DiscreteTime> clock,
        Callback& callback,
        Store& store,
        Checker& checker,
        Journal journal)
        : m_journal (journal, Reporting::logic)
        , m_state (&store, clock, journal)
        , m_clock (clock)
        , m_callback (callback)
        , m_store (store)
        , m_checker (checker)
    {
        setConfig (Config ());
    }

    // Load persistent state information from the Store
    //
    void load ()
    {
        SharedState::Access state (m_state);

        state->bootcache.load ();
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

    /** Returns a new set of connection addresses from the live cache. */
    IPAddresses fetch_livecache (std::size_t needed, SharedState::Access& state)
    {
        Endpoints endpoints (state->livecache.fetch_unique());
        Endpoints temp;
        temp.reserve (endpoints.size ());

        {
            // Remove the addresses we are currently connected to
            struct LessWithoutPortSet
            {
                bool operator() (
                    Endpoint const& lhs, IPAddress const& rhs) const
                {
                    return lhs.address.address()  < rhs.address();
                }
                bool operator() (
                    Endpoint const& lhs, Endpoint const& rhs) const
                {
                    return lhs.address.address() < rhs.address.address();
                }
                bool operator() (
                    IPAddress const& lhs, Endpoint const& rhs) const
                {
                    return lhs.address() < rhs.address.address();
                }
                bool operator() (
                    IPAddress const& lhs, IPAddress const& rhs) const
                {
                    return lhs.address() < rhs.address();
                }
            };
            std::set_difference (endpoints.begin (), endpoints.end (),
                state->connected_addresses.begin (), state->connected_addresses.end (),
                    std::back_inserter (temp), LessWithoutPortSet ());
            std::swap (endpoints, temp);
            temp.clear ();
        }

        {
            // Sort by hops descending
            struct LessHops
            {
                bool operator() (Endpoint const& lhs, Endpoint const& rhs) const
                {
                    return lhs.hops > rhs.hops;
                }
            };
            std::sort (endpoints.begin (), endpoints.end (), LessHops ());
        }

        if (endpoints.size () > needed)
            endpoints.resize (needed);

        IPAddresses result;
        result.reserve (endpoints.size ());
        for (Endpoints::const_iterator iter (endpoints.begin ());
            iter != endpoints.end (); ++iter)
            result.push_back (iter->address);
        return result;
    }

    //--------------------------------------------------------------------------

    /** Returns a new set of connection addresses from the live cache. */
    IPAddresses fetch_bootcache (std::size_t needed, SharedState::Access& state)
    {
        // Get everything
        Bootcache::Endpoints endpoints (state->bootcache.fetch ());
        struct LessRank
        {
            bool operator() (Bootcache::Endpoint const& lhs,
                Bootcache::Endpoint const& rhs) const
            {
                if (lhs.uptime() > rhs.uptime())
                    return true;
                else if (lhs.uptime() <= rhs.uptime() && rhs.uptime() != 0)
                    return false;
                if (lhs.valence() > rhs.valence())
                    return true;
                return false;
            }
        };

        {
            // Sort ignoring port
            struct LessWithoutPort
            {
                bool operator() (Bootcache::Endpoint const& lhs,
                    Bootcache::Endpoint const& rhs) const
                {
                    if (lhs.address().at_port (0) < rhs.address().at_port (0))
                        return true;
                    // Break ties by preferring higher ranks
                    //return m_rank (lhs, rhs);
                    return false;
                }

                LessRank m_rank;
            };
            std::sort (endpoints.begin (), endpoints.end (), LessWithoutPort());
        }

        Bootcache::Endpoints temp;
        temp.reserve (endpoints.size ());

        {
            // Remove all but the first unique addresses ignoring port
            struct EqualWithoutPort
            {
                bool operator() (Bootcache::Endpoint const& lhs,
                    Bootcache::Endpoint const& rhs) const
                {
                    return lhs.address().at_port (0) ==
                        rhs.address().at_port (0);
                }
            };

            std::unique_copy (endpoints.begin (), endpoints.end (),
                std::back_inserter (temp), EqualWithoutPort ());
            std::swap (endpoints, temp);
            temp.clear ();
        }

        {
            // Remove the addresses we are currently connected to
            struct LessWithoutPortSet
            {
                bool operator() (Bootcache::Endpoint const& lhs,
                    IPAddress const& rhs) const
                {
                    return lhs.address().at_port (0) < rhs.at_port (0);
                }
                bool operator() (Bootcache::Endpoint const& lhs,
                    Bootcache::Endpoint const& rhs) const
                {
                    return lhs.address().at_port (0) <
                        rhs.address().at_port (0);
                }
                bool operator() (IPAddress const& lhs,
                    Bootcache::Endpoint const& rhs) const
                {
                    return lhs.at_port (0) < rhs.address().at_port (0);
                }
                bool operator() (IPAddress const& lhs,
                    IPAddress const& rhs) const
                {
                    return lhs.at_port (0) < rhs.at_port (0);
                }
            };
            std::set_difference (endpoints.begin (), endpoints.end (),
                state->connected_addresses.begin (), state->connected_addresses.end (),
                    std::back_inserter (temp), LessWithoutPortSet ());
            std::swap (endpoints, temp);
            temp.clear ();
        }

        {
            // Sort by rank descending
            std::sort (endpoints.begin (), endpoints.end (), LessRank ());
        }

        if (endpoints.size () > needed)
            endpoints.resize (needed);

        IPAddresses result;
        result.reserve (endpoints.size ());
        for (Bootcache::Endpoints::const_iterator iter (endpoints.begin ());
            iter != endpoints.end (); ++iter)
            result.emplace_back (iter->address());
        return result;
    }

    /** Returns a set of addresses for fixed peers we aren't connected to. */
    IPAddresses fetch_unconnected_fixedpeers (SharedState::Access& state)
    {
        IPAddresses addrs;

        addrs.reserve (m_fixedPeers.size());

        struct EqualWithoutPort
        {
            bool operator() (IPAddress const& lhs,
                IPAddress const& rhs) const
            {
                return lhs.at_port (0) == rhs.at_port (0);
            }
        };

        struct select_unconnected
        {
            SharedState::Access& state;
            IPAddresses& addresses;

            select_unconnected (SharedState::Access& state_, 
                IPAddresses& addresses_)
                    : state (state_)
                    , addresses (addresses_)
            { }

            void operator() (FixedPeers::value_type const& peer)
            {
                for(ConnectedAddresses::const_iterator iter = state->connected_addresses.cbegin();
                    iter != state->connected_addresses.cend(); ++iter)
                {
                    if (peer.second.hasAddress (*iter, EqualWithoutPort ()))
                        return;
                }

                addresses.push_back (peer.second.getAddress ());
            }
        };

        std::for_each (m_fixedPeers.begin(), m_fixedPeers.end(),
            select_unconnected (state, addrs));

        if (m_journal.debug)
        {
            m_journal.debug << "Unconnected peer list:";

            for(IPAddresses::const_iterator iter = addrs.cbegin();
                iter != addrs.cend(); ++iter)
                m_journal.debug << "[" << *iter << "]" << std::endl;
        }

        return addrs;
    }

    //--------------------------------------------------------------------------

    /** Create new outbound connection attempts as needed.
        This implements PeerFinder's "Outbound Connection Strategy"
    */
    void makeOutgoingConnections ()
    {
        SharedState::Access state (m_state);

        // Always make outgoing connections to all configured fixed peers
        // that are not currently connected.
        if (m_fixedPeers.size() != 0)
        {
            IPAddresses addrs (fetch_unconnected_fixedpeers (state));

            if (!addrs.empty())
                m_callback.connectPeers (addrs);
        }

        // Count how many more attempts we need
        if (! state->config.autoConnect)
            return;
        std::size_t const needed (
            state->slots.additionalAttemptsNeeded ());
        if (needed <= 0)
            return;

        if (m_journal.debug) m_journal.debug << leftw (18) <<
            "Logic need " << needed << " outbound attempts";

        /*  Stage #1 Livecache
            Stay in stage #1 until there are no entries left in the Livecache,
            and there are no more outbound connection attempts in progress.
            While in stage #1, all addresses for new connection attempts come
            from the Livecache.
        */
        if (state->slots.connectCount () > 0 || ! state->livecache.empty ())
        {
            IPAddresses const addrs (fetch_livecache (needed, state));
            if (! addrs.empty ())
            {
                if (m_journal.debug) m_journal.debug << leftw (18) <<
                    "Logic connect " << addrs.size () << " live " <<
                    ((addrs.size () > 1) ? "endpoints" : "endpoint");
                m_callback.connectPeers (addrs);
                return;
            }
        }

        /*  Stage #2 Bootcache Fetch
            If the Bootcache is empty, try to get addresses from the current
            set of Sources and add them into the Bootstrap cache.

            Pseudocode:
                If (    domainNames.count() > 0 AND (
                           unusedBootstrapIPs.count() == 0
                        OR activeNameResolutions.count() > 0) )
                    ForOneOrMore (DomainName that hasn't been resolved recently)
                        Contact DomainName and add entries to the unusedBootstrapIPs
                    return;
        */
        {
            // VFALCO TODO Stage #2
        }

        /*  Stage #3 Bootcache
            If the Bootcache contains entries that we haven't tried lately,
            then attempt them.
        */
        {
            IPAddresses const addrs (fetch_bootcache (needed, state));
            if (! addrs.empty ())
            {
                if (m_journal.debug) m_journal.debug << leftw (18) <<
                    "Logic connect " << addrs.size () << " boot " <<
                    ((addrs.size () > 1) ? "addresses" : "address");
                m_callback.connectPeers (addrs);
                return;
            }
        }

        // If we get here we are stuck
    }

    //--------------------------------------------------------------------------
    //
    // Logic
    //
    //--------------------------------------------------------------------------

    Slot::ptr new_inbound_slot (IP::Endpoint const& local_endpoint,
        IP::Endpoint const& remote_endpoint)
    {
        if (m_journal.debug) m_journal.debug << leftw (18) <<
            "Logic accept" << remote_endpoint <<
            " on local " << local_endpoint;

        SharedState::Access state (m_state);

        // Check for self-connect by address
        {
            auto const iter (state->slots_by_ip.find (local_endpoint));
            if (iter != state->slots_by_ip.end ())
            {
                Slot::ptr const& self (iter->second);
                assert (self->local_endpoint () == remote_endpoint);
                if (m_journal.warning) m_journal.warning << leftw (18) <<
                    "Logic dropping " << remote_endpoint <<
                    " as self connect";
                m_callback.disconnectPeer (remote_endpoint, false);
                return Slot::ptr ();
            }
        }

        // Create the slot
        Slot::ptr const slot (std::make_shared <SlotImp> (local_endpoint,
            remote_endpoint, fixed (remote_endpoint.address ())));
        // Add slot to table
        std::pair <SlotsByIP::iterator, bool> result (
            state->slots_by_ip.emplace (slot->remote_endpoint (),
                slot));
        // Remote address must not already exist
        assert (result.second);
        // Add to the connected address list
        state->connected_addresses.emplace (remote_endpoint.at_port (0));

        // Update slots
        state->slots.add (*slot);

        return result.first->second;
    }

    Slot::ptr new_outbound_slot (IP::Endpoint const& remote_endpoint)
    {
        if (m_journal.debug) m_journal.debug << leftw (18) <<
            "Logic connect " << remote_endpoint;

        // Create the slot
        Slot::ptr const slot (std::make_shared <SlotImp> (
            remote_endpoint, fixed (remote_endpoint.address ())));

        SharedState::Access state (m_state);

        // Add slot to table
        std::pair <SlotsByIP::iterator, bool> result (
            state->slots_by_ip.emplace (slot->remote_endpoint (),
                slot));
        // Remote address must not already exist
        assert (result.second);
        // Add to the connected address list
        state->connected_addresses.emplace (remote_endpoint.at_port (0));

        // Update slots
        state->slots.add (*slot);

        return result.first->second;
    }

    void on_connected (Slot::ptr const& slot,
        IP::Endpoint const& local_endpoint)
    {
        if (m_journal.trace) m_journal.trace << leftw (18) <<
            "Logic connected" << slot->remote_endpoint () <<
            " on local " << local_endpoint;

        SlotImp& impl (*std::dynamic_pointer_cast <SlotImp> (slot));
        SharedState::Access state (m_state);

        // The slot must exist in the table
        assert (state->slots_by_ip.find (slot->remote_endpoint ()) !=
            state->slots_by_ip.end ());
        // Assign the local endpoint now that it's known
        impl.local_endpoint (local_endpoint);

        // Check for self-connect by address
        {
            auto const iter (state->slots_by_ip.find (local_endpoint));
            if (iter != state->slots_by_ip.end ())
            {
                Slot::ptr const& self (iter->second);
                assert (self->local_endpoint () == slot->remote_endpoint ());
                if (m_journal.warning) m_journal.warning << leftw (18) <<
                    "Logic dropping " << slot->remote_endpoint () <<
                    " as self connect";
                m_callback.disconnectPeer (slot->remote_endpoint (), false);
                return;
            }
        }

        // Update slots
        state->slots.remove (*slot);
        impl.state (Slot::connected);
        state->slots.add (*slot);
    }

    void on_handshake (Slot::ptr const& slot,
        RipplePublicKey const& key, bool cluster)
    {
        if (m_journal.debug) m_journal.debug << leftw (18) <<
            "Logic handshake " << slot->remote_endpoint () <<
            " with " << (cluster ? "clustered " : "") << "key " << key;

        SlotImp& impl (*std::dynamic_pointer_cast <SlotImp> (slot));
        SharedState::Access state (m_state);

        // The slot must exist in the table
        assert (state->slots_by_ip.find (slot->remote_endpoint()) !=
            state->slots_by_ip.end ());
        // Must be accepted or connected
        assert (slot->state() == Slot::accept ||
            slot->state() == Slot::connected);

        // Set key and cluster
        state->slots.remove (*slot);
        impl.public_key (key);
        impl.cluster (cluster);
        state->slots.add (*slot);

        // Check for duplicate connection by key
        if (state->keys.find (key) != state->keys.end())
        {
            m_callback.disconnectPeer (slot->remote_endpoint (), true);
            return;
        }

        // See if we have an open space for this slot
        if (state->slots.available (*slot))
        {
            // Add the public key to the active set
            std::pair <Keys::iterator, bool> const result (
                state->keys.insert (key));
            // Public key must not already exist
            assert (result.second);

            // Change state and update slots
            state->slots.remove (*slot);
            impl.state (Slot::active);
            state->slots.add (*slot);

            // NIKB FIX peer is being replaced with slot
            //peer.activate (key, m_clock());
            // NIKB FIX callbacks should take the slot instead of address
            m_callback.activatePeer (slot->remote_endpoint());
        }
        else
        {
            if (slot->inbound ())
            {
                // We are full, so send the inbound connection some
                // new addresses to try then gracefully close them.
                Endpoints const endpoints (getSomeEndpoints ());
                if (! endpoints.empty ())
                {
                    if (m_journal.trace) m_journal.trace << leftw (18) <<
                        "Logic redirect " << slot->remote_endpoint() <<
                        " with " << endpoints.size() <<
                        ((endpoints.size() > 1) ? " addresses" : " address");
                    m_callback.sendEndpoints (
                        slot->remote_endpoint(), endpoints);
                }
                else
                {
                    if (m_journal.warning) m_journal.warning << leftw (18) <<
                        "Logic deferred " << slot->remote_endpoint();
                }
            }

            m_callback.disconnectPeer (slot->remote_endpoint (), true);
        }
    }

    void on_closed (Slot::ptr const& slot)
    {
        SharedState::Access state (m_state);
        SlotsByIP::iterator const iter (state->slots_by_ip.find (
            slot->remote_endpoint ()));
        // The slot must exist in the table
        assert (iter != state->slots_by_ip.end ());
        // Remove from slot by IP table
        state->slots_by_ip.erase (iter);
        // Remove the key if present
        if (slot->public_key () != boost::none)
        {
            Keys::iterator const iter (state->keys.find (*slot->public_key()));
            // Key must exist
            assert (iter != state->keys.end ());
            state->keys.erase (iter);
        }
        // Remove from connected address table
        {
            auto const iter (state->connected_addresses.find (
                slot->remote_endpoint().at_port (0)));
            // Address must exist
            assert (iter != state->connected_addresses.end ());
            state->connected_addresses.erase (iter);
        }

        // Update slots
        state->slots.remove (*slot);

        // Do state specific bookkeeping
        switch (slot->state())
        {
        case Slot::accept:
            if (m_journal.trace) m_journal.trace << leftw (18) <<
                "Logic accept " << slot->remote_endpoint () << " failed";
            break;

        case Slot::connect:
        case Slot::connected:
            state->bootcache.onConnectionFailure (slot->remote_endpoint ());
            // VFALCO TODO If the address exists in the ephemeral/live
            //             endpoint livecache then we should mark the failure
            // as if it didn't pass the listening test. We should also
            // avoid propagating the address.
            break;

        case Slot::active:
            if (! slot->inbound ())
                state->bootcache.onConnectionClosed (slot->remote_endpoint ());
            if (m_journal.trace) m_journal.trace << leftw (18) <<
                "Logic closed active " << slot->remote_endpoint();
            break;

        case Slot::closing:
            if (m_journal.trace) m_journal.trace << leftw (18) <<
                "Logic closed " << slot->remote_endpoint();
            break;

        default:
            assert (false);
            break;
        }
    }

    //--------------------------------------------------------------------------

    // Returns `true` if the address matches a fixed peer address
    // Note that this does not use the port information in the IP::Endpoint
    bool fixed (IP::Address const& address) const
    {
        // NIKB FIX Use std::find_if and a lambda. When resolving fixed peers
        //          pick only one endpoint and discard the rest. Store the
        //          entire IP::Endpoint in the fixed peer vector. But in
        //          this function only compare the address part.
        return false;
    }

    //--------------------------------------------------------------------------

    void setConfig (Config const& config)
    {
        SharedState::Access state (m_state);
        state->config = config;
        state->slots.onConfig (state->config);
    }

    void addFixedPeer (std::string const& name,
        std::vector <IPAddress> const& addresses)
    {
        if (addresses.empty ())
        {
            if (m_journal.info) m_journal.info <<
                "Could not resolve fixed peer '" << name << "'";
            return;
        }

        // NIKB TODO There is a problem with the following code: if you have
        //           two entries which resolve to the same IP address, you
        //           will end up with duplicate entries that resolve to the
        //           same IP. This will cause issues when you try to fetch
        //           a peer set because you will end up adding the same IP
        //           to the connection set twice.
        //
        //           The entries can be distinct (e.g. two hostnames that
        //           resolve to the same IP) or they can be the same entry
        //           but with whitespace/format changes.
        std::pair<FixedPeers::iterator, bool> result (
            m_fixedPeers.insert (std::make_pair (
                name, FixedPeer (name, addresses))));

        // If we have a duplicate, ignore it.
        if (!result.second)
        {
            if (m_journal.error) m_journal.error <<
                "'" << name << "' is already listed as fixed";
        }
        else
        {
            if (m_journal.debug) m_journal.debug <<
                "'" << name <<"' added as fixed";
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

    //--------------------------------------------------------------------------

    // Called periodically to sweep the livecache and remove aged out items.
    void sweepCache ()
    {
        SharedState::Access state (m_state);
        state->livecache.sweep ();
        for (Peers::iterator iter (state->peers.begin());
            iter != state->peers.end(); ++iter)
        {
            //Peer& peer (iter->second);
            //peer.received.cycle();
        }
    }

    // Called periodically to update uptime for connected outbound peers.
    void processUptime (SharedState::Access& state)
    {
        for (Peers::iterator iter (state->peers.begin());
            iter != state->peers.end(); ++iter)
        {
            Peer const& peer (iter->second);

            if (! peer.inbound() && peer.state() == Peer::stateActive)
                state->bootcache.onConnectionActive (
                    peer.remote_address());
        }
    }

    // Called every so often to perform periodic tasks.
    void periodicActivity ()
    {
        SharedState::Access state (m_state);
        processUptime (state);
        state->bootcache.periodicActivity ();
    }

    //--------------------------------------------------------------------------
    //
    // Bootcache livecache sources
    //
    //--------------------------------------------------------------------------

    // Add one address.
    // Returns `true` if the address is new.
    //
    bool addBootcacheAddress (IPAddress const& address,
        SharedState::Access& state)
    {
        return state->bootcache.insert (address);
    }

    // Add a set of addresses.
    // Returns the number of addresses added.
    //
    int addBootcacheAddresses (IPAddresses const& list)
    {
        int count (0);
        SharedState::Access state (m_state);
        for (IPAddresses::const_iterator iter (
            list.begin()); iter != list.end(); ++iter)
            if (addBootcacheAddress (*iter, state))
                ++count;
        return count;
    }

    // Fetch bootcache addresses from the specified source.
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

            // VFALCO NOTE The fetch is synchronous,
            //             not sure if that's a good thing.
            //
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
            int const count (addBootcacheAddresses (results.addresses));
            if (m_journal.info) m_journal.info << leftw (18) <<
                "Logic added " << count <<
                " new " << ((count == 1) ? "address" : "addresses") <<
                " from " << source->name();
        }
        else
        {
            if (m_journal.error) m_journal.error << leftw (18) <<
                "Logic failed " << "'" << source->name() << "' fetch, " <<
                results.error.message();
        }

    }

    //--------------------------------------------------------------------------
    //
    // Endpoint message handling
    //
    //--------------------------------------------------------------------------

    // Returns a suitable Endpoint representing us.
    Endpoint thisEndpoint (SharedState::Access& state)
    {
        // Why would someone call this if we don't want incoming?
        consistency_check (state->config.wantIncoming);
        Endpoint ep;
        ep.hops = 0;
        ep.address = IPAddress (
            IP::AddressV4 ()).at_port (state->config.listeningPort);
        ep.features = state->config.features;
        return ep;
    }

    // Returns true if the IPAddress contains no invalid data.
    bool is_valid_address (IPAddress const& address)
    {
        if (is_unspecified (address))
            return false;
        if (! is_public (address))
            return false;
        if (address.port() == 0)
            return false;
        return true;
    }

    // Creates a set of endpoints suitable for a temporary peer.
    // Sent to a peer when we are full, before disconnecting them.
    //
    Endpoints getSomeEndpoints ()
    {
        SharedState::Access state (m_state);
        Endpoints result (state->livecache.fetch_unique ());
        std::random_shuffle (result.begin(), result.end());
        if (result.size () > Tuning::redirectEndpointCount)
            result.resize (Tuning::redirectEndpointCount);       
        return result;
    }

    // Send mtENDPOINTS for the specified peer
    void sendEndpointsTo (Peer const& peer, Giveaways& g)
    {
        Endpoints endpoints;

        if (endpoints.size() < Tuning::numberOfEndpoints)
        {
            SharedState::Access state (m_state);

            // Add an entry for ourselves if:
            //  1. We want incoming
            //  2. We have slots
            //  3. We haven't failed the firewalled test
            //
            if (state->config.wantIncoming && state->slots.inboundSlots() > 0)
                endpoints.push_back (thisEndpoint (state));
        }

        if (endpoints.size() < Tuning::numberOfEndpoints)
        {
            g.append (Tuning::numberOfEndpoints - endpoints.size(), endpoints);
        }

        if (! endpoints.empty())
        {
            if (m_journal.trace) m_journal.trace << leftw (18) <<
                "Logic sending " << peer.remote_address() << 
                " with " << endpoints.size() <<
                ((endpoints.size() > 1) ? " endpoints" : " endpoint");
            m_callback.sendEndpoints (peer.remote_address(), endpoints);
        }
    }

    // Send mtENDPOINTS for each peer as needed
    void sendEndpoints ()
    {
        SharedState::Access state (m_state);
        if (! state->peers.empty())
        {
            DiscreteTime const now (m_clock());
            DiscreteTime const whenSendEndpoints (
                now + Tuning::secondsPerMessage);
            Giveaways g (state->livecache.giveaways ());
            for (Peers::iterator iter (state->peers.begin());
                iter != state->peers.end(); ++iter)
            {
                Peer& peer (iter->second);
                if (peer.state() == Peer::stateActive)
                {
                    if (peer.whenSendEndpoints <= now)
                    {
                        sendEndpointsTo (peer, g);
                        peer.whenSendEndpoints = whenSendEndpoints;
                    }
                }
            }
        }
    }

    // Called when the Checker completes a connectivity test
    void checkComplete (IPAddress const& address,
        IPAddress const & checkedAddress, Checker::Result const& result)
    {
        if (result.error == boost::asio::error::operation_aborted)
            return;

        SharedState::Access state (m_state);
        Peers::iterator const iter (state->peers.find (address));
        Peer& peer (iter->second);

        if (iter == state->peers.end())
        {
            // The peer disconnected before we finished the check
            if (m_journal.debug) m_journal.debug << leftw (18) <<
                "Logic tested " << address <<
                " but the connection was closed";
            return;
        }

        // Mark that a check for this peer is finished.
        peer.connectivityCheckInProgress = false;

        if (! result.error)
        {
            peer.checked = true;
            peer.canAccept = result.canAccept;

            if (peer.canAccept)
            {
                if (m_journal.debug) m_journal.debug << leftw (18) <<
                    "Logic testing " << address << " succeeded";
            }
            else
            {
                if (m_journal.info) m_journal.info << leftw (18) <<
                    "Logic testing " << address << " failed";
            }
        }
        else
        {
            // VFALCO TODO Should we retry depending on the error?
            peer.checked = true;
            peer.canAccept = false;

            if (m_journal.error) m_journal.error << leftw (18) <<
                "Logic testing " << iter->first << " with error, " <<
                result.error.message();
        }

        if (peer.canAccept)
        {
            // VFALCO TODO Why did I think this line was needed?
            //state->bootcache.onConnectionHandshake (address);
        }
        else
        {
            state->bootcache.onConnectionFailure (address);
        }
    }

    //--------------------------------------------------------------------------
    //
    // Socket Hooks
    //
    //--------------------------------------------------------------------------

    // Returns `true` if the address matches the remote address of one
    // of our outbound sockets.
    //
    // VFALCO TODO Do the lookup using an additional index by local address
    bool haveLocalOutboundAddress (IPAddress const& local_address,
        SharedState::Access& state)
    {
        for (Peers::const_iterator iter (state->peers.begin());
            iter != state->peers.end(); ++iter)
        {
            Peer const& peer (iter->second);
            if (peer.outbound () &&
                peer.local_address() == local_address)
                return true;
        }
        return false;
    }

    //--------------------------------------------------------------------------
    bool isFixed (IPAddress const& address) const
    {
        struct EqualWithoutPort
        {
            bool operator() (IPAddress const& lhs,
                IPAddress const& rhs) const
            {
                return lhs.at_port (0) == rhs.at_port (0);
            }
        };

        for (FixedPeers::const_iterator iter = m_fixedPeers.cbegin();
            iter != m_fixedPeers.cend(); ++iter)
        {
            if (iter->second.hasAddress (address, EqualWithoutPort ()))
                return true;
        }

        return false;
    }

    //--------------------------------------------------------------------------

    void onPeerAccept (IPAddress const& local_address,
        IPAddress const& remote_endpoint)
    {
return;
        if (m_journal.debug) m_journal.debug << leftw (18) <<
            "Logic accept" << remote_endpoint <<
            " on local " << local_address;
        SharedState::Access state (m_state);
        state->slots.onPeerAccept ();
        state->connected_addresses.insert (remote_endpoint.at_port (0));

        // FIXME m_fixedPeers contains both an IP and a port and incoming peers
        // wouldn't match the port, so wouldn't get identified as fixed. One
        // solution is to have fixed peers tracked by IP and not port. The
        // port will be used only for outbound connections. Another option is
        // to always consider incoming connections as non-fixed, then after
        // an outbound connection to the peer is established and we realize
        // we have a duplicate connection to a fixed peer, to find that peer
        // and mark it as fixed.
        //
        //bool fixed (m_fixedPeers.count (
        //    remote_endpoint.address ()) != 0);

        bool fixed (false);

        std::pair <Peers::iterator, bool> result (
            state->peers.emplace (boost::unordered::piecewise_construct,
                boost::make_tuple (remote_endpoint),
                    boost::make_tuple (remote_endpoint, true, fixed)));
        // Address must not already exist!
        consistency_check (result.second);
        // Prevent self connect
        if (haveLocalOutboundAddress (remote_endpoint, state))
        {
            if (m_journal.warning) m_journal.warning << leftw (18) <<
                "Logic dropping " << remote_endpoint << " as self connect";
            m_callback.disconnectPeer (remote_endpoint, false);
            return;
        }
    }

    void onPeerConnect (IPAddress const& remote_endpoint)
    {
return;
        if (m_journal.debug) m_journal.debug << leftw (18) <<
            "Logic connect " << remote_endpoint;
        SharedState::Access state (m_state);
        state->slots.onPeerConnect ();
        state->connected_addresses.insert (remote_endpoint.at_port (0));

        bool fixed (isFixed (remote_endpoint));

        // VFALCO TODO Change to use forward_as_tuple
        std::pair <Peers::iterator, bool> result (
            state->peers.emplace (boost::unordered::piecewise_construct,
                boost::make_tuple (remote_endpoint),
                    boost::make_tuple (remote_endpoint, false, fixed)));
        // Address must not already exist!
        consistency_check (result.second);
    }

    void onPeerConnected (IPAddress const& local_address,
        IPAddress const& remote_endpoint)
    {
return;
        if (m_journal.trace) m_journal.trace << leftw (18) <<
            "Logic connected" << remote_endpoint <<
            " on local " << local_address;
        SharedState::Access state (m_state);
        Peers::iterator const iter (state->peers.find (remote_endpoint));
        // Address must exist!
        consistency_check (iter != state->peers.end());
        Peer& peer (iter->second);
        peer.local_address (local_address);
        peer.state (Peer::stateConnected);
    }

    void onPeerHandshake (IPAddress const& remote_endpoint, 
        PeerID const& key, bool inCluster)
    {
return;
        if (m_journal.debug) m_journal.debug << leftw (18) <<
            "Logic handshake " << remote_endpoint <<
            " with key " << key;
        SharedState::Access state (m_state);
        Peers::iterator const iter (state->peers.find (remote_endpoint));
        // Address must exist!
        consistency_check (iter != state->peers.end());
        Peer& peer (iter->second);
        // Must be accepted or connected
        consistency_check (
            peer.state() == Peer::stateAccept ||
            peer.state() == Peer::stateConnected);
        // Mark cluster peers as necessary
        peer.cluster (inCluster);        

        // We track connections by their public key to allow us to reliably
        // detect duplicates even for multi-homed servers.

        // NIKB FIXME Doing this will cause the Slots count to miscount the
        //            closing count, as the code below puts the server in
        //            Peer::stateClosing which attempts to decrement the slot
        //            closing code which we have not incremented.
        HandshakeAction action (doClose);

        // If the public key of the server is on our list, it means that we already
        // have a connection to that server, so we don't even need to bother with
        // attempting to assign a slot.
        if (state->keys.find (key) == state->keys.end())
            action = state->slots.onPeerHandshake (
                peer.inbound(), peer.fixed(), peer.cluster());

        // Pass address metadata to bootcache
        if (peer.outbound())
            state->bootcache.onConnectionHandshake (remote_endpoint, action);
        if (action == doActivate)
        {
            // Track the public key
            std::pair <Keys::iterator, bool> const result (
                state->keys.insert (key));
            // Must not already exist!
            consistency_check (result.second);
            peer.activate (key, m_clock());
            m_callback.activatePeer (remote_endpoint);
        }
        else
        {
            peer.state (Peer::stateClosing);
            if (action == doRedirect)
            {
                // Must be inbound!
                consistency_check (peer.inbound());
                Endpoints const endpoints (getSomeEndpoints ());
                if (! endpoints.empty ())
                {
                    if (m_journal.trace) m_journal.trace << leftw (18) <<
                        "Logic redirect " << remote_endpoint <<
                        " with " << endpoints.size() <<
                        ((endpoints.size() > 1) ? " addresses" : " address");
                    m_callback.sendEndpoints (peer.remote_address(), endpoints);
                }
                else
                {
                    if (m_journal.warning) m_journal.warning << leftw (18) <<
                        "Logic deferred " << remote_endpoint;
                }
            }
            m_callback.disconnectPeer (remote_endpoint, true);
        }
    }

    void onPeerClosed (IPAddress const& remote_endpoint)
    {
return;
        SharedState::Access state (m_state);
        {
            ConnectedAddresses::iterator iter (state->connected_addresses.find (
                remote_endpoint.at_port (0)));
            // Address must exist!
            consistency_check (iter != state->connected_addresses.end());
            state->connected_addresses.erase (iter);
        }
        Peers::iterator const iter (state->peers.find (remote_endpoint));
        // Address must exist!
        consistency_check (iter != state->peers.end());
        Peer& peer (iter->second);
        switch (peer.state())
        {
        // Accepted but no handshake
        case Peer::stateAccept:
        // Connection attempt failed
        case Peer::stateConnect:
        // Connected but no handshake
        case Peer::stateConnected:
            {
                // Update slots
                state->slots.onPeerClosed (peer.inbound (), false, 
                    peer.fixed (), peer.cluster ());
                if (peer.outbound())
                {
                    state->bootcache.onConnectionFailure (remote_endpoint);
                }
                else
                {
                    if (m_journal.trace) m_journal.trace << leftw (18) <<
                        "Logic accept " << remote_endpoint << " failed";
                }

                // VFALCO TODO If the address exists in the ephemeral/live
                //             endpoint livecache then we should mark the failure
                // as if it didn't pass the listening test. We should also
                // avoid propagating the address.
            }
            break;

        // The peer was assigned an open slot.
        case Peer::stateActive:
            {
                // Remove the key
                Keys::iterator const iter (state->keys.find (peer.id()));
                // Key must exist!
                consistency_check (iter != state->keys.end());
                state->keys.erase (iter);
                if (peer.outbound())
                    state->bootcache.onConnectionClosed (remote_endpoint);
                state->slots.onPeerClosed (peer.inbound (), true, 
                    peer.fixed (), peer.cluster ());
                if (m_journal.trace) m_journal.trace << leftw (18) <<
                    "Logic closed active " << peer.remote_address();
            }
            break;

        // The peer handshaked but we were full on slots
        // or it was a self connection.
        case Peer::stateClosing:
            {
                if (m_journal.trace) m_journal.trace << leftw (18) <<
                    "Logic closed " << remote_endpoint;
                state->slots.onPeerGracefulClose ();
            }
            break;

        default:
            consistency_check (false);
            break;
        };

        state->peers.erase (iter);
    }

    void onPeerAddressChanged (
        IPAddress const& currentAddress, IPAddress const& newAddress)
    {
#if FIX_ME
        // VFALCO TODO Demote this to trace after PROXY is tested.
        m_journal.debug <<
            "onPeerAddressChanged (" << currentAddress <<
            ", " << newAddress << ")";

        SharedState::Access state (m_state);

        Connections::iterator iter (
            state->connections.find (currentAddress));

        // Current address must exist!
        consistency_check (iter != state->connections.end());

        Connection& connection (iter->second);

        // Connection must be inbound!
        consistency_check (connection.inbound());

        // Connection must be connected!
        consistency_check (connection.state() == Connection::stateConnected);

        // Create a new Connection entry for the new address
        std::pair <Connections::iterator, bool> result (
            state->connections.emplace (newAddress,
                Connection (iter->second)));

        // New address must not already exist!
        consistency_check (result.second);

        // Remove old Connection entry
        state->connections.erase (iter);

        // Update the address on the peer
        Peer& peer (result.first->second.peersIterator()->second);
        peer.address = newAddress;
#endif
    }

    void onPeerEndpoints (IPAddress const& address, Endpoints list)
    {
        if (m_journal.trace) m_journal.trace << leftw (18) <<
            "Endpoints from " << address <<
            " contained " << list.size () <<
            ((list.size() > 1) ? " entries" : " entry");
        SharedState::Access state (m_state);
        Peers::iterator const iter (state->peers.find (address));
        // Address must exist!
        consistency_check (iter != state->peers.end());
        Peer& peer (iter->second);
        // Must be handshaked!
        consistency_check (peer.state() == Peer::stateActive);
        // Preprocess the endpoints
        {
            bool neighbor (false);
            for (Endpoints::iterator iter (list.begin());
                iter != list.end();)
            {
                Endpoint& ep (*iter);
                if (ep.hops > Tuning::maxHops)
                {
                    if (m_journal.warning) m_journal.warning << leftw (18) <<
                        "Endpoints drop " << ep.address <<
                        " for excess hops " << ep.hops;
                    iter = list.erase (iter);
                    continue;
                }
                if (ep.hops == 0)
                {
                    if (! neighbor)
                    {
                        // Fill in our neighbors remote address
                        neighbor = true;
                        ep.address = peer.remote_address().at_port (
                            ep.address.port ());
                    }
                    else
                    {
                        if (m_journal.warning) m_journal.warning << leftw (18) <<
                            "Endpoints drop " << ep.address <<
                            " for extra self";
                        iter = list.erase (iter);
                        continue;
                    }
                }
                if (! is_valid_address (ep.address))
                {
                    if (m_journal.warning) m_journal.warning << leftw (18) <<
                        "Endpoints drop " << ep.address <<
                        " as invalid";
                    iter = list.erase (iter);
                    continue;
                }
                ++iter;
            }
        }

        DiscreteTime const now (m_clock());

        for (Endpoints::const_iterator iter (list.begin());
            iter != list.end(); ++iter)
        {
            Endpoint const& ep (*iter);

            //peer.received.insert (ep.address);

            if (ep.hops == 0)
            {
                if (peer.connectivityCheckInProgress)
                {
                    if (m_journal.warning) m_journal.warning << leftw (18) <<
                        "Logic testing " << ep.address << " already in progress";
                }
                else if (! peer.checked)
                {
                    // Mark that a check for this peer is now in progress.
                    peer.connectivityCheckInProgress = true;

                    // Test the peer's listening port before
                    // adding it to the livecache for the first time.
                    //                     
                    m_checker.async_test (ep.address, bind (
                        &Logic::checkComplete, this, address,
                            ep.address, _1));

                    // Note that we simply discard the first Endpoint
                    // that the neighbor sends when we perform the
                    // listening test. They will just send us another
                    // one in a few seconds.
                }
                else if (peer.canAccept)
                {
                    // We only add to the livecache if the neighbor passed the
                    // listening test, else we silently drop their messsage
                    // since their listening port is misconfigured.
                    //
                    state->livecache.insert (ep);
                    state->bootcache.insert (ep.address);
                }
            }
            else
            {
                state->livecache.insert (ep);
                state->bootcache.insert (ep.address);
            }
        }

        peer.whenAcceptEndpoints = now + Tuning::secondsPerMessage;
    }

    void onLegacyEndpoints (
        IPAddresses const& list)
    {
        // Ignoring them also seems a valid choice.
        SharedState::Access state (m_state);
        for (IPAddresses::const_iterator iter (list.begin());
            iter != list.end(); ++iter)
            state->bootcache.insert (*iter);
    }

    //--------------------------------------------------------------------------
    //
    // PropertyStream
    //
    //--------------------------------------------------------------------------

    void writePeers (PropertyStream::Set& set, Peers const& peers)
    {
        for (Peers::const_iterator iter (peers.begin());
            iter != peers.end(); ++iter)
        {
            PropertyStream::Map item (set);
            Peer const& peer (iter->second);
            item ["local_address"]   = to_string (peer.local_address ());
            item ["remote_address"]   = to_string (peer.remote_address ());
            if (peer.inbound())
                item ["inbound"]    = "yes";
            if (peer.fixed())
                item ["fixed"]      = "yes";
            if (peer.cluster())
                item ["cluster"]    = "yes";
            
            item ["state"] = stateString (peer.state());
        }
    }

    void onWrite (PropertyStream::Map& map)
    {
        SharedState::Access state (m_state);

        // VFALCO NOTE These ugly casts are needed because
        //             of how std::size_t is declared on some linuxes
        //
        map ["livecache"]   = uint32 (state->livecache.size());
        map ["bootcache"]   = uint32 (state->bootcache.size());
        map ["fixed"]       = uint32 (m_fixedPeers.size());

        {
            PropertyStream::Set child ("peers", map);
            writePeers (child, state->peers);
        }

        {
            PropertyStream::Map child ("slots", map);
            state->slots.onWrite (child);
        }

        {
            PropertyStream::Map child ("config", map);
            state->config.onWrite (child);
        }

        {
            PropertyStream::Map child ("bootcache", map);
            state->bootcache.onWrite (child);
        }
    }

    //--------------------------------------------------------------------------
    //
    // Diagnostics
    //
    //--------------------------------------------------------------------------

    State const& state () const
    {
        return *SharedState::ConstAccess (m_state);
    }

    Slots const& slots () const
    {
        return SharedState::ConstAccess (m_state)->slots;
    }

    static std::string stateString (Peer::State state)
    {
        switch (state)
        {
        case Peer::stateAccept:     return "accept";
        case Peer::stateConnect:    return "connect";
        case Peer::stateConnected:  return "connected";
        case Peer::stateActive:     return "active";
        case Peer::stateClosing:    return "closing";
        default:
            break;
        };
        return "?";
    }

    void dump_peers (Journal::ScopedStream& ss,
        SharedState::ConstAccess const& state) const
    {
        ss << std::endl << std::endl <<
            "Peers";
        for (Peers::const_iterator iter (state->peers.begin());
            iter != state->peers.end(); ++iter)
        {
            Peer const& peer (iter->second);
            ss << std::endl <<
                peer.remote_address () <<
                (peer.inbound () ? " (in) " : " ") <<
                stateString (peer.state ()) << " " <<
                peer.id();
        }
    }

    void dump (Journal::ScopedStream& ss) const
    {
        SharedState::ConstAccess state (m_state);

        state->bootcache.dump (ss);
        state->livecache.dump (ss);
        dump_peers (ss, state);
        ss << std::endl <<
            state->slots.state_string ();
        ss << std::endl;
    }

};

}
}

#endif

/*

Terms

'Book' an order book
'Offer' an entry in a book
'Inverse Book' the book for the opposite direction

'Directory' Holds offers with the same quality level

An order book is a list of offers. The book has the following
canonical order. The primary key is the quality (ratio of input to
output). The secondary key is an ordinal to break ties for two offers
with the same quality (first come first serve).

Three places where books are iterated in canonical order:

1. When responding to a client request for a book

2. When placing an offer in the inverse book

3. When processing a payment that goes through the book

A directory is a type of structure in the ledger



Invariants:

- All that is needed to process a transaction is the current Ledger object.

*/
