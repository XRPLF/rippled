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

#include <xrpld/peerfinder/PeerfinderManager.h>
#include <xrpld/peerfinder/detail/Bootcache.h>
#include <xrpld/peerfinder/detail/Counts.h>
#include <xrpld/peerfinder/detail/Fixed.h>
#include <xrpld/peerfinder/detail/Handouts.h>
#include <xrpld/peerfinder/detail/Livecache.h>
#include <xrpld/peerfinder/detail/SlotImp.h>
#include <xrpld/peerfinder/detail/Source.h>
#include <xrpld/peerfinder/detail/Store.h>
#include <xrpld/peerfinder/detail/iosformat.h>
#include <xrpl/basics/Log.h>
#include <xrpl/basics/contract.h>
#include <xrpl/basics/random.h>
#include <xrpl/beast/container/aged_container_utility.h>
#include <xrpl/beast/net/IPAddressConversion.h>

#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <set>

namespace ripple {
namespace PeerFinder {

/** The Logic for maintaining the list of Slot addresses.
    We keep this in a separate class so it can be instantiated
    for unit tests.
*/
template <class Checker>
class Logic
{
public:
    // Maps remote endpoints to slots. Since a slot has a
    // remote endpoint upon construction, this holds all counts.
    //
    using Slots = std::map<beast::IP::Endpoint, std::shared_ptr<SlotImp>>;

    beast::Journal m_journal;
    clock_type& m_clock;
    Store& m_store;
    Checker& m_checker;

    std::recursive_mutex lock_;

    // True if we are stopping.
    bool stopping_ = false;

    // The source we are currently fetching.
    // This is used to cancel I/O during program exit.
    std::shared_ptr<Source> fetchSource_;

    // Configuration settings
    Config config_;

    // Slot counts and other aggregate statistics.
    Counts counts_;

    // A list of slots that should always be connected
    std::map<beast::IP::Endpoint, Fixed> fixed_;

    // Live livecache from mtENDPOINTS messages
    Livecache<> livecache_;

    // LiveCache of addresses suitable for gaining initial connections
    Bootcache bootcache_;

    // Holds all counts
    Slots slots_;

    // The addresses (but not port) we are connected to. This includes
    // outgoing connection attempts. Note that this set can contain
    // duplicates (since the port is not set)
    std::multiset<beast::IP::Address> connectedAddresses_;

    // Set of public keys belonging to active peers
    std::set<PublicKey> keys_;

    // A list of dynamic sources to consult as a fallback
    std::vector<std::shared_ptr<Source>> m_sources;

    clock_type::time_point m_whenBroadcast;

    ConnectHandouts::Squelches m_squelches;

    //--------------------------------------------------------------------------

    Logic(
        clock_type& clock,
        Store& store,
        Checker& checker,
        beast::Journal journal)
        : m_journal(journal)
        , m_clock(clock)
        , m_store(store)
        , m_checker(checker)
        , livecache_(m_clock, journal)
        , bootcache_(store, m_clock, journal)
        , m_whenBroadcast(m_clock.now())
        , m_squelches(m_clock)
    {
        config({});
    }

    // Load persistent state information from the Store
    //
    void
    load()
    {
        std::lock_guard _(lock_);
        bootcache_.load();
    }

    /** Stop the logic.
        This will cancel the current fetch and set the stopping flag
        to `true` to prevent further fetches.
        Thread safety:
            Safe to call from any thread.
    */
    void
    stop()
    {
        std::lock_guard _(lock_);
        stopping_ = true;
        if (fetchSource_ != nullptr)
            fetchSource_->cancel();
    }

    //--------------------------------------------------------------------------
    //
    // Manager
    //
    //--------------------------------------------------------------------------

    void
    config(Config const& c)
    {
        std::lock_guard _(lock_);
        config_ = c;
        counts_.onConfig(config_);
    }

    Config
    config()
    {
        std::lock_guard _(lock_);
        return config_;
    }

    void
    addFixedPeer(std::string const& name, beast::IP::Endpoint const& ep)
    {
        std::vector<beast::IP::Endpoint> v;
        v.push_back(ep);
        addFixedPeer(name, v);
    }

