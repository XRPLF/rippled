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

#ifndef RIPPLE_PEERFINDER_MANAGER_H_INCLUDED
#define RIPPLE_PEERFINDER_MANAGER_H_INCLUDED

#include <ripple/beast/clock/abstract_clock.h>
#include <ripple/beast/utility/PropertyStream.h>
#include <ripple/core/Config.h>
#include <ripple/peerfinder/Slot.h>
#include <boost/asio/ip/tcp.hpp>

namespace ripple {
namespace PeerFinder {

using clock_type = beast::abstract_clock<std::chrono::steady_clock>;

/** Represents a set of addresses. */
using IPAddresses = std::vector<beast::IP::Endpoint>;

//------------------------------------------------------------------------------

/** PeerFinder configuration settings. */
struct Config
{
    /** The largest number of public peer slots to allow.
        This includes both inbound and outbound, but does not include
        fixed peers.
    */
    std::size_t maxPeers;

    /** The number of automatic outbound connections to maintain.
        Outbound connections are only maintained if autoConnect
        is `true`.
    */
    std::size_t outPeers;

    /** The number of automatic inbound connections to maintain.
        Inbound connections are only maintained if wantIncoming
        is `true`.
    */
    std::size_t inPeers;

    /** `true` if we want our IP address kept private. */
    bool peerPrivate = true;

    /** `true` if we want to accept incoming connections. */
    bool wantIncoming;

    /** `true` if we want to establish connections automatically */
    bool autoConnect;

    /** The listening port number. */
    std::uint16_t listeningPort;

    /** The set of features we advertise. */
    std::string features;

    /** Limit how many incoming connections we allow per IP */
    int ipLimit;

    //--------------------------------------------------------------------------

    /** Create a configuration with default values. */
    Config();

    /** Returns a suitable value for outPeers according to the rules. */
    std::size_t
    calcOutPeers() const;

    /** Adjusts the values so they follow the business rules. */
    void
    applyTuning();

    /** Write the configuration into a property stream */
    void
    onWrite(beast::PropertyStream::Map& map);

    /** Make PeerFinder::Config from configuration parameters
     * @param config server's configuration
     * @param port server's listening port
     * @param validationPublicKey true if validation public key is not empty
     * @param ipLimit limit of incoming connections per IP
     * @return PeerFinder::Config
     */
    static Config
    makeConfig(
        ripple::Config const& config,
        std::uint16_t port,
        bool validationPublicKey,
        int ipLimit);
};

//------------------------------------------------------------------------------

/** Describes a connectible peer address along with some metadata. */
struct Endpoint
{
    Endpoint();

    Endpoint(beast::IP::Endpoint const& ep, int hops_);

    int hops;
    beast::IP::Endpoint address;
};

bool
operator<(Endpoint const& lhs, Endpoint const& rhs);

/** A set of Endpoint used for connecting. */
using Endpoints = std::vector<Endpoint>;

//------------------------------------------------------------------------------

/** Possible results from activating a slot. */
enum class Result { duplicate, full, success };

/** Maintains a set of IP addresses used for getting into the network. */
class Manager : public beast::PropertyStream::Source
{
protected:
    Manager() noexcept;

public:
    /** Destroy the object.
        Any pending source fetch operations are aborted.
        There may be some listener calls made before the
        destructor returns.
    */
    virtual ~Manager() = default;

    /** Set the configuration for the manager.
        The new settings will be applied asynchronously.
        Thread safety:
            Can be called from any threads at any time.
    */
    virtual void
    setConfig(Config const& config) = 0;

    /** Transition to the started state, synchronously. */
    virtual void
    start() = 0;

    /** Transition to the stopped state, synchronously. */
    virtual void
    stop() = 0;

    /** Returns the configuration for the manager. */
    virtual Config
    config() = 0;

    /** Add a peer that should always be connected.
        This is useful for maintaining a private cluster of peers.
        The string is the name as specified in the configuration
        file, along with the set of corresponding IP addresses.
    */
    virtual void
    addFixedPeer(
        std::string const& name,
        std::vector<beast::IP::Endpoint> const& addresses) = 0;

    /** Add a set of strings as fallback IP::Endpoint sources.
        @param name A label used for diagnostics.
    */
    virtual void
    addFallbackStrings(
        std::string const& name,
        std::vector<std::string> const& strings) = 0;

    /** Add a URL as a fallback location to obtain IP::Endpoint sources.
        @param name A label used for diagnostics.
    */
    /* VFALCO NOTE Unimplemented
    virtual void addFallbackURL (std::string const& name,
        std::string const& url) = 0;
    */

    //--------------------------------------------------------------------------

    /** Create a new inbound slot with the specified remote endpoint.
        If nullptr is returned, then the slot could not be assigned.
        Usually this is because of a detected self-connection.
    */
    virtual std::shared_ptr<Slot>
    new_inbound_slot(
        beast::IP::Endpoint const& local_endpoint,
        beast::IP::Endpoint const& remote_endpoint) = 0;

    /** Create a new outbound slot with the specified remote endpoint.
        If nullptr is returned, then the slot could not be assigned.
        Usually this is because of a duplicate connection.
    */
    virtual std::shared_ptr<Slot>
    new_outbound_slot(beast::IP::Endpoint const& remote_endpoint) = 0;

    /** Called when mtENDPOINTS is received. */
    virtual void
    on_endpoints(
        std::shared_ptr<Slot> const& slot,
        Endpoints const& endpoints) = 0;

    /** Called when the slot is closed.
        This always happens when the socket is closed, unless the socket
        was canceled.
    */
    virtual void
    on_closed(std::shared_ptr<Slot> const& slot) = 0;

    /** Called when an outbound connection is deemed to have failed */
    virtual void
    on_failure(std::shared_ptr<Slot> const& slot) = 0;

    /** Called when we received redirect IPs from a busy peer. */
    virtual void
    onRedirects(
        boost::asio::ip::tcp::endpoint const& remote_address,
        std::vector<boost::asio::ip::tcp::endpoint> const& eps) = 0;

    //--------------------------------------------------------------------------

    /** Called when an outbound connection attempt succeeds.
        The local endpoint must be valid. If the caller receives an error
        when retrieving the local endpoint from the socket, it should
        proceed as if the connection attempt failed by calling on_closed
        instead of on_connected.
        @return `true` if the connection should be kept
    */
    virtual bool
    onConnected(
        std::shared_ptr<Slot> const& slot,
        beast::IP::Endpoint const& local_endpoint) = 0;

    /** Request an active slot type. */
    virtual Result
    activate(
        std::shared_ptr<Slot> const& slot,
        PublicKey const& key,
        bool reserved) = 0;

    /** Returns a set of endpoints suitable for redirection. */
    virtual std::vector<Endpoint>
    redirect(std::shared_ptr<Slot> const& slot) = 0;

    /** Return a set of addresses we should connect to. */
    virtual std::vector<beast::IP::Endpoint>
    autoconnect() = 0;

    virtual std::vector<std::pair<std::shared_ptr<Slot>, std::vector<Endpoint>>>
    buildEndpointsForPeers() = 0;

    /** Perform periodic activity.
        This should be called once per second.
    */
    virtual void
    once_per_second() = 0;

    virtual std::size_t
    livecacheSize() const = 0;

    virtual std::size_t
    bootcacheSize() const = 0;
};

}  // namespace PeerFinder
}  // namespace ripple

#endif
