#pragma once

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
#include <xrpl/beast/net/IPAddressConversion.h>
#include <xrpl/beast/utility/WrappedSink.h>

#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <set>

namespace xrpl::PeerFinder {

/** The Logic for maintaining the list of Slot addresses.
    We keep this in a separate class so it can be instantiated
    for unit tests.
*/
template <class Checker>
class Logic
{
public:
    // Maps remote endpoints to slots. Since a slot has a
    // remote endpoint upon construction, this holds all counts_.
    //
    using Slots = std::map<beast::IP::Endpoint, std::shared_ptr<SlotImp>>;

    beast::Journal journal;
    clock_type& clock;
    Store& store;
    Checker& checker;

    std::recursive_mutex lock;

    // True if we are stopping.
    bool stopping = false;

    // The source we are currently fetching.
    // This is used to cancel I/O during program exit.
    std::shared_ptr<Source> fetchSource;

private:
    // Configuration settings
    Config config_;

    // Slot counts and other aggregate statistics.
    Counts counts_;

    // A list of slots that should always be connected
    std::map<beast::IP::Endpoint, Fixed> fixed_;

public:
    // Live livecache from mtENDPOINTS messages
    Livecache<> livecache;

    // LiveCache of addresses suitable for gaining initial connections
    Bootcache bootcache;

    // Holds all counts
    Slots slots;

    // The addresses (but not port) we are connected to. This includes
    // outgoing connection attempts. Note that this set can contain
    // duplicates (since the port is not set)
    std::multiset<beast::IP::Address> connectedAddresses;

    // Set of public keys belonging to active peers
    std::set<PublicKey> keys;

    // A list of dynamic sources to consult as a fallback
    std::vector<std::shared_ptr<Source>> sources;

    clock_type::time_point whenBroadcast;

    ConnectHandouts::Squelches squelches;

    //--------------------------------------------------------------------------
public:
    Logic(clock_type& clock, Store& store, Checker& checker, beast::Journal journal)
        : journal(journal)
        , clock(clock)
        , store(store)
        , checker(checker)
        , livecache(clock, journal)
        , bootcache(store, clock, journal)
        , whenBroadcast(clock.now())
        , squelches(clock)
    {
        config({});
    }

    // Load persistent state information from the Store
    //
    void
    load()
    {
        std::scoped_lock const _(lock);
        bootcache.load();
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
        std::scoped_lock const _(lock);
        stopping = true;
        if (fetchSource != nullptr)
            fetchSource->cancel();
    }

    //--------------------------------------------------------------------------
    //
    // Manager
    //
    //--------------------------------------------------------------------------

    void
    config(Config const& c)
    {
        std::scoped_lock const _(lock);
        config_ = c;
        counts_.onConfig(config_);
    }

    Config
    config()
    {
        std::scoped_lock const _(lock);
        return config_;
    }

    void
    addFixedPeer(std::string const& name, beast::IP::Endpoint const& ep)
    {
        addFixedPeer(name, std::vector<beast::IP::Endpoint>{ep});
    }