    void
    addFixedPeer(
        std::string const& name,
        std::vector<beast::IP::Endpoint> const& addresses)
    {
        std::lock_guard _(lock_);

        if (addresses.empty())
        {
            JLOG(m_journal.info())
                << "Could not resolve fixed slot '" << name << "'";
            return;
        }

        for (auto const& remote_address : addresses)
        {
            if (remote_address.port() == 0)
            {
                Throw<std::runtime_error>(
                    "Port not specified for address:" +
                    remote_address.to_string());
            }

            auto result(fixed_.emplace(
                std::piecewise_construct,
                std::forward_as_tuple(remote_address),
                std::make_tuple(std::ref(m_clock))));

            if (result.second)
            {
                JLOG(m_journal.debug())
                    << beast::leftw(18) << "Logic add fixed '" << name
                    << "' at " << remote_address;
                return;
            }
        }
    }

    //--------------------------------------------------------------------------

    // Called when the Checker completes a connectivity test
    void
    checkComplete(
        beast::IP::Endpoint const& remoteAddress,
        beast::IP::Endpoint const& checkedAddress,
        boost::system::error_code ec)
    {
        if (ec == boost::asio::error::operation_aborted)
            return;

        std::lock_guard _(lock_);
        auto const iter(slots_.find(remoteAddress));
        if (iter == slots_.end())
        {
            // The slot disconnected before we finished the check
            JLOG(m_journal.debug())
                << beast::leftw(18) << "Logic tested " << checkedAddress
                << " but the connection was closed";
            return;
        }

        SlotImp& slot(*iter->second);
        slot.checked = true;
        slot.connectivityCheckInProgress = false;

        if (ec)
        {
            // VFALCO TODO Should we retry depending on the error?
            slot.canAccept = false;
            JLOG(m_journal.error())
                << beast::leftw(18) << "Logic testing " << iter->first
                << " with error, " << ec.message();
            bootcache_.on_failure(checkedAddress);
            return;
        }

        slot.canAccept = true;
        slot.set_listening_port(checkedAddress.port());
        JLOG(m_journal.debug()) << beast::leftw(18) << "Logic testing "
                                << checkedAddress << " succeeded";
    }

    //--------------------------------------------------------------------------

    SlotImp::ptr
    new_inbound_slot(
        beast::IP::Endpoint const& local_endpoint,
        beast::IP::Endpoint const& remote_endpoint)
    {
        JLOG(m_journal.debug())
            << beast::leftw(18) << "Logic accept" << remote_endpoint
            << " on local " << local_endpoint;

        std::lock_guard _(lock_);

        // Check for connection limit per address
        if (is_public(remote_endpoint))
        {
            auto const count =
                connectedAddresses_.count(remote_endpoint.address());
            if (count > config_.ipLimit)
            {
                JLOG(m_journal.debug())
                    << beast::leftw(18) << "Logic dropping inbound "
                    << remote_endpoint << " because of ip limits.";
                return SlotImp::ptr();
            }
        }

        // Check for duplicate connection
        if (slots_.find(remote_endpoint) != slots_.end())
        {
            JLOG(m_journal.debug())
                << beast::leftw(18) << "Logic dropping " << remote_endpoint
                << " as duplicate incoming";
            return SlotImp::ptr();
        }

        // Create the slot
        SlotImp::ptr const slot(std::make_shared<SlotImp>(
            local_endpoint,
            remote_endpoint,
            fixed(remote_endpoint.address()),
            m_clock));
        // Add slot to table
        auto const result(slots_.emplace(slot->remote_endpoint(), slot));
        // Remote address must not already exist
        ASSERT(
            result.second,
            "ripple::PeerFinder::Logic::new_inbound_slot : remote endpoint "
            "inserted");
        // Add to the connected address list
        connectedAddresses_.emplace(remote_endpoint.address());

        // Update counts
        counts_.add(*slot);

        return result.first->second;
    }

