#pragma once

#include <xrpld/app/main/Application.h>
#include <xrpld/overlay/Peer.h>
#include <xrpld/overlay/detail/ProtocolMessage.h>

namespace xrpl {

/** Manages the set of peers queried during a single distributed data retrieval.
 *
 *  When a node lacks locally-stored data — a ledger, transaction set, or
 *  SHAMap subtree — it fetches it from connected peers. `PeerSet` owns the
 *  peer-selection, deduplication, and message-dispatch concerns for one
 *  in-flight acquisition task.
 *
 *  Each acquisition (`InboundLedger`, `TransactionAcquire`, `LedgerReplayer`,
 *  etc.) holds a `std::unique_ptr<PeerSet>` as its exclusive transport handle.
 *  Because ownership is exclusive, `PeerSet` itself needs no internal locking;
 *  callers serialize access through their own acquisition mutex.
 *
 *  The concrete implementation (`PeerSetImpl`, hidden in `detail/PeerSet.cpp`)
 *  maintains a `std::set<Peer::id_t>` that prevents the same peer from being
 *  contacted twice for the same acquisition across multiple `addPeers` calls.
 *
 *  @see makePeerSetBuilder, makeDummyPeerSet
 */
class PeerSet
{
public:
    virtual ~PeerSet() = default;

    /** Select and contact additional peers for this acquisition.
     *
     *  Scores every currently connected peer via `Peer::getScore(hasItem(peer))`,
     *  sorts candidates in descending score order (favouring peers that have
     *  signalled they hold the target data), then adds up to `limit` peers
     *  that have not been contacted before. `onPeerAdded` is invoked
     *  immediately for each newly accepted peer so the caller can send an
     *  initial request.
     *
     *  Deduplication is enforced across calls: a peer already in the tracked
     *  set is never re-added, regardless of how many times `addPeers` is
     *  called.
     *
     *  @param limit       Maximum number of new peers to add in this call.
     *  @param hasItem     Predicate returning `true` if the given peer is
     *                     believed to hold the desired item; used to boost
     *                     the peer's score during selection.
     *  @param onPeerAdded Called once per accepted peer immediately after it
     *                     is inserted into the tracked set; typically used to
     *                     send the initial data request.
     */
    virtual void
    addPeers(
        std::size_t limit,
        std::function<bool(std::shared_ptr<Peer> const&)> hasItem,
        std::function<void(std::shared_ptr<Peer> const&)> onPeerAdded) = 0;

    /** Send a typed protobuf request to one peer or broadcast to all tracked peers.
     *
     *  Type-safe wrapper that deduces the `protocol::MessageType` from the
     *  concrete protobuf message type via `protocolMessageType()` (defined in
     *  `detail/ProtocolMessage.h`), then delegates to the virtual overload.
     *  This solves the templated-virtual-function impossibility: the
     *  type-safe API lives here, while the single virtual dispatch point
     *  handles the erased protobuf base class.
     *
     *  @param message  Protobuf message to send (e.g. `TMGetLedger`,
     *                  `TMReplayDeltaRequest`).
     *  @param peer     If non-null, sends only to that peer. If null,
     *                  broadcasts to every peer in the tracked set by
     *                  resolving each `Peer::id_t` through the overlay.
     */
    template <typename MessageType>
    void
    sendRequest(MessageType const& message, std::shared_ptr<Peer> const& peer)
    {
        this->sendRequest(message, protocolMessageType(message), peer);
    }

    /** Send a protobuf message (type-erased) to one peer or all tracked peers.
     *
     *  When `peer` is non-null the packet is sent only to that peer.
     *  When `peer` is null the packet is broadcast to every `Peer::id_t`
     *  in the tracked set, resolving each through `Overlay::findPeerByShortID`.
     *  Peers that have disconnected since they were added are silently skipped.
     *
     *  @param message  Serialized protobuf message.
     *  @param type     Wire-protocol message type tag.
     *  @param peer     Target peer, or null to broadcast to all tracked peers.
     */
    virtual void
    sendRequest(
        ::google::protobuf::Message const& message,
        protocol::MessageType type,
        std::shared_ptr<Peer> const& peer) = 0;

    /** Return the set of IDs of all peers added to this acquisition so far.
     *
     *  Callers (e.g. `InboundLedger`) inspect this to count active peers and
     *  decide whether an acquisition should be abandoned. The returned
     *  reference is valid for the lifetime of this `PeerSet`.
     */
    [[nodiscard]] virtual std::set<Peer::id_t> const&
    getPeerIds() const = 0;
};

/** Abstract factory that creates `PeerSet` instances on demand.
 *
 *  Subsystems that manage multiple concurrent acquisitions
 *  (`InboundLedgersImp`, `InboundTransactions`, `LedgerReplayer`) receive a
 *  `std::unique_ptr<PeerSetBuilder>` at construction and call `build()` each
 *  time a new acquisition begins. This defers concrete `PeerSetImpl`
 *  construction — which requires a live `Application&` — to the moment it is
 *  actually needed, and allows tests to inject a mock builder without touching
 *  production code paths.
 *
 *  @see makePeerSetBuilder
 */
class PeerSetBuilder
{
public:
    virtual ~PeerSetBuilder() = default;

    /** Construct and return a new `PeerSet` for one acquisition task. */
    virtual std::unique_ptr<PeerSet>
    build() = 0;
};

/** Create a `PeerSetBuilder` backed by the live overlay.
 *
 *  Each call to `PeerSetBuilder::build()` on the returned object produces a
 *  `PeerSetImpl` that selects peers from `app.getOverlay()` and dispatches
 *  real protobuf messages over the network.
 *
 *  @param app  The running application; held by reference in each built
 *              `PeerSet` for overlay and journal access.
 */
std::unique_ptr<PeerSetBuilder>
makePeerSetBuilder(Application& app);

/** Create a no-op `PeerSet` that logs an error on every operation.
 *
 *  Used by `ApplicationImp::loadOldLedger()` when constructing `InboundLedger`
 *  objects to replay historical state without live peer activity. Rather than
 *  propagating a nullable pointer that every call site must guard, the
 *  codebase injects a `DummyPeerSet` that fulfils the `PeerSet` contract while
 *  emitting an error-level log entry and doing nothing else. This catches
 *  accidental calls in development without crashing production nodes during
 *  startup replay.
 *
 *  @param app  Application reference (used only to obtain a journal).
 */
std::unique_ptr<PeerSet>
makeDummyPeerSet(Application& app);

}  // namespace xrpl
