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

#include "Fixed.h"
#include "SlotImp.h"

#include <unordered_map>

namespace ripple {
namespace PeerFinder {

/** The Logic for maintaining the list of Slot addresses.
    We keep this in a separate class so it can be instantiated
    for unit tests.
*/
class Logic
{
public:
    // Maps remote endpoints to slots. Since a slot has a
    // remote endpoint upon construction, this holds all counts.
    // 
    typedef std::map <IP::Endpoint,
        std::shared_ptr <SlotImp>> Slots;

    typedef std::map <IP::Endpoint, Fixed> FixedSlots;

    // A set of unique Ripple public keys
    typedef std::set <RipplePublicKey> Keys;

    // A set of non-unique IPAddresses without ports, used
    // to filter duplicates when making outgoing connections.
    typedef std::multiset <IP::Endpoint> ConnectedAddresses;

    struct State
    {
        State (
            Store* store,
            clock_type& clock,
            Journal journal)
            : stopping (false)
            , counts (clock)
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
        Counts counts;

        // A list of slots that should always be connected
        FixedSlots fixed;

        // Live livecache from mtENDPOINTS messages
        Livecache livecache;

        // LiveCache of addresses suitable for gaining initial connections
        Bootcache bootcache;

        // Holds all counts
        Slots slots;

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
    clock_type& m_clock;
    Callback& m_callback;
    Store& m_store;
    Checker& m_checker;

    // A list of dynamic sources to consult as a fallback
    std::vector <SharedPtr <Source>> m_sources;

    //--------------------------------------------------------------------------

    Logic (
        clock_type& clock,
        Callback& callback,
        Store& store,
        Checker& checker,
        Journal journal)
        : m_journal (journal, Reporting::logic)
        , m_state (&store, std::ref (clock), journal)
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
    //
    // Manager
    //
    //--------------------------------------------------------------------------

    void setConfig (Config const& config)
    {
        SharedState::Access state (m_state);
        state->config = config;
        state->counts.onConfig (state->config);
    }

    void addFixedPeer (std::string const& name,
        std::vector <IP::Endpoint> const& addresses)
    {
        SharedState::Access state (m_state);

        if (addresses.empty ())
        {
            if (m_journal.info) m_journal.info <<
                "Could not resolve fixed slot '" << name << "'";
            return;
        }

        for (auto remote_address : addresses)
        {
            auto result (state->fixed.emplace (std::piecewise_construct,
                std::forward_as_tuple (remote_address),
                    std::make_tuple (std::ref (m_clock))));

            if (result.second)
            {
                if (m_journal.debug) m_journal.debug << leftw (18) <<
                    "Logic add fixed" << "'" << name <<
                    "' at " << remote_address;
                return;
            }
        }
    }

    //--------------------------------------------------------------------------

    SlotImp::ptr new_inbound_slot (IP::Endpoint const& local_endpoint,
        IP::Endpoint const& remote_endpoint)
    {
        if (m_journal.debug) m_journal.debug << leftw (18) <<
            "Logic accept" << remote_endpoint <<
            " on local " << local_endpoint;

        SharedState::Access state (m_state);

        // Check for self-connect by address
        {
            auto const iter (state->slots.find (local_endpoint));
            if (iter != state->slots.end ())
            {
                Slot::ptr const& self (iter->second);
                assert (self->local_endpoint () == remote_endpoint);
                if (m_journal.warning) m_journal.warning << leftw (18) <<
                    "Logic dropping " << remote_endpoint <<
                    " as self connect";
                return SlotImp::ptr ();
            }
        }

        // Create the slot
        SlotImp::ptr const slot (std::make_shared <SlotImp> (local_endpoint,
            remote_endpoint, fixed (remote_endpoint.address (), state)));
        // Add slot to table
        auto const result (state->slots.emplace (
            slot->remote_endpoint (), slot));
        // Remote address must not already exist
        assert (result.second);
        // Add to the connected address list
        state->connected_addresses.emplace (remote_endpoint.at_port (0));

        // Update counts
        state->counts.add (*slot);

        return result.first->second;
    }