    // Can't check for self-connect because we don't know the local endpoint
    SlotImp::ptr
    new_outbound_slot(beast::IP::Endpoint const& remote_endpoint)
    {
        JLOG(m_journal.debug())
            << beast::leftw(18) << "Logic connect " << remote_endpoint;

        std::lock_guard _(lock_);

        // Check for duplicate connection
        if (slots_.find(remote_endpoint) != slots_.end())
        {
            JLOG(m_journal.debug())
                << beast::leftw(18) << "Logic dropping " << remote_endpoint
                << " as duplicate connect";
            return SlotImp::ptr();
        }

        // Create the slot
        SlotImp::ptr const slot(std::make_shared<SlotImp>(
            remote_endpoint, fixed(remote_endpoint), m_clock));

        // Add slot to table
        auto const result = slots_.emplace(slot->remote_endpoint(), slot);
        // Remote address must not already exist
        ASSERT(
            result.second,
            "ripple::PeerFinder::Logic::new_outbound_slot : remote endpoint "
            "inserted");

        // Add to the connected address list
        connectedAddresses_.emplace(remote_endpoint.address());

        // Update counts
        counts_.add(*slot);

        return result.first->second;
    }

    bool
    onConnected(
        SlotImp::ptr const& slot,
        beast::IP::Endpoint const& local_endpoint)
    {
        JLOG(m_journal.trace())
            << beast::leftw(18) << "Logic connected" << slot->remote_endpoint()
            << " on local " << local_endpoint;

        std::lock_guard _(lock_);

        // The object must exist in our table
        ASSERT(
            slots_.find(slot->remote_endpoint()) != slots_.end(),
            "ripple::PeerFinder::Logic::onConnected : valid slot input");
        // Assign the local endpoint now that it's known
        slot->local_endpoint(local_endpoint);

        // Check for self-connect by address
        {
            auto const iter(slots_.find(local_endpoint));
            if (iter != slots_.end())
            {
                ASSERT(
                    iter->second->local_endpoint() == slot->remote_endpoint(),
                    "ripple::PeerFinder::Logic::onConnected : local and remote "
                    "endpoints do match");
                JLOG(m_journal.warn())
                    << beast::leftw(18) << "Logic dropping "
                    << slot->remote_endpoint() << " as self connect";
                return false;
            }
        }

        // Update counts
        counts_.remove(*slot);
        slot->state(Slot::connected);
        counts_.add(*slot);
        return true;
    }

    Result
    activate(SlotImp::ptr const& slot, PublicKey const& key, bool reserved)
    {
        JLOG(m_journal.debug())
            << beast::leftw(18) << "Logic handshake " << slot->remote_endpoint()
            << " with " << (reserved ? "reserved " : "") << "key " << key;

        std::lock_guard _(lock_);

        // The object must exist in our table
        ASSERT(
            slots_.find(slot->remote_endpoint()) != slots_.end(),
            "ripple::PeerFinder::Logic::activate : valid slot input");
        // Must be accepted or connected
        ASSERT(
            slot->state() == Slot::accept || slot->state() == Slot::connected,
            "ripple::PeerFinder::Logic::activate : valid slot state");

        // Check for duplicate connection by key
        if (keys_.find(key) != keys_.end())
            return Result::duplicate;

        // If the peer belongs to a cluster or is reserved,
        // update the slot to reflect that.
        counts_.remove(*slot);
        slot->reserved(reserved);
        counts_.add(*slot);

        // See if we have an open space for this slot
        if (!counts_.can_activate(*slot))
        {
            if (!slot->inbound())
                bootcache_.on_success(slot->remote_endpoint());
            return Result::full;
        }

        // Set the key right before adding to the map, otherwise we might
        // assert later when erasing the key.
        slot->public_key(key);
        {
            [[maybe_unused]] bool const inserted = keys_.insert(key).second;
            // Public key must not already exist
            ASSERT(
                inserted,
                "ripple::PeerFinder::Logic::activate : public key inserted");
        }

        // Change state and update counts
        counts_.remove(*slot);
        slot->activate(m_clock.now());
        counts_.add(*slot);

        if (!slot->inbound())
            bootcache_.on_success(slot->remote_endpoint());

        // Mark fixed slot success
        if (slot->fixed() && !slot->inbound())
        {
            auto iter(fixed_.find(slot->remote_endpoint()));
            if (iter == fixed_.end())
                LogicError(
                    "PeerFinder::Logic::activate(): remote_endpoint "
                    "missing from fixed_");

            iter->second.success(m_clock.now());
            JLOG(m_journal.trace()) << beast::leftw(18) << "Logic fixed "
                                    << slot->remote_endpoint() << " success";
        }

        return Result::success;
    }

