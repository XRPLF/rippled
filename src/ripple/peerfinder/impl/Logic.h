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

#include <ripple/peerfinder/impl/Fixed.h>
#include <ripple/peerfinder/impl/SlotImp.h>

#include <ripple/peerfinder/impl/handout.h>
#include <ripple/peerfinder/impl/ConnectHandouts.h>
#include <ripple/peerfinder/impl/RedirectHandouts.h>
#include <ripple/peerfinder/impl/SlotHandouts.h>

#include <beast/container/aged_container_utility.h>

#include <functional>
#include <map>
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
    typedef std::map <beast::IP::Endpoint,
        std::shared_ptr <SlotImp>> Slots;

    typedef std::map <beast::IP::Endpoint, Fixed> FixedSlots;

    // A set of unique Ripple public keys
    typedef std::set <RipplePublicKey> Keys;

    // A set of non-unique IPAddresses without ports, used
    // to filter duplicates when making outgoing connections.
    typedef std::multiset <beast::IP::Endpoint> ConnectedAddresses;

    struct State
    {
        State (
            Store* store,
            clock_type& clock,
            beast::Journal journal)
            : stopping (false)
            , counts ()
            , livecache (clock, beast::Journal (
                journal, Reporting::livecache))
            , bootcache (*store, clock, beast::Journal (
                journal, Reporting::bootcache))
        {
        }

        // True if we are stopping.
        bool stopping;

        // The source we are currently fetching.
        // This is used to cancel I/O during program exit.
        beast::SharedPtr <Source> fetchSource;

        // Configuration settings
        Config config;

        // Slot counts and other aggregate statistics.
        Counts counts;

        // A list of slots that should always be connected
        FixedSlots fixed;

        // Live livecache from mtENDPOINTS messages
        Livecache <> livecache;

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

    typedef beast::SharedData <State> SharedState;

    beast::Journal m_journal;
    SharedState m_state;
    clock_type& m_clock;
    Callback& m_callback;
    Store& m_store;
    Checker& m_checker;

    // A list of dynamic sources to consult as a fallback
    std::vector <beast::SharedPtr <Source>> m_sources;

    clock_type::time_point m_whenBroadcast;

    ConnectHandouts::Squelches m_squelches;

    //--------------------------------------------------------------------------

    Logic (
        clock_type& clock,
        Callback& callback,
        Store& store,
        Checker& checker,
        beast::Journal journal)
        : m_journal (journal, Reporting::logic)
        , m_state (&store, std::ref (clock), journal)
        , m_clock (clock)
        , m_callback (callback)
        , m_store (store)
        , m_checker (checker)
        , m_whenBroadcast (m_clock.now())
        , m_squelches (m_clock)
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
        std::vector <beast::IP::Endpoint> const& addresses)
    {
        SharedState::Access state (m_state);

        if (addresses.empty ())
        {
            if (m_journal.info) m_journal.info <<
                "Could not resolve fixed slot '" << name << "'";
            return;
        }

        for (auto const& remote_address : addresses)
        {
            auto result (state->fixed.emplace (std::piecewise_construct,
                std::forward_as_tuple (remote_address),
                    std::make_tuple (std::ref (m_clock))));

            if (result.second)
            {
                if (m_journal.debug) m_journal.debug << beast::leftw (18) <<
                    "Logic add fixed" << "'" << name <<
                    "' at " << remote_address;
                return;
            }
        }
    }

    //--------------------------------------------------------------------------

    // Called when the Checker completes a connectivity test
    void checkComplete (beast::IP::Endpoint const& address,
        beast::IP::Endpoint const & checkedAddress, Checker::Result const& result)
    {
        if (result.error == boost::asio::error::operation_aborted)
            return;

        SharedState::Access state (m_state);
        Slots::iterator const iter (state->slots.find (address));
        SlotImp& slot (*iter->second);

        if (iter == state->slots.end())
        {
            // The slot disconnected before we finished the check
            if (m_journal.debug) m_journal.debug << beast::leftw (18) <<
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
                if (m_journal.debug) m_journal.debug << beast::leftw (18) <<
                    "Logic testing " << address << " succeeded";
            }
            else
            {
                if (m_journal.info) m_journal.info << beast::leftw (18) <<
                    "Logic testing " << address << " failed";
            }
        }
        else
        {
            // VFALCO TODO Should we retry depending on the error?
            slot.checked = true;
            slot.canAccept = false;

            if (m_journal.error) m_journal.error << beast::leftw (18) <<
                "Logic testing " << iter->first << " with error, " <<
                result.error.message();
        }

        if (! slot.canAccept)
            state->bootcache.on_failure (address);
    }

    //--------------------------------------------------------------------------

    SlotImp::ptr new_inbound_slot (beast::IP::Endpoint const& local_endpoint,
        beast::IP::Endpoint const& remote_endpoint)
    {
        if (m_journal.debug) m_journal.debug << beast::leftw (18) <<
            "Logic accept" << remote_endpoint <<
            " on local " << local_endpoint;

        SharedState::Access state (m_state);

        // Check for self-connect by address
        {
            auto const iter (state->slots.find (local_endpoint));
            if (iter != state->slots.end ())
            {
                Slot::ptr const& self (iter->second);
                assert ((self->local_endpoint () == boost::none) ||
                    (self->local_endpoint () == remote_endpoint));
                if (m_journal.warning) m_journal.warning << beast::leftw (18) <<
                    "Logic dropping " << remote_endpoint <<
                    " as self connect";
                return SlotImp::ptr ();
            }
        }

        // Create the slot
        SlotImp::ptr const slot (std::make_shared <SlotImp> (local_endpoint,
            remote_endpoint, fixed (remote_endpoint.address (), state),
                m_clock));
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

    SlotImp::ptr new_outbound_slot (beast::IP::Endpoint const& remote_endpoint)
    {
        if (m_journal.debug) m_journal.debug << beast::leftw (18) <<
            "Logic connect " << remote_endpoint;

        SharedState::Access state (m_state);

        // Check for duplicate connection
        if (state->slots.find (remote_endpoint) !=
            state->slots.end ())
        {
            if (m_journal.warning) m_journal.warning << beast::leftw (18) <<
                "Logic dropping " << remote_endpoint <<
                " as duplicate connect";
            return SlotImp::ptr ();
        }

        // Create the slot
        SlotImp::ptr const slot (std::make_shared <SlotImp> (
            remote_endpoint, fixed (remote_endpoint, state), m_clock));

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
        beast::IP::Endpoint const& local_endpoint)
    {
        if (m_journal.trace) m_journal.trace << beast::leftw (18) <<
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
                if (m_journal.warning) m_journal.warning << beast::leftw (18) <<
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
        if (m_journal.debug) m_journal.debug << beast::leftw (18) <<
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
                state->bootcache.on_success (
                    slot->remote_endpoint());

            // Mark fixed slot success
            if (slot->fixed() && ! slot->inbound())
            {
                auto iter (state->fixed.find (slot->remote_endpoint()));
                assert (iter != state->fixed.end ());
                iter->second.success (m_clock.now ());
                if (m_journal.trace) m_journal.trace << beast::leftw (18) <<
                    "Logic fixed " << slot->remote_endpoint () << " success";
            }

            m_callback.activate (slot);
        }
        else
        {
            if (! slot->inbound())
                state->bootcache.on_success (
                    slot->remote_endpoint());

            // Maybe give them some addresses to try
            if (slot->inbound ())
                redirect (slot, state);

            m_callback.disconnect (slot, true);
        }
    }

    //--------------------------------------------------------------------------

    // Validate and clean up the list that we received from the slot.
    void preprocess (SlotImp::ptr const& slot, Endpoints& list,
        SharedState::Access& state)
    {
        bool neighbor (false);
        for (auto iter (list.begin()); iter != list.end();)
        {
            Endpoint& ep (*iter);

            // Enforce hop limit
            if (ep.hops > Tuning::maxHops)
            {
                if (m_journal.warning) m_journal.warning << beast::leftw (18) <<
                    "Endpoints drop " << ep.address <<
                    " for excess hops " << ep.hops;
                iter = list.erase (iter);
                continue;
            }

            // See if we are directly connected
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
                    if (m_journal.warning) m_journal.warning << beast::leftw (18) <<
                        "Endpoints drop " << ep.address <<
                        " for extra self";
                    iter = list.erase (iter);
                    continue;
                }
            }

            // Discard invalid addresses
            if (! is_valid_address (ep.address))
            {
                if (m_journal.warning) m_journal.warning << beast::leftw (18) <<
                    "Endpoints drop " << ep.address <<
                    " as invalid";
                iter = list.erase (iter);
                continue;
            }

            // Filter duplicates
            if (std::any_of (list.begin(), iter,
                [ep](Endpoints::value_type const& other)
                {
                    return ep.address == other.address;
                }))
            {
                if (m_journal.warning) m_journal.warning << beast::leftw (18) <<
                    "Endpoints drop " << ep.address <<
                    " as duplicate";
                iter = list.erase (iter);
                continue;
            }

            // Increment hop count on the incoming message, so
            // we store it at the hop count we will send it at.
            //
            ++ep.hops;

            ++iter;
        }
    }

    void on_endpoints (SlotImp::ptr const& slot, Endpoints list)
    {
        if (m_journal.trace) m_journal.trace << beast::leftw (18) <<
            "Endpoints from " << slot->remote_endpoint () <<
            " contained " << list.size () <<
            ((list.size() > 1) ? " entries" : " entry");

        SharedState::Access state (m_state);
   
        // The object must exist in our table
        assert (state->slots.find (slot->remote_endpoint ()) !=
            state->slots.end ());

        // Must be handshaked!
        assert (slot->state() == Slot::active);

        preprocess (slot, list, state);

        clock_type::time_point const now (m_clock.now());

        for (auto iter (list.cbegin()); iter != list.cend(); ++iter)
        {
            Endpoint const& ep (*iter);

            assert (ep.hops != 0);

            slot->recent.insert (ep.address, ep.hops);

            // Note hops has been incremented, so 1
            // means a directly connected neighbor.
            //
            if (ep.hops == 1)
            {
                if (slot->connectivityCheckInProgress)
                {
                    if (m_journal.warning) m_journal.warning << beast::leftw (18) <<
                        "Logic testing " << ep.address << " already in progress";
                    continue;
                }
                
                if (! slot->checked)
                {
                    // Mark that a check for this slot is now in progress.
                    slot->connectivityCheckInProgress = true;

                    // Test the slot's listening port before
                    // adding it to the livecache for the first time.
                    //                     
                    m_checker.async_test (ep.address, std::bind (
                        &Logic::checkComplete, this, slot->remote_endpoint (),
                            ep.address, std::placeholders::_1));

                    // Note that we simply discard the first Endpoint
                    // that the neighbor sends when we perform the
                    // listening test. They will just send us another
                    // one in a few seconds.

                    continue;
                }
 
                // If they failed the test then skip the address
                if (! slot->canAccept)
                    continue;
            }

            // We only add to the livecache if the neighbor passed the
            // listening test, else we silently drop their messsage
            // since their listening port is misconfigured.
            //
            state->livecache.insert (ep);
            state->bootcache.insert (ep.address);
        }

        slot->whenAcceptEndpoints = now + Tuning::secondsPerMessage;
    }

    //--------------------------------------------------------------------------

    void on_legacy_endpoints (IPAddresses const& list)
    {
        // Ignoring them also seems a valid choice.
        SharedState::Access state (m_state);
        for (IPAddresses::const_iterator iter (list.begin());
            iter != list.end(); ++iter)
            state->bootcache.insert (*iter);
    }

    void remove (SlotImp::ptr const& slot, SharedState::Access& state)
    {
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
    }

    void on_closed (SlotImp::ptr const& slot)
    {
        SharedState::Access state (m_state);

        remove (slot, state);

        // Mark fixed slot failure
        if (slot->fixed() && ! slot->inbound() && slot->state() != Slot::active)
        {
            auto iter (state->fixed.find (slot->remote_endpoint()));
            assert (iter != state->fixed.end ());
            iter->second.failure (m_clock.now ());
            if (m_journal.debug) m_journal.debug << beast::leftw (18) <<
                "Logic fixed " << slot->remote_endpoint () << " failed";
        }

        // Do state specific bookkeeping
        switch (slot->state())
        {
        case Slot::accept:
            if (m_journal.trace) m_journal.trace << beast::leftw (18) <<
                "Logic accept " << slot->remote_endpoint () << " failed";
            break;

        case Slot::connect:
        case Slot::connected:
            state->bootcache.on_failure (slot->remote_endpoint ());
            // VFALCO TODO If the address exists in the ephemeral/live
            //             endpoint livecache then we should mark the failure
            // as if it didn't pass the listening test. We should also
            // avoid propagating the address.
            break;

        case Slot::active:
            if (m_journal.trace) m_journal.trace << beast::leftw (18) <<
                "Logic close " << slot->remote_endpoint();
            break;

        case Slot::closing:
            if (m_journal.trace) m_journal.trace << beast::leftw (18) <<
                "Logic finished " << slot->remote_endpoint();
            break;

        default:
            assert (false);
            break;
        }
    }

    void on_cancel (SlotImp::ptr const& slot)
    {
        SharedState::Access state (m_state);

        remove (slot, state);

        if (m_journal.trace) m_journal.trace << beast::leftw (18) <<
            "Logic cancel " << slot->remote_endpoint();
    }

    //--------------------------------------------------------------------------

    // Returns `true` if the address matches a fixed slot address
    bool fixed (beast::IP::Endpoint const& endpoint, SharedState::Access& state) const
    {
        for (auto const& entry : state->fixed)
            if (entry.first == endpoint)
                return true;
        return false;
    }

    // Returns `true` if the address matches a fixed slot address
    // Note that this does not use the port information in the IP::Endpoint
    bool fixed (beast::IP::Address const& address, SharedState::Access& state) const
    {
        for (auto const& entry : state->fixed)
            if (entry.first.address () == address)
                return true;
        return false;
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

    //--------------------------------------------------------------------------

    // Adds slot addresses to the squelched set
    void squelch_slots (SharedState::Access& state)
    {
        for (auto const& s : state->slots)
        {
            auto const result (m_squelches.insert (
                s.second->remote_endpoint().address()));
            if (! result.second)
                m_squelches.touch (result.first);
        }
    }

    /** Create new outbound connection attempts as needed.
        This implements PeerFinder's "Outbound Connection Strategy"
    */
    void autoconnect ()
    {
        SharedState::Access state (m_state);

        // Count how many more outbound attempts to make
        //
        auto needed (state->counts.attempts_needed ());
        if (needed == 0)
            return;

        ConnectHandouts h (needed, m_squelches);

        // Make sure we don't connect to already-connected entries.
        squelch_slots (state);

        // 1. Use Fixed if:
        //    Fixed active count is below fixed count AND
        //      ( There are eligible fixed addresses to try OR
        //        Any outbound attempts are in progress)
        //
        if (state->counts.fixed_active() < state->fixed.size ())
        {
            get_fixed (needed, h.list(), state);

            if (! h.list().empty ())
            {
                if (m_journal.debug) m_journal.debug << beast::leftw (18) <<
                    "Logic connect " << h.list().size() << " fixed";
                m_callback.connect (h.list());
                return;
            }

            if (state->counts.attempts() > 0)
            {
                if (m_journal.debug) m_journal.debug << beast::leftw (18) <<
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
        {
            state->livecache.hops.shuffle();
            handout (&h, (&h)+1,
                state->livecache.hops.rbegin(),
                    state->livecache.hops.rend());
            if (! h.list().empty ())
            {
                if (m_journal.debug) m_journal.debug << beast::leftw (18) <<
                    "Logic connect " << h.list().size () << " live " <<
                    ((h.list().size () > 1) ? "endpoints" : "endpoint");
                m_callback.connect (h.list());
                return;
            }
            else if (state->counts.attempts() > 0)
            {
                if (m_journal.debug) m_journal.debug << beast::leftw (18) <<
                    "Logic waiting on " <<
                        state->counts.attempts() << " attempts";
                return;
            }
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
        for (auto iter (state->bootcache.begin());
            ! h.full() && iter != state->bootcache.end(); ++iter)
            h.try_insert (*iter);

        if (! h.list().empty ())
        {
            if (m_journal.debug) m_journal.debug << beast::leftw (18) <<
                "Logic connect " << h.list().size () << " boot " <<
                ((h.list().size () > 1) ? "addresses" : "address");
            m_callback.connect (h.list());
            return;
        }

        // If we get here we are stuck
    }

    //--------------------------------------------------------------------------

    void addStaticSource (beast::SharedPtr <Source> const& source)
    {
        fetch (source);
    }

    void addSource (beast::SharedPtr <Source> const& source)
    {
        m_sources.push_back (source);
    }

    //--------------------------------------------------------------------------

    // Called periodically to sweep the livecache and remove aged out items.
    void expire ()
    {
        SharedState::Access state (m_state);

        // Expire the Livecache
        state->livecache.expire ();

        // Expire the recent cache in each slot
        for (auto const& entry : state->slots)
            entry.second->expire();

        // Expire the recent attempts table
        beast::expire (m_squelches,
            Tuning::recentAttemptDuration);
    }

    // Called every so often to perform periodic tasks.
    void periodicActivity ()
    {
        SharedState::Access state (m_state);

        clock_type::time_point const now (m_clock.now());

        autoconnect ();
        expire ();
        state->bootcache.periodicActivity ();

        if (m_whenBroadcast <= now)
        {
            broadcast ();
            m_whenBroadcast = now + Tuning::secondsPerMessage;
        }
    }

    //--------------------------------------------------------------------------
    //
    // Bootcache livecache sources
    //
    //--------------------------------------------------------------------------

    // Add one address.
    // Returns `true` if the address is new.
    //
    bool addBootcacheAddress (beast::IP::Endpoint const& address,
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
        for (auto addr : list)
        {
            if (addBootcacheAddress (addr, state))
                ++count;
        }
        return count;
    }

    // Fetch bootcache addresses from the specified source.
    void fetch (beast::SharedPtr <Source> const& source)
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
            if (m_journal.info) m_journal.info << beast::leftw (18) <<
                "Logic added " << count <<
                " new " << ((count == 1) ? "address" : "addresses") <<
                " from " << source->name();
        }
        else
        {
            if (m_journal.error) m_journal.error << beast::leftw (18) <<
                "Logic failed " << "'" << source->name() << "' fetch, " <<
                results.error.message();
        }
    }

    //--------------------------------------------------------------------------
    //
    // Endpoint message handling
    //
    //--------------------------------------------------------------------------

    // Returns true if the IP::Endpoint contains no invalid data.
    bool is_valid_address (beast::IP::Endpoint const& address)
    {
        if (is_unspecified (address))
            return false;
        if (! is_public (address))
            return false;
        if (address.port() == 0)
            return false;
        return true;
    }

    //--------------------------------------------------------------------------

    // Gives a slot a set of addresses to try next since we're full
    void redirect (SlotImp::ptr const& slot, SharedState::Access& state)
    {
        RedirectHandouts h (slot);
        state->livecache.hops.shuffle();
        handout (&h, (&h)+1,
            state->livecache.hops.begin(),
                state->livecache.hops.end());

        if (! h.list().empty ())
        {
            if (m_journal.trace) m_journal.trace << beast::leftw (18) <<
                "Logic redirect " << slot->remote_endpoint() <<
                " with " << h.list().size() <<
                ((h.list().size() == 1) ? " address" : " addresses");
            m_callback.send (slot, h.list());
        }
        else
        {
            if (m_journal.warning) m_journal.warning << beast::leftw (18) <<
                "Logic deferred " << slot->remote_endpoint();
        }
    }

    // Send mtENDPOINTS for each slot as needed
    void broadcast ()
    {
        SharedState::Access state (m_state);

        std::vector <SlotHandouts> targets;

        {
            // build list of active slots
            std::vector <SlotImp::ptr> slots;
            slots.reserve (state->slots.size());
            std::for_each (state->slots.cbegin(), state->slots.cend(),
                [&slots](Slots::value_type const& value)
                {
                    if (value.second->state() == Slot::active)
                        slots.emplace_back (value.second);
                });
            std::random_shuffle (slots.begin(), slots.end());

            // build target vector
            targets.reserve (slots.size());
            std::for_each (slots.cbegin(), slots.cend(),
                [&targets](SlotImp::ptr const& slot)
                {
                    targets.emplace_back (slot);
                });
        }
        
        /* VFALCO NOTE
            This is a temporary measure. Once we know our own IP
            address, the correct solution is to put it into the Livecache
            at hops 0, and go through the regular handout path. This way
            we avoid handing our address out too frequenty, which this code
            suffers from.
        */
        // Add an entry for ourselves if:
        // 1. We want incoming
        // 2. We have slots
        // 3. We haven't failed the firewalled test
        //
        if (state->config.wantIncoming &&
            state->counts.inboundSlots() > 0)
        {
            Endpoint ep;
            ep.hops = 0;
            ep.address = beast::IP::Endpoint (
                beast::IP::AddressV4 ()).at_port (
                    state->config.listeningPort);
            for (auto& t : targets)
                t.insert (ep);
        }

        // build sequence of endpoints by hops
        state->livecache.hops.shuffle();
        handout (targets.begin(), targets.end(),
            state->livecache.hops.begin(),
                state->livecache.hops.end());

        // broadcast
        for (auto const& t : targets)
        {
            SlotImp::ptr const& slot (t.slot());
            auto const& list (t.list());
            if (m_journal.trace) m_journal.trace << beast::leftw (18) <<
                "Logic sending " << slot->remote_endpoint() << 
                " with " << list.size() <<
                ((list.size() == 1) ? " endpoint" : " endpoints");
            m_callback.send (slot, list);
        }
    }

    //--------------------------------------------------------------------------
    //
    // PropertyStream
    //
    //--------------------------------------------------------------------------

    void writeSlots (beast::PropertyStream::Set& set, Slots const& slots)
    {
        for (auto const& entry : slots)
        {
            beast::PropertyStream::Map item (set);
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

    void onWrite (beast::PropertyStream::Map& map)
    {
        SharedState::Access state (m_state);

        // VFALCO NOTE These ugly casts are needed because
        //             of how std::size_t is declared on some linuxes
        //
        map ["bootcache"]   = std::uint32_t (state->bootcache.size());
        map ["fixed"]       = std::uint32_t (state->fixed.size());

        {
            beast::PropertyStream::Set child ("peers", map);
            writeSlots (child, state->slots);
        }

        {
            beast::PropertyStream::Map child ("counts", map);
            state->counts.onWrite (child);
        }

        {
            beast::PropertyStream::Map child ("config", map);
            state->config.onWrite (child);
        }

        {
            beast::PropertyStream::Map child ("livecache", map);
            state->livecache.onWrite (child);
        }

        {
            beast::PropertyStream::Map child ("bootcache", map);
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
};

}
}

#endif

/*

- recent tables entries should last equal to the cache time to live
- never send a slot a message thats in its recent table at a lower hop count
- when sending a message to a slot, add it to its recent table at one lower hop count

Giveaway logic

When do we give away?

- To one inbound connection when we redirect due to full

- To all slots at every broadcast

*/