    SlotImp::ptr new_outbound_slot (IP::Endpoint const& remote_endpoint)
    {
        if (m_journal.debug) m_journal.debug << leftw (18) <<
            "Logic connect " << remote_endpoint;

        SharedState::Access state (m_state);

        // Check for duplicate connection
        if (state->slots.find (remote_endpoint) !=
            state->slots.end ())
        {
            if (m_journal.warning) m_journal.warning << leftw (18) <<
                "Logic dropping " << remote_endpoint <<
                " as duplicate connect";
            return SlotImp::ptr ();
        }

        // Create the slot
        SlotImp::ptr const slot (std::make_shared <SlotImp> (
            remote_endpoint, fixed (remote_endpoint, state)));

        // Add slot to table
        std::pair <Slots::iterator, bool> result (
            state->slots.emplace (slot->remote_endpoint (),
                slot));
        // Remote address must not already exist
        assert (result.second);

        // Add to the connected address list
        state->connected_addresses.emplace (remote_endpoint.at_port (0));

        // Update counts
        state->counts.add (*slot);

        return result.first->second;
    }

    void on_connected (SlotImp::ptr const& slot,
        IP::Endpoint const& local_endpoint)
    {
        if (m_journal.trace) m_journal.trace << leftw (18) <<
            "Logic connected" << slot->remote_endpoint () <<
            " on local " << local_endpoint;

        SharedState::Access state (m_state);

        // The object must exist in our table
        assert (state->slots.find (slot->remote_endpoint ()) !=
            state->slots.end ());
        // Assign the local endpoint now that it's known
        slot->local_endpoint (local_endpoint);

        // Check for self-connect by address
        {
            auto const iter (state->slots.find (local_endpoint));
            if (iter != state->slots.end ())
            {
                Slot::ptr const& self (iter->second);
                assert (self->local_endpoint () == slot->remote_endpoint ());
                if (m_journal.warning) m_journal.warning << leftw (18) <<
                    "Logic dropping " << slot->remote_endpoint () <<
                    " as self connect";
                m_callback.disconnect (slot, false);
                return;
            }
        }

        // Update counts
        state->counts.remove (*slot);
        slot->state (Slot::connected);
        state->counts.add (*slot);
    }

    void on_handshake (SlotImp::ptr const& slot,
        RipplePublicKey const& key, bool cluster)
    {
        if (m_journal.debug) m_journal.debug << leftw (18) <<
            "Logic handshake " << slot->remote_endpoint () <<
            " with " << (cluster ? "clustered " : "") << "key " << key;

        SharedState::Access state (m_state);

        // The object must exist in our table
        assert (state->slots.find (slot->remote_endpoint ()) !=
            state->slots.end ());
        // Must be accepted or connected
        assert (slot->state() == Slot::accept ||
            slot->state() == Slot::connected);

        // Check for duplicate connection by key
        if (state->keys.find (key) != state->keys.end())
        {
            m_callback.disconnect (slot, true);
            return;
        }

        // See if we have an open space for this slot
        if (state->counts.can_activate (*slot))
        {
            // Set key and cluster right before adding to the map
            // otherwise we could assert later when erasing the key.
            state->counts.remove (*slot);
            slot->public_key (key);
            slot->cluster (cluster);
            state->counts.add (*slot);

            // Add the public key to the active set
            std::pair <Keys::iterator, bool> const result (
                state->keys.insert (key));
            // Public key must not already exist
            assert (result.second);

            // Change state and update counts
            state->counts.remove (*slot);
            slot->activate (m_clock.now ());
            state->counts.add (*slot);

            if (! slot->inbound())
                state->bootcache.onConnectionHandshake (
                    slot->remote_endpoint(), doActivate);

            // Mark fixed slot success
            if (slot->fixed() && ! slot->inbound())
            {
                auto iter (state->fixed.find (slot->remote_endpoint()));
                assert (iter != state->fixed.end ());
                iter->second.success (m_clock.now ());
                if (m_journal.trace) m_journal.trace << leftw (18) <<
                    "Logic fixed " << slot->remote_endpoint () << " success";
            }

            m_callback.activate (slot);
        }
        else
        {
            if (! slot->inbound())
                state->bootcache.onConnectionHandshake (
                    slot->remote_endpoint(), doClose);

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
                    m_callback.send (slot, endpoints);
                }
                else
                {
                    if (m_journal.warning) m_journal.warning << leftw (18) <<
                        "Logic deferred " << slot->remote_endpoint();
                }
            }