    /** Return a list of addresses suitable for redirection.
        This is a legacy function, redirects should be returned in
        the HTTP handshake and not via TMEndpoints.
    */
    std::vector<Endpoint>
    redirect(SlotImp::ptr const& slot)
    {
        std::lock_guard _(lock_);
        RedirectHandouts h(slot);
        livecache_.hops.shuffle();
        handout(&h, (&h) + 1, livecache_.hops.begin(), livecache_.hops.end());
        return std::move(h.list());
    }

    /** Create new outbound connection attempts as needed.
        This implements PeerFinder's "Outbound Connection Strategy"
    */
    // VFALCO TODO This should add the returned addresses to the
    //             squelch list in one go once the list is built,
    //             rather than having each module add to the squelch list.
    std::vector<beast::IP::Endpoint>
    autoconnect()
    {
        std::vector<beast::IP::Endpoint> const none;

        std::lock_guard _(lock_);

        // Count how many more outbound attempts to make
        //
        auto needed(counts_.attempts_needed());
        if (needed == 0)
            return none;

        ConnectHandouts h(needed, m_squelches);

        // Make sure we don't connect to already-connected entries.
        for (auto const& s : slots_)
        {
            auto const result(
                m_squelches.insert(s.second->remote_endpoint().address()));
            if (!result.second)
                m_squelches.touch(result.first);
        }

        // 1. Use Fixed if:
        //    Fixed active count is below fixed count AND
        //      ( There are eligible fixed addresses to try OR
        //        Any outbound attempts are in progress)
        //
        if (counts_.fixed_active() < fixed_.size())
        {
            get_fixed(needed, h.list(), m_squelches);

            if (!h.list().empty())
            {
                JLOG(m_journal.debug()) << beast::leftw(18) << "Logic connect "
                                        << h.list().size() << " fixed";
                return h.list();
            }

            if (counts_.attempts() > 0)
            {
                JLOG(m_journal.debug())
                    << beast::leftw(18) << "Logic waiting on "
                    << counts_.attempts() << " attempts";
                return none;
            }
        }

        // Only proceed if auto connect is enabled and we
        // have less than the desired number of outbound slots
        //
        if (!config_.autoConnect || counts_.out_active() >= counts_.out_max())
            return none;

        // 2. Use Livecache if:
        //    There are any entries in the cache OR
        //    Any outbound attempts are in progress
        //
        {
            livecache_.hops.shuffle();
            handout(
                &h, (&h) + 1, livecache_.hops.rbegin(), livecache_.hops.rend());
            if (!h.list().empty())
            {
                JLOG(m_journal.debug())
                    << beast::leftw(18) << "Logic connect " << h.list().size()
                    << " live "
                    << ((h.list().size() > 1) ? "endpoints" : "endpoint");
                return h.list();
            }
            else if (counts_.attempts() > 0)
            {
                JLOG(m_journal.debug())
                    << beast::leftw(18) << "Logic waiting on "
                    << counts_.attempts() << " attempts";
                return none;
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
                        Contact DomainName and add entries to the
           unusedBootstrapIPs return;
        */

        // 4. Use Bootcache if:
        //    There are any entries we haven't tried lately
        //
        for (auto iter(bootcache_.begin());
             !h.full() && iter != bootcache_.end();
             ++iter)
            h.try_insert(*iter);

        if (!h.list().empty())
        {
            JLOG(m_journal.debug())
                << beast::leftw(18) << "Logic connect " << h.list().size()
                << " boot "
                << ((h.list().size() > 1) ? "addresses" : "address");
            return h.list();
        }

        // If we get here we are stuck
        return none;
    }

    std::vector<std::pair<std::shared_ptr<Slot>, std::vector<Endpoint>>>
    buildEndpointsForPeers()
    {
        std::vector<std::pair<std::shared_ptr<Slot>, std::vector<Endpoint>>>
            result;

        std::lock_guard _(lock_);

        clock_type::time_point const now = m_clock.now();
        if (m_whenBroadcast <= now)
        {
            std::vector<SlotHandouts> targets;

            {
                // build list of active slots
                std::vector<SlotImp::ptr> slots;
                slots.reserve(slots_.size());
                std::for_each(
                    slots_.cbegin(),
                    slots_.cend(),
                    [&slots](Slots::value_type const& value) {
                        if (value.second->state() == Slot::active)
                            slots.emplace_back(value.second);
                    });
                std::shuffle(slots.begin(), slots.end(), default_prng());

                // build target vector
                targets.reserve(slots.size());
                std::for_each(
                    slots.cbegin(),
                    slots.cend(),
                    [&targets](SlotImp::ptr const& slot) {
                        targets.emplace_back(slot);
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
            if (config_.wantIncoming && counts_.inboundSlots() > 0)
            {
                Endpoint ep;
                ep.hops = 0;
                // we use the unspecified (0) address here because the value is
                // irrelevant to recipients. When peers receive an endpoint
                // with 0 hops, they use the socket remote_addr instead of the
                // value in the message. Furthermore, since the address value
                // is ignored, the type/version (ipv4 vs ipv6) doesn't matter
                // either. ipv6 has a slightly more compact string
                // representation of 0, so use that for self entries.
                ep.address = beast::IP::Endpoint(beast::IP::AddressV6())
                                 .at_port(config_.listeningPort);
                for (auto& t : targets)
                    t.insert(ep);
            }

            // build sequence of endpoints by hops
            livecache_.hops.shuffle();
            handout(
                targets.begin(),
                targets.end(),
                livecache_.hops.begin(),
                livecache_.hops.end());

            // broadcast
            for (auto const& t : targets)
            {
                SlotImp::ptr const& slot = t.slot();
                auto const& list = t.list();
                JLOG(m_journal.trace())
                    << beast::leftw(18) << "Logic sending "
                    << slot->remote_endpoint() << " with " << list.size()
                    << ((list.size() == 1) ? " endpoint" : " endpoints");
                result.push_back(std::make_pair(slot, list));
            }

            m_whenBroadcast = now + Tuning::secondsPerMessage;
        }

        return result;
    }

    void
    once_per_second()
    {
        std::lock_guard _(lock_);

        // Expire the Livecache
        livecache_.expire();

        // Expire the recent cache in each slot
        for (auto const& entry : slots_)
            entry.second->expire();

        // Expire the recent attempts table
        beast::expire(m_squelches, Tuning::recentAttemptDuration);

        bootcache_.periodicActivity();
    }

    //--------------------------------------------------------------------------

    // Validate and clean up the list that we received from the slot.
    void
    preprocess(SlotImp::ptr const& slot, Endpoints& list)
    {
        bool neighbor(false);
        for (auto iter = list.begin(); iter != list.end();)
        {
            Endpoint& ep(*iter);

            // Enforce hop limit
            if (ep.hops > Tuning::maxHops)
            {
                JLOG(m_journal.debug())
                    << beast::leftw(18) << "Endpoints drop " << ep.address
                    << " for excess hops " << ep.hops;
                iter = list.erase(iter);
                continue;
            }

            // See if we are directly connected
            if (ep.hops == 0)
            {
                if (!neighbor)
                {
                    // Fill in our neighbors remote address
                    neighbor = true;
                    ep.address =
                        slot->remote_endpoint().at_port(ep.address.port());
                }
                else
                {
                    JLOG(m_journal.debug())
                        << beast::leftw(18) << "Endpoints drop " << ep.address
                        << " for extra self";
                    iter = list.erase(iter);
                    continue;
                }
            }

            // Discard invalid addresses
            if (!is_valid_address(ep.address))
            {
                JLOG(m_journal.debug()) << beast::leftw(18) << "Endpoints drop "
                                        << ep.address << " as invalid";
                iter = list.erase(iter);
                continue;
            }

            // Filter duplicates
            if (std::any_of(
                    list.begin(),
                    iter,
                    [ep](Endpoints::value_type const& other) {
                        return ep.address == other.address;
                    }))
            {
                JLOG(m_journal.debug()) << beast::leftw(18) << "Endpoints drop "
                                        << ep.address << " as duplicate";
                iter = list.erase(iter);
                continue;
            }

            // Increment hop count on the incoming message, so
            // we store it at the hop count we will send it at.
            //
            ++ep.hops;

            ++iter;
        }
    }

    void
    on_endpoints(SlotImp::ptr const& slot, Endpoints list)
    {
        // If we're sent too many endpoints, sample them at random:
        if (list.size() > Tuning::numberOfEndpointsMax)
        {
            std::shuffle(list.begin(), list.end(), default_prng());
            list.resize(Tuning::numberOfEndpointsMax);
        }

        JLOG(m_journal.trace())
            << beast::leftw(18) << "Endpoints from " << slot->remote_endpoint()
            << " contained " << list.size()
            << ((list.size() > 1) ? " entries" : " entry");

        std::lock_guard _(lock_);

        // The object must exist in our table
        ASSERT(
            slots_.find(slot->remote_endpoint()) != slots_.end(),
            "ripple::PeerFinder::Logic::on_endpoints : valid slot input");

        // Must be handshaked!
        ASSERT(
            slot->state() == Slot::active,
            "ripple::PeerFinder::Logic::on_endpoints : valid slot state");

        clock_type::time_point const now(m_clock.now());

        // Limit how often we accept new endpoints
        if (slot->whenAcceptEndpoints > now)
            return;

        preprocess(slot, list);

        for (auto const& ep : list)
        {
            ASSERT(
                ep.hops != 0,
                "ripple::PeerFinder::Logic::on_endpoints : nonzero hops");

            slot->recent.insert(ep.address, ep.hops);

            // Note hops has been incremented, so 1
            // means a directly connected neighbor.
            //
            if (ep.hops == 1)
            {
                if (slot->connectivityCheckInProgress)
                {
                    JLOG(m_journal.debug())
                        << beast::leftw(18) << "Logic testing " << ep.address
                        << " already in progress";
                    continue;
                }

                if (!slot->checked)
                {
                    // Mark that a check for this slot is now in progress.
                    slot->connectivityCheckInProgress = true;

                    // Test the slot's listening port before
                    // adding it to the livecache for the first time.
                    //
                    m_checker.async_connect(
                        ep.address,
                        std::bind(
                            &Logic::checkComplete,
                            this,
                            slot->remote_endpoint(),
                            ep.address,
                            std::placeholders::_1));

                    // Note that we simply discard the first Endpoint
                    // that the neighbor sends when we perform the
                    // listening test. They will just send us another
                    // one in a few seconds.

                    continue;
                }

                // If they failed the test then skip the address
                if (!slot->canAccept)
                    continue;
            }

            // We only add to the livecache if the neighbor passed the
            // listening test, else we silently drop neighbor endpoint
            // since their listening port is misconfigured.
            //
            livecache_.insert(ep);
            bootcache_.insert(ep.address);
        }

        slot->whenAcceptEndpoints = now + Tuning::secondsPerMessage;
    }

    //--------------------------------------------------------------------------

    void
    remove(SlotImp::ptr const& slot)
    {
        {
            auto const iter = slots_.find(slot->remote_endpoint());
            // The slot must exist in the table
            if (iter == slots_.end())
                LogicError(
                    "PeerFinder::Logic::remove(): remote_endpoint "
                    "missing from slots_");

            // Remove from slot by IP table
            slots_.erase(iter);
        }
        // Remove the key if present
        if (slot->public_key() != std::nullopt)
        {
            auto const iter = keys_.find(*slot->public_key());
            // Key must exist
            if (iter == keys_.end())
                LogicError(
                    "PeerFinder::Logic::remove(): public_key missing "
                    "from keys_");

            keys_.erase(iter);
        }
        // Remove from connected address table
        {
            auto const iter(
                connectedAddresses_.find(slot->remote_endpoint().address()));
            // Address must exist
            if (iter == connectedAddresses_.end())
                LogicError(
                    "PeerFinder::Logic::remove(): remote_endpont "
                    "address missing from connectedAddresses_");

            connectedAddresses_.erase(iter);
        }

        // Update counts
        counts_.remove(*slot);
    }

    void
    on_closed(SlotImp::ptr const& slot)
    {
        std::lock_guard _(lock_);

        remove(slot);

        // Mark fixed slot failure
        if (slot->fixed() && !slot->inbound() && slot->state() != Slot::active)
        {
            auto iter(fixed_.find(slot->remote_endpoint()));
            if (iter == fixed_.end())
                LogicError(
                    "PeerFinder::Logic::on_closed(): remote_endpont "
                    "missing from fixed_");

            iter->second.failure(m_clock.now());
            JLOG(m_journal.debug()) << beast::leftw(18) << "Logic fixed "
                                    << slot->remote_endpoint() << " failed";
        }

        // Do state specific bookkeeping
        switch (slot->state())
        {
            case Slot::accept:
                JLOG(m_journal.trace()) << beast::leftw(18) << "Logic accept "
                                        << slot->remote_endpoint() << " failed";
                break;

            case Slot::connect:
            case Slot::connected:
                bootcache_.on_failure(slot->remote_endpoint());
                // VFALCO TODO If the address exists in the ephemeral/live
                //             endpoint livecache then we should mark the
                //             failure
                // as if it didn't pass the listening test. We should also
                // avoid propagating the address.
                break;

            case Slot::active:
                JLOG(m_journal.trace()) << beast::leftw(18) << "Logic close "
                                        << slot->remote_endpoint();
                break;

            case Slot::closing:
                JLOG(m_journal.trace()) << beast::leftw(18) << "Logic finished "
                                        << slot->remote_endpoint();
                break;

            default:
                UNREACHABLE(
                    "ripple::PeerFinder::Logic::on_closed : invalid slot "
                    "state");
                break;
        }
    }

    void
    on_failure(SlotImp::ptr const& slot)
    {
        std::lock_guard _(lock_);

        bootcache_.on_failure(slot->remote_endpoint());
    }

    // Insert a set of redirect IP addresses into the Bootcache
    template <class FwdIter>
    void
    onRedirects(
        FwdIter first,
        FwdIter last,
        boost::asio::ip::tcp::endpoint const& remote_address);

    //--------------------------------------------------------------------------

    // Returns `true` if the address matches a fixed slot address
    // Must have the lock held
    bool
    fixed(beast::IP::Endpoint const& endpoint) const
    {
        for (auto const& entry : fixed_)
            if (entry.first == endpoint)
                return true;
        return false;
    }

    // Returns `true` if the address matches a fixed slot address
    // Note that this does not use the port information in the IP::Endpoint
    // Must have the lock held
    bool
    fixed(beast::IP::Address const& address) const
    {
        for (auto const& entry : fixed_)
            if (entry.first.address() == address)
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
    void
    get_fixed(
        std::size_t needed,
        Container& c,
        typename ConnectHandouts::Squelches& squelches)
    {
        auto const now(m_clock.now());
        for (auto iter = fixed_.begin(); needed && iter != fixed_.end(); ++iter)
        {
            auto const& address(iter->first.address());
            if (iter->second.when() <= now &&
                squelches.find(address) == squelches.end() &&
                std::none_of(
                    slots_.cbegin(),
                    slots_.cend(),
                    [address](Slots::value_type const& v) {
                        return address == v.first.address();
                    }))
            {
                squelches.insert(iter->first.address());
                c.push_back(iter->first);
                --needed;
            }
        }
    }

    //--------------------------------------------------------------------------

    void
    addStaticSource(std::shared_ptr<Source> const& source)
    {
        fetch(source);
    }

    void
    addSource(std::shared_ptr<Source> const& source)
    {
        m_sources.push_back(source);
    }

    //--------------------------------------------------------------------------
    //
    // Bootcache livecache sources
    //
    //--------------------------------------------------------------------------

    // Add a set of addresses.
    // Returns the number of addresses added.
    //
    int
    addBootcacheAddresses(IPAddresses const& list)
    {
        int count(0);
        std::lock_guard _(lock_);
        for (auto addr : list)
        {
            if (bootcache_.insertStatic(addr))
                ++count;
        }
        return count;
    }

    // Fetch bootcache addresses from the specified source.
    void
    fetch(std::shared_ptr<Source> const& source)
    {
        Source::Results results;

        {
            {
                std::lock_guard _(lock_);
                if (stopping_)
                    return;
                fetchSource_ = source;
            }

            // VFALCO NOTE The fetch is synchronous,
            //             not sure if that's a good thing.
            //
            source->fetch(results, m_journal);

            {
                std::lock_guard _(lock_);
                if (stopping_)
                    return;
                fetchSource_ = nullptr;
            }
        }

        if (!results.error)
        {
            int const count(addBootcacheAddresses(results.addresses));
            JLOG(m_journal.info())
                << beast::leftw(18) << "Logic added " << count << " new "
                << ((count == 1) ? "address" : "addresses") << " from "
                << source->name();
        }
        else
        {
            JLOG(m_journal.error())
                << beast::leftw(18) << "Logic failed " << "'" << source->name()
                << "' fetch, " << results.error.message();
        }
    }

    //--------------------------------------------------------------------------
    //
    // Endpoint message handling
    //
    //--------------------------------------------------------------------------

    // Returns true if the IP::Endpoint contains no invalid data.
    bool
    is_valid_address(beast::IP::Endpoint const& address)
    {
        if (is_unspecified(address))
            return false;
        if (!is_public(address))
            return false;
        if (address.port() == 0)
            return false;
        return true;
    }

    //--------------------------------------------------------------------------
    //
    // PropertyStream
    //
    //--------------------------------------------------------------------------

    void
    writeSlots(beast::PropertyStream::Set& set, Slots const& slots)
    {
        for (auto const& entry : slots)
        {
            beast::PropertyStream::Map item(set);
            SlotImp const& slot(*entry.second);
            if (slot.local_endpoint() != std::nullopt)
                item["local_address"] = to_string(*slot.local_endpoint());
            item["remote_address"] = to_string(slot.remote_endpoint());
            if (slot.inbound())
                item["inbound"] = "yes";
            if (slot.fixed())
                item["fixed"] = "yes";
            if (slot.reserved())
                item["reserved"] = "yes";

            item["state"] = stateString(slot.state());
        }
    }

    void
    onWrite(beast::PropertyStream::Map& map)
    {
        std::lock_guard _(lock_);

        // VFALCO NOTE These ugly casts are needed because
        //             of how std::size_t is declared on some linuxes
        //
        map["bootcache"] = std::uint32_t(bootcache_.size());
        map["fixed"] = std::uint32_t(fixed_.size());

        {
            beast::PropertyStream::Set child("peers", map);
            writeSlots(child, slots_);
        }

        {
            beast::PropertyStream::Map child("counts", map);
            counts_.onWrite(child);
        }

        {
            beast::PropertyStream::Map child("config", map);
            config_.onWrite(child);
        }

        {
            beast::PropertyStream::Map child("livecache", map);
            livecache_.onWrite(child);
        }

        {
            beast::PropertyStream::Map child("bootcache", map);
            bootcache_.onWrite(child);
        }
    }

    //--------------------------------------------------------------------------
    //
    // Diagnostics
    //
    //--------------------------------------------------------------------------

    Counts const&
    counts() const
    {
        return counts_;
    }

    static std::string
    stateString(Slot::State state)
    {
        switch (state)
        {
            case Slot::accept:
                return "accept";
            case Slot::connect:
                return "connect";
            case Slot::connected:
                return "connected";
            case Slot::active:
                return "active";
            case Slot::closing:
                return "closing";
            default:
                break;
        };
        return "?";
    }
};

//------------------------------------------------------------------------------

template <class Checker>
template <class FwdIter>
void
Logic<Checker>::onRedirects(
    FwdIter first,
    FwdIter last,
    boost::asio::ip::tcp::endpoint const& remote_address)
{
    std::lock_guard _(lock_);
    std::size_t n = 0;
    for (; first != last && n < Tuning::maxRedirects; ++first, ++n)
        bootcache_.insert(beast::IPAddressConversion::from_asio(*first));
    if (n > 0)
    {
        JLOG(m_journal.trace()) << beast::leftw(18) << "Logic add " << n
                                << " redirect IPs from " << remote_address;
    }
}

}  // namespace PeerFinder
}  // namespace ripple

#endif