    void
    addFixedPeer(std::string const& name, std::vector<beast::IP::Endpoint> const& addresses)
    {
        std::scoped_lock const _(lock);

        if (addresses.empty())
        {
            JLOG(journal.info()) << "Could not resolve fixed slot '" << name << "'";
            return;
        }

        for (auto const& remoteAddress : addresses)
        {
            if (remoteAddress.port() == 0)
            {
                Throw<std::runtime_error>(
                    "Port not specified for address:" + remoteAddress.toString());
            }

            auto result(fixed_.emplace(
                std::piecewise_construct,
                std::forward_as_tuple(remoteAddress),
                std::make_tuple(std::ref(clock))));

            if (result.second)
            {
                JLOG(journal.debug())
                    << beast::Leftw(18) << "Logic add fixed '" << name << "' at " << remoteAddress;
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

        std::scoped_lock const _(lock);
        auto const iter(slots.find(remoteAddress));
        if (iter == slots.end())
        {
            // The slot disconnected before we finished the check
            JLOG(journal.debug()) << beast::Leftw(18) << "Logic tested " << checkedAddress
                                  << " but the connection was closed";
            return;
        }

        SlotImp& slot(*iter->second);
        slot.checked = true;
        slot.connectivityCheckInProgress = false;

        beast::WrappedSink sink{journal.sink(), slot.prefix()};
        beast::Journal const journal{sink};

        if (ec)
        {
            // VFALCO TODO Should we retry depending on the error?
            slot.canAccept = false;
            JLOG(journal.error()) << "Logic testing " << iter->first << " with error, "
                                  << ec.message();
            bootcache.onFailure(checkedAddress);
            return;
        }

        slot.canAccept = true;
        slot.setListeningPort(checkedAddress.port());
        JLOG(journal.debug()) << "Logic testing " << checkedAddress << " succeeded";
    }

    //--------------------------------------------------------------------------

    std::pair<SlotImp::ptr, Result>
    newInboundSlot(
        beast::IP::Endpoint const& localEndpoint,
        beast::IP::Endpoint const& remoteEndpoint)
    {
        JLOG(journal.debug()) << beast::Leftw(18) << "Logic accept" << remoteEndpoint
                              << " on local " << localEndpoint;

        std::scoped_lock const _(lock);

        // Check for connection limit per address
        if (isPublic(remoteEndpoint))
        {
            auto const count = connectedAddresses.count(remoteEndpoint.address());
            if (count + 1 > config_.ipLimit)
            {
                JLOG(journal.debug()) << beast::Leftw(18) << "Logic dropping inbound "
                                      << remoteEndpoint << " because of ip limits.";
                return {SlotImp::ptr(), Result::IpLimitExceeded};
            }
        }

        // Check for duplicate connection
        if (slots.contains(remoteEndpoint))
        {
            JLOG(journal.debug()) << beast::Leftw(18) << "Logic dropping " << remoteEndpoint
                                  << " as duplicate incoming";
            return {SlotImp::ptr(), Result::DuplicatePeer};
        }

        // Create the slot
        SlotImp::ptr const slot(
            std::make_shared<SlotImp>(
                localEndpoint, remoteEndpoint, fixed(remoteEndpoint.address()), clock));
        // Add slot to table
        auto const result(slots.emplace(slot->remoteEndpoint(), slot));
        // Remote address must not already exist
        XRPL_ASSERT(
            result.second,
            "xrpl::PeerFinder::Logic::new_inbound_slot : remote endpoint "
            "inserted");
        // Add to the connected address list
        connectedAddresses.emplace(remoteEndpoint.address());

        // Update counts
        counts_.add(*slot);

        return {result.first->second, Result::Success};
    }

    // Can't check for self-connect because we don't know the local endpoint
    std::pair<SlotImp::ptr, Result>
    newOutboundSlot(beast::IP::Endpoint const& remoteEndpoint)
    {
        JLOG(journal.debug()) << beast::Leftw(18) << "Logic connect " << remoteEndpoint;

        std::scoped_lock const _(lock);

        // Check for duplicate connection
        if (slots.contains(remoteEndpoint))
        {
            JLOG(journal.debug()) << beast::Leftw(18) << "Logic dropping " << remoteEndpoint
                                  << " as duplicate connect";
            return {SlotImp::ptr(), Result::DuplicatePeer};
        }

        // Create the slot
        SlotImp::ptr const slot(
            std::make_shared<SlotImp>(remoteEndpoint, fixed(remoteEndpoint), clock));

        // Add slot to table
        auto const result = slots.emplace(slot->remoteEndpoint(), slot);
        // Remote address must not already exist
        XRPL_ASSERT(
            result.second,
            "xrpl::PeerFinder::Logic::new_outbound_slot : remote endpoint "
            "inserted");

        // Add to the connected address list
        connectedAddresses.emplace(remoteEndpoint.address());

        // Update counts
        counts_.add(*slot);

        return {result.first->second, Result::Success};
    }

    bool
    onConnected(SlotImp::ptr const& slot, beast::IP::Endpoint const& localEndpoint)
    {
        beast::WrappedSink sink{journal.sink(), slot->prefix()};
        beast::Journal const journal{sink};

        JLOG(journal.trace()) << "Logic connected on local " << localEndpoint;

        std::scoped_lock const _(lock);

        // The object must exist in our table
        XRPL_ASSERT(
            slots.contains(slot->remoteEndpoint()),
            "xrpl::PeerFinder::Logic::onConnected : valid slot input");
        // Assign the local endpoint now that it's known
        slot->localEndpoint(localEndpoint);

        // Check for self-connect by address
        {
            auto const iter(slots.find(localEndpoint));
            if (iter != slots.end())
            {
                XRPL_ASSERT(
                    iter->second->localEndpoint() == slot->remoteEndpoint(),
                    "xrpl::PeerFinder::Logic::onConnected : local and remote "
                    "endpoints do match");
                JLOG(journal.warn()) << "Logic dropping as self connect";
                return false;
            }
        }

        // Update counts
        counts_.remove(*slot);
        slot->state(Slot::State::Connected);
        counts_.add(*slot);
        return true;
    }

    Result
    activate(SlotImp::ptr const& slot, PublicKey const& key, bool reserved)
    {
        beast::WrappedSink sink{journal.sink(), slot->prefix()};
        beast::Journal const journal{sink};

        JLOG(journal.debug()) << "Logic handshake " << slot->remoteEndpoint() << " with "
                              << (reserved ? "reserved " : "") << "key " << key;

        std::scoped_lock const _(lock);

        // The object must exist in our table
        XRPL_ASSERT(
            slots.contains(slot->remoteEndpoint()),
            "xrpl::PeerFinder::Logic::activate : valid slot input");
        // Must be accepted or connected
        XRPL_ASSERT(
            slot->state() == Slot::State::Accept || slot->state() == Slot::State::Connected,
            "xrpl::PeerFinder::Logic::activate : valid slot state");

        // Check for duplicate connection by key
        if (keys.contains(key))
            return Result::DuplicatePeer;

        // If the peer belongs to a cluster or is reserved,
        // update the slot to reflect that.
        counts_.remove(*slot);
        slot->reserved(reserved);
        counts_.add(*slot);

        // See if we have an open space for this slot
        if (!counts_.canActivate(*slot))
        {
            if (!slot->inbound())
                bootcache.onSuccess(slot->remoteEndpoint());
            if (slot->inbound() && counts_.inMax() == 0)
                return Result::InboundDisabled;
            return Result::Full;
        }

        // Set the key right before adding to the map, otherwise we might
        // assert later when erasing the key.
        slot->publicKey(key);
        {
            [[maybe_unused]] bool const inserted = keys.insert(key).second;
            // Public key must not already exist
            XRPL_ASSERT(inserted, "xrpl::PeerFinder::Logic::activate : public key inserted");
        }

        // Change state and update counts
        counts_.remove(*slot);
        slot->activate(clock.now());
        counts_.add(*slot);

        if (!slot->inbound())
            bootcache.onSuccess(slot->remoteEndpoint());

        // Mark fixed slot success
        if (slot->fixed() && !slot->inbound())
        {
            auto iter(fixed_.find(slot->remoteEndpoint()));
            if (iter == fixed_.end())
            {
                logicError(
                    "PeerFinder::Logic::activate(): remote_endpoint "
                    "missing from fixed_");
            }

            iter->second.success(clock.now());
            JLOG(journal.trace()) << "Logic fixed success";
        }

        return Result::Success;
    }

    /** Return a list of addresses suitable for redirection.
        This is a legacy function, redirects should be returned in
        the HTTP handshake and not via TMEndpoints.
    */
    std::vector<Endpoint>
    redirect(SlotImp::ptr const& slot)
    {
        std::scoped_lock const _(lock);
        RedirectHandouts h(slot);
        livecache.hops.shuffle();
        handout(&h, (&h) + 1, livecache.hops.begin(), livecache.hops.end());
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
        std::vector<beast::IP::Endpoint> none;

        std::scoped_lock const _(lock);

        // Count how many more outbound attempts to make
        //
        auto needed(counts_.attemptsNeeded());
        if (needed == 0)
            return none;

        ConnectHandouts h(needed, squelches);

        // Make sure we don't connect to already-connected entries.
        for (auto const& s : slots)
        {
            auto const result(squelches.insert(s.second->remoteEndpoint().address()));
            if (!result.second)
                squelches.touch(result.first);
        }

        // 1. Use Fixed if:
        //    Fixed active count is below fixed count AND
        //      ( There are eligible fixed addresses to try OR
        //        Any outbound attempts are in progress)
        //
        if (counts_.fixedActive() < fixed_.size())
        {
            getFixed(needed, h.list(), squelches);

            if (!h.list().empty())
            {
                JLOG(journal.debug())
                    << beast::Leftw(18) << "Logic connect " << h.list().size() << " fixed";
                return h.list();
            }

            if (counts_.attempts() > 0)
            {
                JLOG(journal.debug())
                    << beast::Leftw(18) << "Logic waiting on " << counts_.attempts() << " attempts";
                return none;
            }
        }

        // Only proceed if auto connect is enabled and we
        // have less than the desired number of outbound slots
        //
        if (!config_.autoConnect || counts_.outActive() >= counts_.outMax())
            return none;

        // 2. Use Livecache if:
        //    There are any entries in the cache OR
        //    Any outbound attempts are in progress
        //
        {
            livecache.hops.shuffle();
            handout(&h, (&h) + 1, livecache.hops.rbegin(), livecache.hops.rend());
            if (!h.list().empty())
            {
                JLOG(journal.debug())
                    << beast::Leftw(18) << "Logic connect " << h.list().size() << " live "
                    << ((h.list().size() > 1) ? "endpoints" : "endpoint");
                return h.list();
            }
            if (counts_.attempts() > 0)
            {
                JLOG(journal.debug())
                    << beast::Leftw(18) << "Logic waiting on " << counts_.attempts() << " attempts";
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
        for (auto iter(bootcache.begin()); !h.full() && iter != bootcache.end(); ++iter)
            h.tryInsert(*iter);

        if (!h.list().empty())
        {
            JLOG(journal.debug()) << beast::Leftw(18) << "Logic connect " << h.list().size()
                                  << " boot " << ((h.list().size() > 1) ? "addresses" : "address");
            return h.list();
        }

        // If we get here we are stuck
        return none;
    }

    std::vector<std::pair<std::shared_ptr<Slot>, std::vector<Endpoint>>>
    buildEndpointsForPeers()
    {
        std::vector<std::pair<std::shared_ptr<Slot>, std::vector<Endpoint>>> result;

        std::scoped_lock const _(lock);

        clock_type::time_point const now = clock.now();
        if (whenBroadcast <= now)
        {
            std::vector<SlotHandouts> targets;

            {
                // build list of active slots
                std::vector<SlotImp::ptr> activeSlots;
                activeSlots.reserve(slots.size());
                std::for_each(
                    slots.cbegin(), slots.cend(), [&activeSlots](Slots::value_type const& value) {
                        if (value.second->state() == Slot::State::Active)
                            activeSlots.emplace_back(value.second);
                    });
                std::shuffle(activeSlots.begin(), activeSlots.end(), defaultPrng());

                // build target vector
                targets.reserve(activeSlots.size());
                std::for_each(
                    activeSlots.cbegin(), activeSlots.cend(), [&targets](SlotImp::ptr const& slot) {
                        targets.emplace_back(slot);
                    });
            }

            /* VFALCO NOTE
                This is a temporary measure. Once we know our own IP
                address, the correct solution is to put it into the Livecache
                at hops 0, and go through the regular handout path. This way
                we avoid handing our address out too frequently, which this code
                suffers from.
            */
            // Add an entry for ourselves if:
            // 1. We want incoming
            // 2. We have slots
            // 3. We haven't failed the firewalled test
            //
            if (config_.wantIncoming && counts_.inMax() > 0)
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
                ep.address =
                    beast::IP::Endpoint(beast::IP::AddressV6()).atPort(config_.listeningPort);
                for (auto& t : targets)
                    t.insert(ep);
            }

            // build sequence of endpoints by hops
            livecache.hops.shuffle();
            handout(targets.begin(), targets.end(), livecache.hops.begin(), livecache.hops.end());

            // broadcast
            for (auto const& t : targets)
            {
                SlotImp::ptr const& slot = t.slot();
                auto const& list = t.list();
                beast::WrappedSink sink{journal.sink(), slot->prefix()};
                beast::Journal const journal{sink};
                JLOG(journal.trace()) << "Logic sending " << list.size()
                                      << ((list.size() == 1) ? " endpoint" : " endpoints");
                result.emplace_back(slot, list);
            }

            whenBroadcast = now + Tuning::kSECONDS_PER_MESSAGE;
        }

        return result;
    }

    void
    oncePerSecond()
    {
        std::scoped_lock const _(lock);

        // Expire the Livecache
        livecache.expire();

        // Expire the recent cache in each slot
        for (auto const& entry : slots)
            entry.second->expire();

        // Expire the recent attempts table
        beast::expire(squelches, Tuning::kRECENT_ATTEMPT_DURATION);

        bootcache.periodicActivity();
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
            if (ep.hops > Tuning::kMAX_HOPS)
            {
                JLOG(journal.debug()) << beast::Leftw(18) << "Endpoints drop " << ep.address
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
                    ep.address = slot->remoteEndpoint().atPort(ep.address.port());
                }
                else
                {
                    JLOG(journal.debug())
                        << beast::Leftw(18) << "Endpoints drop " << ep.address << " for extra self";
                    iter = list.erase(iter);
                    continue;
                }
            }

            // Discard invalid addresses
            if (!isValidAddress(ep.address))
            {
                JLOG(journal.debug())
                    << beast::Leftw(18) << "Endpoints drop " << ep.address << " as invalid";
                iter = list.erase(iter);
                continue;
            }

            // Filter duplicates
            if (std::any_of(list.begin(), iter, [ep](Endpoints::value_type const& other) {
                    return ep.address == other.address;
                }))
            {
                JLOG(journal.debug())
                    << beast::Leftw(18) << "Endpoints drop " << ep.address << " as duplicate";
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
    onEndpoints(SlotImp::ptr const& slot, Endpoints list)
    {
        beast::WrappedSink sink{journal.sink(), slot->prefix()};
        beast::Journal const journal{sink};

        // If we're sent too many endpoints, sample them at random:
        if (list.size() > Tuning::kNUMBER_OF_ENDPOINTS_MAX)
        {
            std::shuffle(list.begin(), list.end(), defaultPrng());
            list.resize(Tuning::kNUMBER_OF_ENDPOINTS_MAX);
        }

        JLOG(journal.trace()) << "Endpoints contained " << list.size()
                              << ((list.size() > 1) ? " entries" : " entry");

        std::scoped_lock const _(lock);

        // The object must exist in our table
        XRPL_ASSERT(
            slots.contains(slot->remoteEndpoint()),
            "xrpl::PeerFinder::Logic::onEndpoints : valid slot input");

        // Must be handshaked!
        XRPL_ASSERT(
            slot->state() == Slot::State::Active,
            "xrpl::PeerFinder::Logic::onEndpoints : valid slot state");

        clock_type::time_point const now(clock.now());

        // Limit how often we accept new endpoints
        if (slot->whenAcceptEndpoints > now)
            return;

        preprocess(slot, list);

        for (auto const& ep : list)
        {
            XRPL_ASSERT(ep.hops, "xrpl::PeerFinder::Logic::onEndpoints : nonzero hops");

            slot->recent.insert(ep.address, ep.hops);

            // Note hops has been incremented, so 1
            // means a directly connected neighbor.
            //
            if (ep.hops == 1)
            {
                if (slot->connectivityCheckInProgress)
                {
                    JLOG(journal.debug())
                        << "Logic testing " << ep.address << " already in progress";
                    continue;
                }

                if (!slot->checked)
                {
                    // Mark that a check for this slot is now in progress.
                    slot->connectivityCheckInProgress = true;

                    // Test the slot's listening port before
                    // adding it to the livecache for the first time.
                    //
                    checker.asyncConnect(
                        ep.address,
                        std::bind(
                            &Logic::checkComplete,
                            this,
                            slot->remoteEndpoint(),
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
            livecache.insert(ep);
            bootcache.insert(ep.address);
        }

        slot->whenAcceptEndpoints = now + Tuning::kSECONDS_PER_MESSAGE;
    }

    //--------------------------------------------------------------------------

    void
    remove(SlotImp::ptr const& slot)
    {
        {
            auto const iter = slots.find(slot->remoteEndpoint());
            // The slot must exist in the table
            if (iter == slots.end())
            {
                logicError(
                    "PeerFinder::Logic::remove(): remote_endpoint "
                    "missing from slots_");
            }

            // Remove from slot by IP table
            slots.erase(iter);
        }
        // Remove the key if present
        if (slot->publicKey() != std::nullopt)
        {
            auto const iter = keys.find(*slot->publicKey());
            // Key must exist
            if (iter == keys.end())
            {
                logicError(
                    "PeerFinder::Logic::remove(): public_key missing "
                    "from keys_");
            }

            keys.erase(iter);
        }
        // Remove from connected address table
        {
            auto const iter(connectedAddresses.find(slot->remoteEndpoint().address()));
            // Address must exist
            if (iter == connectedAddresses.end())
            {
                logicError(
                    "PeerFinder::Logic::remove(): remote_endpoint "
                    "address missing from connectedAddresses_");
            }

            connectedAddresses.erase(iter);
        }

        // Update counts
        counts_.remove(*slot);
    }

    void
    onClosed(SlotImp::ptr const& slot)
    {
        std::scoped_lock const _(lock);

        remove(slot);

        beast::WrappedSink sink{journal.sink(), slot->prefix()};
        beast::Journal const journal{sink};

        // Mark fixed slot failure
        if (slot->fixed() && !slot->inbound() && slot->state() != Slot::State::Active)
        {
            auto iter(fixed_.find(slot->remoteEndpoint()));
            if (iter == fixed_.end())
            {
                logicError(
                    "PeerFinder::Logic::on_closed(): remote_endpoint "
                    "missing from fixed_");
            }

            iter->second.failure(clock.now());
            JLOG(journal.debug()) << "Logic fixed failed";
        }

        // Do state specific bookkeeping
        switch (slot->state())
        {
            case Slot::State::Accept:
                JLOG(journal.trace()) << "Logic accept failed";
                break;

            case Slot::State::Connect:
            case Slot::State::Connected:
                bootcache.onFailure(slot->remoteEndpoint());
                // VFALCO TODO If the address exists in the ephemeral/live
                //             endpoint livecache then we should mark the
                //             failure
                // as if it didn't pass the listening test. We should also
                // avoid propagating the address.
                break;

            case Slot::State::Active:
                JLOG(journal.trace()) << "Logic close";
                break;

            case Slot::State::Closing:
                JLOG(journal.trace()) << "Logic finished";
                break;

            // LCOV_EXCL_START
            default:
                UNREACHABLE(
                    "xrpl::PeerFinder::Logic::on_closed : invalid slot "
                    "state");
                break;
                // LCOV_EXCL_STOP
        }
    }

    void
    onFailure(SlotImp::ptr const& slot)
    {
        std::scoped_lock const _(lock);

        bootcache.onFailure(slot->remoteEndpoint());
    }

    // Insert a set of redirect IP addresses into the Bootcache
    template <class FwdIter>
    void
    onRedirects(FwdIter first, FwdIter last, boost::asio::ip::tcp::endpoint const& remoteAddress);

    //--------------------------------------------------------------------------

    // Returns `true` if the address matches a fixed slot address
    // Must have the lock held
    bool
    fixed(beast::IP::Endpoint const& endpoint) const
    {
        for (auto const& entry : fixed_)
        {
            if (entry.first == endpoint)
                return true;
        }
        return false;
    }

    // Returns `true` if the address matches a fixed slot address
    // Note that this does not use the port information in the IP::Endpoint
    // Must have the lock held
    bool
    fixed(beast::IP::Address const& address) const
    {
        for (auto const& entry : fixed_)
        {
            if (entry.first.address() == address)
                return true;
        }
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
    getFixed(std::size_t needed, Container& c, typename ConnectHandouts::Squelches& squelches)
    {
        auto const now(clock.now());
        for (auto iter = fixed_.begin(); needed && iter != fixed_.end(); ++iter)
        {
            auto const& address(iter->first.address());
            if (iter->second.when() <= now && squelches.find(address) == squelches.end() &&
                std::none_of(slots.cbegin(), slots.cend(), [address](Slots::value_type const& v) {
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
        sources.push_back(source);
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
        std::scoped_lock const _(lock);
        for (auto const& addr : list)
        {
            if (bootcache.insertStatic(addr))
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
                std::scoped_lock const _(lock);
                if (stopping)
                    return;
                fetchSource = source;
            }

            // VFALCO NOTE The fetch is synchronous,
            //             not sure if that's a good thing.
            //
            source->fetch(results, journal);

            {
                std::scoped_lock const _(lock);
                if (stopping)
                    return;
                fetchSource = nullptr;
            }
        }

        if (!results.error)
        {
            int const count(addBootcacheAddresses(results.addresses));
            JLOG(journal.info()) << beast::Leftw(18) << "Logic added " << count << " new "
                                 << ((count == 1) ? "address" : "addresses") << " from "
                                 << source->name();
        }
        else
        {
            JLOG(journal.error()) << beast::Leftw(18) << "Logic failed "
                                  << "'" << source->name() << "' fetch, "
                                  << results.error.message();
        }
    }

    //--------------------------------------------------------------------------
    //
    // Endpoint message handling
    //
    //--------------------------------------------------------------------------

    // Returns true if the IP::Endpoint contains no invalid data.
    bool
    isValidAddress(beast::IP::Endpoint const& address)
    {
        if (isUnspecified(address))
            return false;
        if (!isPublic(address))
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
            if (slot.localEndpoint() != std::nullopt)
                item["local_address"] = to_string(*slot.localEndpoint());
            item["remote_address"] = to_string(slot.remoteEndpoint());
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
        std::scoped_lock const _(lock);

        // VFALCO NOTE These ugly casts are needed because
        //             of how std::size_t is declared on some linuxes
        //
        map["bootcache"] = std::uint32_t(bootcache.size());
        map["fixed"] = std::uint32_t(fixed_.size());

        {
            beast::PropertyStream::Set child("peers", map);
            writeSlots(child, slots);
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
            livecache.onWrite(child);
        }

        {
            beast::PropertyStream::Map child("bootcache", map);
            bootcache.onWrite(child);
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
            case Slot::State::Accept:
                return "accept";
            case Slot::State::Connect:
                return "connect";
            case Slot::State::Connected:
                return "connected";
            case Slot::State::Active:
                return "active";
            case Slot::State::Closing:
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
    boost::asio::ip::tcp::endpoint const& remoteAddress)
{
    std::scoped_lock const _(lock);
    std::size_t n = 0;
    for (; first != last && n < Tuning::MaxRedirects; ++first, ++n)
        bootcache.insert(beast::IPAddressConversion::fromAsio(*first));
    if (n > 0)
    {
        JLOG(journal.trace()) << beast::Leftw(18) << "Logic add " << n << " redirect IPs from "
                              << remoteAddress;
    }
}

}  // namespace xrpl::PeerFinder
