#pragma once

#include <xrpl/beast/net/IPEndpoint.h>
#include <xrpl/protocol/PublicKey.h>

#include <optional>

namespace xrpl::PeerFinder {

/** Properties and state associated with a peer to peer overlay connection. */
class Slot
{
public:
    using ptr = std::shared_ptr<Slot>;

    enum State { accept, connect, connected, active, closing };

    virtual ~Slot() = 0;

    /** Returns `true` if this is an inbound connection. */
    [[nodiscard]] virtual bool
    inbound() const = 0;

    /** Returns `true` if this is a fixed connection.
        A connection is fixed if its remote endpoint is in the list of
        remote endpoints for fixed connections.
    */
    [[nodiscard]] virtual bool
    fixed() const = 0;

    /** Returns `true` if this is a reserved connection.
        It might be a cluster peer, or a peer with a reservation.
        This is only known after then handshake completes.
     */
    [[nodiscard]] virtual bool
    reserved() const = 0;

    /** Returns the state of the connection. */
    [[nodiscard]] virtual State
    state() const = 0;

    /** The remote endpoint of socket. */
    [[nodiscard]] virtual beast::IP::Endpoint const&
    remote_endpoint() const = 0;

    /** The local endpoint of the socket, when known. */
    [[nodiscard]] virtual std::optional<beast::IP::Endpoint> const&
    local_endpoint() const = 0;

    [[nodiscard]] virtual std::optional<std::uint16_t>
    listening_port() const = 0;

    /** The peer's public key, when known.
        The public key is established when the handshake is complete.
    */
    [[nodiscard]] virtual std::optional<PublicKey> const&
    public_key() const = 0;
};

}  // namespace xrpl::PeerFinder
