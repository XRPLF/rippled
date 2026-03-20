#pragma once

#include <xrpl/beast/net/IPEndpoint.h>
#include <xrpl/protocol/PublicKey.h>

#include <optional>

namespace xrpl {
namespace PeerFinder {

/** Properties and state associated with a peer to peer overlay connection. */
class Slot
{
public:
    using ptr = std::shared_ptr<Slot>;

    enum State { Accept, Connect, Connected, Active, Closing };

    virtual ~Slot() = 0;

    /** Returns `true` if this is an inbound connection. */
    virtual bool
    inbound() const = 0;

    /** Returns `true` if this is a fixed connection.
        A connection is fixed if its remote endpoint is in the list of
        remote endpoints for fixed connections.
    */
    virtual bool
    fixed() const = 0;

    /** Returns `true` if this is a reserved connection.
        It might be a cluster peer, or a peer with a reservation.
        This is only known after then handshake completes.
     */
    virtual bool
    reserved() const = 0;

    /** Returns the state of the connection. */
    virtual State
    state() const = 0;

    /** The remote endpoint of socket. */
    virtual beast::IP::Endpoint const&
    remoteEndpoint() const = 0;

    /** The local endpoint of the socket, when known. */
    virtual std::optional<beast::IP::Endpoint> const&
    localEndpoint() const = 0;

    virtual std::optional<std::uint16_t>
    listeningPort() const = 0;

    /** The peer's public key, when known.
        The public key is established when the handshake is complete.
    */
    virtual std::optional<PublicKey> const&
    publicKey() const = 0;
};

}  // namespace PeerFinder
}  // namespace xrpl
