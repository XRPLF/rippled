/** @file
 *  Defines the abstract read-only interface for a PeerFinder connection slot.
 *
 *  `Manager` creates and owns slots internally, handing `shared_ptr<Slot>`
 *  handles outward so callers can observe connection state without mutating it.
 *  All mutations happen through `SlotImp` in `detail/`.
 */

#pragma once

#include <xrpl/beast/net/IPEndpoint.h>
#include <xrpl/protocol/PublicKey.h>

#include <optional>

namespace xrpl::PeerFinder {

/** Read-only view of a single peer-to-peer overlay connection managed by PeerFinder.
 *
 *  Carries both the immutable socket identity (remote endpoint, direction) and
 *  the mutable state that evolves as the connection progresses through its
 *  lifecycle. The interface is deliberately minimal — consumers only ever need
 *  to read properties; all writes go through `SlotImp`.
 *
 *  @note `Manager` hands out `shared_ptr<Slot>` handles, allowing the overlay
 *      layer to hold references safely across asynchronous operations without
 *      coupling to `SlotImp`'s mutable API.
 *  @see SlotImp, Manager
 */
class Slot
{
public:
    using ptr = std::shared_ptr<Slot>;

    /** Ordered lifecycle phases of a peer connection.
     *
     *  The state machine is enforced by `Logic`: transitions are validated by
     *  `XRPL_ASSERT` in `SlotImp::state()` and `SlotImp::activate()`. The
     *  ordering also drives diagnostic strings in `Logic::stateString()`.
     */
    enum class State {
        Accept,    /**< Inbound socket accepted; handshake not yet started. */
        Connect,   /**< Outbound connection attempt in progress. */
        Connected, /**< TCP established; overlay handshake underway. */
        Active,    /**< Handshake complete; peer is fully participating. */
        Closing    /**< Connection is winding down. */
    };

    virtual ~Slot() = 0;

    /** Returns `true` if this is an inbound connection.
     *
     *  Feeds the slot counters in `Counts` (which track `in_active` vs.
     *  `out_active` separately) and affects peer-privacy rules — only outbound
     *  connections are suppressed when `peerPrivate` is enabled.
     */
    [[nodiscard]] virtual bool
    inbound() const = 0;

    /** Returns `true` if the remote endpoint is in the operator-configured fixed-peers list.
     *
     *  Fixed connections are exempt from the normal slot budget enforced by
     *  `Counts::can_activate()`, guaranteeing connectivity to trusted cluster
     *  nodes regardless of how full the peer slots are. A disconnected fixed
     *  outbound peer is treated by `Logic` as a permanent reconnect obligation.
     */
    [[nodiscard]] virtual bool
    fixed() const = 0;

    /** Returns `true` if this peer has a cluster membership or an explicit reservation.
     *
     *  Like `fixed()`, reserved slots are not counted against the public slot
     *  cap. This flag is populated during `SlotImp::activate()` once the
     *  handshake reveals the peer's identity and must not be relied on before
     *  `State::Active`.
     *
     *  @note Unknown (and therefore `false`) until the handshake completes.
     */
    [[nodiscard]] virtual bool
    reserved() const = 0;

    /** Returns the current lifecycle state of the connection.
     *
     *  @return One of the `State` enumerators reflecting where this slot sits
     *      in the connection state machine.
     *  @see State
     */
    [[nodiscard]] virtual State
    state() const = 0;

    /** Returns the remote endpoint of the socket.
     *
     *  Always known from the moment the slot is created — for both inbound
     *  and outbound connections.
     */
    [[nodiscard]] virtual beast::IP::Endpoint const&
    remoteEndpoint() const = 0;

    /** Returns the local endpoint of the socket, if known.
     *
     *  For inbound connections the local address is populated at construction.
     *  For outbound connections it is only filled in when
     *  `Manager::onConnected()` fires and the OS-assigned local port can be
     *  read from the socket, so it may be `nullopt` before that point.
     */
    [[nodiscard]] virtual std::optional<beast::IP::Endpoint> const&
    localEndpoint() const = 0;

    /** Returns the port on which the remote peer accepts inbound connections, if known.
     *
     *  Populated during the overlay handshake via `mtENDPOINTS`. Returns
     *  `nullopt` until the remote peer has advertised its listener port.
     *
     *  @note Implemented with an `atomic<int32_t>` sentinel (`-1` = unknown)
     *      in `SlotImp`, enabling lock-free reads while the handshake thread
     *      writes. The sentinel is hidden from this interface.
     */
    [[nodiscard]] virtual std::optional<std::uint16_t>
    listeningPort() const = 0;

    /** Returns the peer's public key, if the handshake has completed.
     *
     *  Populated after a successful overlay handshake. `Logic` uses this as a
     *  deduplication key — if `activate()` detects a key that matches an
     *  already-active slot, it returns `Result::duplicatePeer` and the
     *  connection is rejected.
     *
     *  @note Returns `nullopt` until `State::Active`.
     */
    [[nodiscard]] virtual std::optional<PublicKey> const&
    publicKey() const = 0;
};

}  // namespace xrpl::PeerFinder