            m_callback.disconnect (slot, true);
        }
    }

    void on_endpoints (SlotImp::ptr const& slot, Endpoints list)
    {
        if (m_journal.trace) m_journal.trace << leftw (18) <<
            "Endpoints from " << slot->remote_endpoint () <<
            " contained " << list.size () <<
            ((list.size() > 1) ? " entries" : " entry");
        SharedState::Access state (m_state);
        // The object must exist in our table
        assert (state->slots.find (slot->remote_endpoint ()) !=
            state->slots.end ());
        // Must be handshaked!
        assert (slot->state() == Slot::active);
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
                        ep.address = slot->remote_endpoint().at_port (
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

        clock_type::time_point const now (m_clock.now());

        for (Endpoints::const_iterator iter (list.begin());
            iter != list.end(); ++iter)
        {
            Endpoint const& ep (*iter);

            //slot->received.insert (ep.address);

            if (ep.hops == 0)
            {
                if (slot->connectivityCheckInProgress)
                {
                    if (m_journal.warning) m_journal.warning << leftw (18) <<
                        "Logic testing " << ep.address << " already in progress";
                }
                else if (! slot->checked)
                {
                    // Mark that a check for this slot is now in progress.
                    slot->connectivityCheckInProgress = true;

                    // Test the slot's listening port before
                    // adding it to the livecache for the first time.
                    //                     
                    m_checker.async_test (ep.address, bind (
                        &Logic::checkComplete, this, slot->remote_endpoint (),
                            ep.address, _1));

                    // Note that we simply discard the first Endpoint
                    // that the neighbor sends when we perform the
                    // listening test. They will just send us another
                    // one in a few seconds.
                }
                else if (slot->canAccept)
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

        slot->whenAcceptEndpoints = now + Tuning::secondsPerMessage;
    }

    void on_legacy_endpoints (IPAddresses const& list)
    {
        // Ignoring them also seems a valid choice.
        SharedState::Access state (m_state);
        for (IPAddresses::const_iterator iter (list.begin());
            iter != list.end(); ++iter)
            state->bootcache.insert (*iter);
    }

    void on_closed (SlotImp::ptr const& slot)
    {
        SharedState::Access state (m_state);
        Slots::iterator const iter (state->slots.find (
            slot->remote_endpoint ()));
        // The slot must exist in the table
        assert (iter != state->slots.end ());
        // Remove from slot by IP table
        state->slots.erase (iter);
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

        // Update counts
        state->counts.remove (*slot);

        // Mark fixed slot failure
        if (slot->fixed() && ! slot->inbound() && slot->state() != Slot::active)
        {
            auto iter (state->fixed.find (slot->remote_endpoint()));
            assert (iter != state->fixed.end ());
            iter->second.failure (m_clock.now ());
            if (m_journal.debug) m_journal.debug << leftw (18) <<
                "Logic fixed " << slot->remote_endpoint () << " failed";
        }

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

    // Returns `true` if the address matches a fixed slot address
    bool fixed (IP::Endpoint const& endpoint, SharedState::Access& state) const
    {
        for (auto entry : state->fixed)
            if (entry.first == endpoint)
                return true;
        return false;
    }

    // Returns `true` if the address matches a fixed slot address
    // Note that this does not use the port information in the IP::Endpoint
    bool fixed (IP::Address const& address, SharedState::Access& state) const
    {
        for (auto entry : state->fixed)
            if (entry.first.address () == address)
                return true;
        return false;
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
                    Endpoint const& lhs, IP::Endpoint const& rhs) const
                {
                    return lhs.address.address()  < rhs.address();
                }
                bool operator() (
                    Endpoint const& lhs, Endpoint const& rhs) const
                {
                    return lhs.address.address() < rhs.address.address();
                }
                bool operator() (
                    IP::Endpoint const& lhs, Endpoint const& rhs) const
                {
                    return lhs.address() < rhs.address.address();
                }
                bool operator() (
                    IP::Endpoint const& lhs, IP::Endpoint const& rhs) const
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
    //
    // Connection Strategy
    //
    //--------------------------------------------------------------------------

    /** Adds eligible Fixed addresses for outbound attempts. */
    template <class Container>
    void get_fixed (std::size_t needed, Container& c,
        SharedState::Access& state)
    {
        auto const now (m_clock.now());
        for (auto iter = state->fixed.begin ();
            needed && iter != state->fixed.end (); ++iter)
        {
            auto const& address (iter->first.address());
            if (iter->second.when() <= now && std::none_of (
                state->slots.cbegin(), state->slots.cend(),
                    [address](Slots::value_type const& v)
                    {
                        return address == v.first.address();
                    }))
            {
                c.push_back (iter->first);
                --needed;
            }
        }
    }

    /** Adds eligible bootcache addresses for outbound attempts. */
    template <class Container>
    void get_bootcache (std::size_t needed, Container& c, SharedState::Access& state)
    {
        // Get everything
        auto endpoints (state->bootcache.fetch ());

        struct LessRank
        {
            bool operator() (Bootcache::Endpoint const& lhs,
                Bootcache::Endpoint const& rhs) const
            {
                if (lhs.uptime() > rhs.uptime())
                    return true;
                else if (lhs.uptime() <= rhs.uptime() && rhs.uptime().count() != 0)
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
                    IP::Endpoint const& rhs) const
                {
                    return lhs.address().at_port (0) < rhs.at_port (0);
                }
                bool operator() (Bootcache::Endpoint const& lhs,
                    Bootcache::Endpoint const& rhs) const
                {
                    return lhs.address().at_port (0) <
                        rhs.address().at_port (0);
                }
                bool operator() (IP::Endpoint const& lhs,
                    Bootcache::Endpoint const& rhs) const
                {
                    return lhs.at_port (0) < rhs.address().at_port (0);
                }
                bool operator() (IP::Endpoint const& lhs,
                    IP::Endpoint const& rhs) const
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

        c.reserve (endpoints.size ());
        for (Bootcache::Endpoints::const_iterator iter (endpoints.begin ());
            iter != endpoints.end (); ++iter)
            c.emplace_back (iter->address());
    }

    /** Returns a new set of connection addresses from the live cache. */
    template <class Container>
    void get_livecache (std::size_t needed, Container& c,
        SharedState::Access& state)
    {
        Endpoints endpoints (state->livecache.fetch_unique());
        Endpoints temp;
        temp.reserve (endpoints.size ());

        {
            // Remove the addresses we are currently connected to
            struct LessWithoutPortSet
            {
                bool operator() (
                    Endpoint const& lhs, IP::Endpoint const& rhs) const
                {
                    return lhs.address.address()  < rhs.address();
                }
                bool operator() (
                    Endpoint const& lhs, Endpoint const& rhs) const
                {
                    return lhs.address.address() < rhs.address.address();
                }
                bool operator() (
                    IP::Endpoint const& lhs, Endpoint const& rhs) const
                {
                    return lhs.address() < rhs.address.address();
                }
                bool operator() (
                    IP::Endpoint const& lhs, IP::Endpoint const& rhs) const
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
            c.push_back (iter->address);
    }

    //--------------------------------------------------------------------------

    /** Create new outbound connection attempts as needed.
        This implements PeerFinder's "Outbound Connection Strategy"
    */
    void makeOutgoingConnections ()
    {
        SharedState::Access state (m_state);

        // Count how many more outbound attempts to make
        //
        auto needed (state->counts.attempts_needed ());
        if (needed == 0)
            return;
        std::vector <IP::Endpoint> list;
        list.reserve (needed);

        // 1. Use Fixed if:
        //    Fixed active count is below fixed count AND
        //      ( There are eligible fixed addresses to try OR
        //        Any outbound attempts are in progress)
        //
        if (state->counts.fixed_active() < state->fixed.size ())
        {
            get_fixed (needed, list, state);

            if (! list.empty ())
            {
                if (m_journal.debug) m_journal.debug << leftw (18) <<
                    "Logic connect " << list.size() << " fixed";
                m_callback.connect (list);
                return;
            }
            
            if (state->counts.attempts() > 0)
            {
                if (m_journal.debug) m_journal.debug << leftw (18) <<
                    "Logic waiting on " <<
                        state->counts.attempts() << " attempts";
                return;
            }
        }

        // Only proceed if auto connect is enabled and we
        // have less than the desired number of outbound slots
        //
        if (! state->config.autoConnect ||
            state->counts.out_active () >= state->counts.out_max ())
            return;

        // 2. Use Livecache if:
        //    There are any entries in the cache OR
        //    Any outbound attempts are in progress
        //
        get_livecache (needed, list, state);
        if (! list.empty ())
        {
            if (m_journal.debug) m_journal.debug << leftw (18) <<
                "Logic connect " << list.size () << " live " <<
                ((list.size () > 1) ? "endpoints" : "endpoint");
            m_callback.connect (list);
            return;
        }
        else if (state->counts.attempts() > 0)
        {
            if (m_journal.debug) m_journal.debug << leftw (18) <<
                "Logic waiting on " <<
                    state->counts.attempts() << " attempts";
            return;
        }

        /*  3. Bootcache refill
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

        // 4. Use Bootcache if:
        //    There are any entries we haven't tried lately
        //
        get_bootcache (needed, list, state);
        if (! list.empty ())
        {
            if (m_journal.debug) m_journal.debug << leftw (18) <<
                "Logic connect " << list.size () << " boot " <<
                ((list.size () > 1) ? "addresses" : "address");
            m_callback.connect (list);
            return;
        }

        // If we get here we are stuck
    }

    //--------------------------------------------------------------------------

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
        for (auto iter : state->slots)
        {
            //Slot& slot (*iter->second);
            //slot.received.cycle();
        }
    }

    // Called periodically to update uptime for connected outbound peers.
    void processUptime (SharedState::Access& state)
    {
        for (auto entry : state->slots)
        {
            Slot const& slot (*entry.second);
            if (! slot.inbound() && slot.state() == Slot::active)
                state->bootcache.onConnectionActive (
                    slot.remote_endpoint());
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
    bool addBootcacheAddress (IP::Endpoint const& address,
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
        ep.address = IP::Endpoint (
            IP::AddressV4 ()).at_port (state->config.listeningPort);
        return ep;
    }

    // Returns true if the IP::Endpoint contains no invalid data.
    bool is_valid_address (IP::Endpoint const& address)
    {
        if (is_unspecified (address))
            return false;
        if (! is_public (address))
            return false;
        if (address.port() == 0)
            return false;
        return true;
    }

    // Creates a set of endpoints suitable for a temporary slot.
    // Sent to a slot when we are full, before disconnecting them.
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

    // Send mtENDPOINTS for the specified slot
    void sendEndpointsTo (Slot::ptr const& slot, Giveaways& g)
    {
        Endpoints endpoints;

        if (endpoints.size() < Tuning::numberOfEndpoints)
        {
            SharedState::Access state (m_state);

            // Add an entry for ourselves if:
            //  1. We want incoming
            //  2. We have counts
            //  3. We haven't failed the firewalled test
            //
            if (state->config.wantIncoming && state->counts.inboundSlots() > 0)
                endpoints.push_back (thisEndpoint (state));
        }

        if (endpoints.size() < Tuning::numberOfEndpoints)
        {
            g.append (Tuning::numberOfEndpoints - endpoints.size(), endpoints);
        }

        if (! endpoints.empty())
        {
            if (m_journal.trace) m_journal.trace << leftw (18) <<
                "Logic sending " << slot->remote_endpoint() << 
                " with " << endpoints.size() <<
                ((endpoints.size() > 1) ? " endpoints" : " endpoint");
            m_callback.send (slot, endpoints);
        }
    }

    // Send mtENDPOINTS for each slot as needed
    void broadcast ()
    {
        SharedState::Access state (m_state);
        if (! state->slots.empty())
        {
            clock_type::time_point const now (m_clock.now());
            clock_type::time_point const whenSendEndpoints (
                now + Tuning::secondsPerMessage);
            Giveaways g (state->livecache.giveaways ());
            for (auto entry : state->slots)
            {
                auto& slot (entry.second);
                if (slot->state() == Slot::active)
                {
                    if (slot->whenSendEndpoints <= now)
                    {
                        sendEndpointsTo (slot, g);
                        slot->whenSendEndpoints = whenSendEndpoints;
                    }
                }
            }
        }
    }

    // Called when the Checker completes a connectivity test
    void checkComplete (IP::Endpoint const& address,
        IP::Endpoint const & checkedAddress, Checker::Result const& result)
    {
        if (result.error == boost::asio::error::operation_aborted)
            return;

        SharedState::Access state (m_state);
        Slots::iterator const iter (state->slots.find (address));
        SlotImp& slot (*iter->second);

        if (iter == state->slots.end())
        {
            // The slot disconnected before we finished the check
            if (m_journal.debug) m_journal.debug << leftw (18) <<
                "Logic tested " << address <<
                " but the connection was closed";
            return;
        }

        // Mark that a check for this slot is finished.
        slot.connectivityCheckInProgress = false;

        if (! result.error)
        {
            slot.checked = true;
            slot.canAccept = result.canAccept;

            if (slot.canAccept)
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
            slot.checked = true;
            slot.canAccept = false;

            if (m_journal.error) m_journal.error << leftw (18) <<
                "Logic testing " << iter->first << " with error, " <<
                result.error.message();
        }

        if (slot.canAccept)
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
    bool haveLocalOutboundAddress (IP::Endpoint const& local_address,
        SharedState::Access& state)
    {
        for (Slots::const_iterator iter (state->slots.begin());
            iter != state->slots.end(); ++iter)
        {
            Slot const& slot (*iter->second);
            if (! slot.inbound () &&
                slot.local_endpoint() != boost::none &&
                *slot.local_endpoint() == local_address)
                return true;
        }
        return false;
    }

    //--------------------------------------------------------------------------

#if 0
    void onPeerAddressChanged (
        IP::Endpoint const& currentAddress, IP::Endpoint const& newAddress)
    {
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

        // Update the address on the slot
        Slot& slot (result.first->second.peersIterator()->second);
        slot.address = newAddress;
    }
#endif

    //--------------------------------------------------------------------------
    //
    // PropertyStream
    //
    //--------------------------------------------------------------------------

    void writeSlots (PropertyStream::Set& set, Slots const& slots)
    {
        for (auto entry : slots)
        {
            PropertyStream::Map item (set);
            SlotImp const& slot (*entry.second);
            if (slot.local_endpoint () != boost::none)
                item ["local_address"] = to_string (*slot.local_endpoint ());
            item ["remote_address"]   = to_string (slot.remote_endpoint ());
            if (slot.inbound())
                item ["inbound"]    = "yes";
            if (slot.fixed())
                item ["fixed"]      = "yes";
            if (slot.cluster())
                item ["cluster"]    = "yes";
            
            item ["state"] = stateString (slot.state());
        }
    }

    void onWrite (PropertyStream::Map& map)
    {
        SharedState::Access state (m_state);

        // VFALCO NOTE These ugly casts are needed because
        //             of how std::size_t is declared on some linuxes
        //
        map ["bootcache"]   = uint32 (state->bootcache.size());
        map ["fixed"]       = uint32 (state->fixed.size());

        {
            PropertyStream::Set child ("peers", map);
            writeSlots (child, state->slots);
        }

        {
            PropertyStream::Map child ("counts", map);
            state->counts.onWrite (child);
        }

        {
            PropertyStream::Map child ("config", map);
            state->config.onWrite (child);
        }

        {
            PropertyStream::Map child ("livecache", map);
            state->livecache.onWrite (child);
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

    Counts const& counts () const
    {
        return SharedState::ConstAccess (m_state)->counts;
    }

    static std::string stateString (Slot::State state)
    {
        switch (state)
        {
        case Slot::accept:     return "accept";
        case Slot::connect:    return "connect";
        case Slot::connected:  return "connected";
        case Slot::active:     return "active";
        case Slot::closing:    return "closing";
        default:
            break;
        };
        return "?";
    }

    void dump_peers (Journal::ScopedStream& ss,
        SharedState::ConstAccess const& state) const
    {
        ss << std::endl << std::endl <<
            "Slots";
        for (auto const entry : state->slots)
        {
            SlotImp const& slot (*entry.second);
            ss << std::endl <<
                slot.remote_endpoint () <<
                (slot.inbound () ? " (in) " : " ") <<
                stateString (slot.state ()) << " "
                // VFALCO NOTE currently this is broken
                /*
                << ((slot.public_key() != boost::none) ?
                    *slot.public_key() : "")
                */
                ;
        }
    }

    void dump (Journal::ScopedStream& ss) const
    {
        SharedState::ConstAccess state (m_state);

        state->bootcache.dump (ss);
        state->livecache.dump (ss);
        dump_peers (ss, state);
        ss << std::endl <<
            state->counts.state_string ();
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
