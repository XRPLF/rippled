#pragma once

#include <xrpl/beast/net/IPEndpoint.h>
#include <xrpl/beast/utility/PropertyStream.h>
#include <xrpl/peerfinder/Config.h>
#include <xrpl/peerfinder/Slot.h>
#include <xrpl/peerfinder/Types.h>
#include <xrpl/protocol/PublicKey.h>

#include <boost/asio/ip/tcp.hpp>

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace xrpl::PeerFinder {

/**
 * Maintains a set of IP addresses used for getting into the network.
 */
class Manager : public beast::PropertyStream::Source
{
protected:
    Manager() noexcept;

public:
    /**
     * Destroy the object.
     * Any pending source fetch operations are aborted.
     * There may be some listener calls made before the
     * destructor returns.
     */
    ~Manager() override = default;

    /**
     * Set the configuration for the manager.
     * The new settings will be applied asynchronously.
     * Thread safety:
     *     Can be called from any threads at any time.
     */
    virtual void
    setConfig(Config const& config) = 0;

    /**
     * Transition to the started state, synchronously.
     */
    virtual void
    start() = 0;

    /**
     * Transition to the stopped state, synchronously.
     */
    virtual void
    stop() = 0;

    /**
     * Returns the configuration for the manager.
     */
    virtual Config
    config() = 0;

    /**
     * Add a peer that should always be connected.
     * This is useful for maintaining a private cluster of peers.
     * The string is the name as specified in the configuration
     * file, along with the set of corresponding IP addresses.
     */
    virtual void
    addFixedPeer(std::string_view name, std::vector<beast::IP::Endpoint> const& addresses) = 0;

    /**
     * Add a set of strings as fallback IP::Endpoint sources.
     * @param name A label used for diagnostics.
     */
    virtual void
    addFallbackStrings(std::string const& name, std::vector<std::string> const& strings) = 0;

    /**
     * Add a URL as a fallback location to obtain IP::Endpoint sources.
     * @param name A label used for diagnostics.
     */
    /* VFALCO NOTE Unimplemented
    virtual void addFallbackURL (std::string const& name,
        std::string const& url) = 0;
    */

    //--------------------------------------------------------------------------

    /**
     * Create a new inbound slot with the specified remote endpoint.
     * If nullptr is returned, then the slot could not be assigned.
     * Usually this is because of a detected self-connection.
     */
    virtual std::pair<std::shared_ptr<Slot>, Result>
    newInboundSlot(
        beast::IP::Endpoint const& localEndpoint,
        beast::IP::Endpoint const& remoteEndpoint) = 0;

    /**
     * Create a new outbound slot with the specified remote endpoint.
     * If nullptr is returned, then the slot could not be assigned.
     * Usually this is because of a duplicate connection.
     */
    virtual std::pair<std::shared_ptr<Slot>, Result>
    newOutboundSlot(beast::IP::Endpoint const& remoteEndpoint) = 0;

    /**
     * Called when mtENDPOINTS is received.
     */
    virtual void
    onEndpoints(std::shared_ptr<Slot> const& slot, Endpoints const& endpoints) = 0;

    /**
     * Called when the slot is closed.
     * This always happens when the socket is closed, unless the socket
     * was canceled.
     */
    virtual void
    onClosed(std::shared_ptr<Slot> const& slot) = 0;

    /**
     * Called when an outbound connection is deemed to have failed
     */
    virtual void
    onFailure(std::shared_ptr<Slot> const& slot) = 0;

    /**
     * Called when we received redirect IPs from a busy peer.
     */
    virtual void
    onRedirects(
        boost::asio::ip::tcp::endpoint const& remoteAddress,
        std::vector<boost::asio::ip::tcp::endpoint> const& eps) = 0;

    //--------------------------------------------------------------------------

    /**
     * Called when an outbound connection attempt succeeds.
     * The local endpoint must be valid. If the caller receives an error
     * when retrieving the local endpoint from the socket, it should
     * proceed as if the connection attempt failed by calling on_closed
     * instead of on_connected.
     * @return `true` if the connection should be kept
     */
    virtual bool
    onConnected(std::shared_ptr<Slot> const& slot, beast::IP::Endpoint const& localEndpoint) = 0;

    /**
     * Request an active slot type.
     */
    virtual Result
    activate(std::shared_ptr<Slot> const& slot, PublicKey const& key, bool reserved) = 0;

    /**
     * Returns a set of endpoints suitable for redirection.
     */
    virtual std::vector<Endpoint>
    redirect(std::shared_ptr<Slot> const& slot) = 0;

    /**
     * Return a set of addresses we should connect to.
     */
    virtual std::vector<beast::IP::Endpoint>
    autoconnect() = 0;

    virtual std::vector<std::pair<std::shared_ptr<Slot>, std::vector<Endpoint>>>
    buildEndpointsForPeers() = 0;

    /**
     * Perform periodic activity.
     * This should be called once per second.
     */
    virtual void
    oncePerSecond() = 0;
};

}  // namespace xrpl::PeerFinder
