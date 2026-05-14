#include <xrpld/overlay/PeerSet.h>

#include <xrpld/app/main/Application.h>
#include <xrpld/overlay/Message.h>
#include <xrpld/overlay/Overlay.h>
#include <xrpld/overlay/Peer.h>

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Journal.h>

#include <google/protobuf/message.h>

#include <xrpl.pb.h>

#include <algorithm>
#include <cstddef>
#include <functional>
#include <memory>
#include <set>
#include <utility>
#include <vector>

namespace xrpl {

/** Live implementation of `PeerSet` backed by the overlay.
 *
 *  Stores only numeric `Peer::id_t` values rather than `shared_ptr<Peer>`
 *  to avoid keeping disconnected peer objects alive. Peer pointers are
 *  resolved transiently via `Overlay::findPeerByShortID` only at send time.
 *
 *  `peers_` doubles as a lifetime-scoped exclusion list: once a peer has been
 *  added it can never be re-added, so successive `addPeers` calls across
 *  retry rounds never contact the same peer twice for the same acquisition.
 */
class PeerSetImpl : public PeerSet
{
public:
    /** Construct with a reference to the running application.
     *
     *  @param app  Used to access `Overlay` (for peer enumeration and lookup)
     *              and to obtain a journal for diagnostics.
     */
    PeerSetImpl(Application& app);

    /** Select up to `limit` new peers using scored, descending-priority ordering.
     *
     *  Scores every currently connected peer via `peer->getScore(hasItem(peer))`.
     *  Peers believed to already hold the target item score higher, so requests
     *  reach useful peers first. Candidates are sorted descending and accepted
     *  until `limit` new peers have been inserted into `peers_`. Peers already
     *  in `peers_` are skipped without calling `onPeerAdded`.
     *
     *  If all overlay peers are exhausted before `limit` is reached, the method
     *  returns silently; the caller's `TimeoutCounter` is responsible for
     *  declaring the acquisition failed.
     *
     *  @param limit       Maximum number of new peers to add.
     *  @param hasItem     Predicate returning `true` if the peer is believed to
     *                     hold the target data; used to boost score during sort.
     *  @param onPeerAdded Invoked once per accepted peer immediately after
     *                     insertion; typically sends the initial fetch request.
     */
    void
    addPeers(
        std::size_t limit,
        std::function<bool(std::shared_ptr<Peer> const&)> hasItem,
        std::function<void(std::shared_ptr<Peer> const&)> onPeerAdded) override;

    /** Send a serialized protobuf message to one peer or broadcast to all tracked peers.
     *
     *  A single `Message` packet is allocated once and shared across all send
     *  calls in the broadcast path, avoiding redundant serialization. Peers that
     *  have disconnected since being added are silently skipped.
     *
     *  @param message  Protobuf message to serialize and send.
     *  @param type     Wire-protocol message type tag.
     *  @param peer     If non-null, sends only to that peer; if null, broadcasts
     *                  to every `Peer::id_t` in `peers_`.
     */
    void
    sendRequest(
        ::google::protobuf::Message const& message,
        protocol::MessageType type,
        std::shared_ptr<Peer> const& peer) override;

    [[nodiscard]] std::set<Peer::id_t> const&
    getPeerIds() const override;

private:
    /// Provides access to `Overlay` for peer enumeration and journal creation.
    Application& app_;
    beast::Journal journal_;

    /** Numeric IDs of all peers ever added to this acquisition.
     *
     *  Acts as an exclusion list across repeated `addPeers` calls: insert
     *  returning `false` means the peer is already tracked and is skipped.
     */
    std::set<Peer::id_t> peers_;
};

PeerSetImpl::PeerSetImpl(Application& app) : app_(app), journal_(app.getJournal("PeerSet"))
{
}

void
PeerSetImpl::addPeers(
    std::size_t limit,
    std::function<bool(std::shared_ptr<Peer> const&)> hasItem,
    std::function<void(std::shared_ptr<Peer> const&)> onPeerAdded)
{
    using ScoredPeer = std::pair<int, std::shared_ptr<Peer>>;

    auto const& overlay = app_.getOverlay();

    std::vector<ScoredPeer> pairs;
    pairs.reserve(overlay.size());

    overlay.foreach([&](auto const& peer) {
        auto const score = peer->getScore(hasItem(peer));
        pairs.emplace_back(score, std::move(peer));
    });

    std::ranges::sort(
        pairs, [](ScoredPeer const& lhs, ScoredPeer const& rhs) { return lhs.first > rhs.first; });

    std::size_t accepted = 0;
    for (auto const& pair : pairs)
    {
        auto const peer = pair.second;
        if (!peers_.insert(peer->id()).second)
            continue;
        onPeerAdded(peer);
        if (++accepted >= limit)
            break;
    }
}

void
PeerSetImpl::sendRequest(
    ::google::protobuf::Message const& message,
    protocol::MessageType type,
    std::shared_ptr<Peer> const& peer)
{
    auto packet = std::make_shared<Message>(message, type);
    if (peer)
    {
        peer->send(packet);
        return;
    }

    for (auto id : peers_)
    {
        if (auto p = app_.getOverlay().findPeerByShortID(id))
            p->send(packet);
    }
}

std::set<Peer::id_t> const&
PeerSetImpl::getPeerIds() const
{
    return peers_;
}

/** Concrete factory that produces `PeerSetImpl` instances.
 *
 *  Holds an `Application&` reference and passes it to each newly constructed
 *  `PeerSetImpl`. Test code can substitute its own `PeerSetBuilder` to inject
 *  mock or instrumented `PeerSet` objects without touching production paths.
 *
 *  @see makePeerSetBuilder
 */
class PeerSetBuilderImpl : public PeerSetBuilder
{
public:
    /** @param app  Application reference forwarded to every built `PeerSetImpl`. */
    PeerSetBuilderImpl(Application& app) : app_(app)
    {
    }

    /** Construct and return a new `PeerSetImpl` bound to the live overlay. */
    std::unique_ptr<PeerSet>
    build() override
    {
        return std::make_unique<PeerSetImpl>(app_);
    }

private:
    Application& app_;
};

std::unique_ptr<PeerSetBuilder>
makePeerSetBuilder(Application& app)
{
    return std::make_unique<PeerSetBuilderImpl>(app);
}

/** Null-object `PeerSet` used during offline ledger loading.
 *
 *  Injected by `ApplicationImp::loadOldLedger()` when an `InboundLedger` is
 *  constructed solely to drive local ledger-assembly logic without any live
 *  peer interaction. All three interface methods log an error if called —
 *  they represent programming errors in this context, not expected no-ops —
 *  so any accidental peer interaction during startup replay surfaces
 *  immediately rather than silently misbehaving.
 *
 *  `getPeerIds()` returns a reference to a `static` empty set to satisfy the
 *  return-by-reference contract without undefined behavior.
 *
 *  @see makeDummyPeerSet
 */
class DummyPeerSet : public PeerSet
{
public:
    /** @param app  Used only to obtain a journal for error logging. */
    DummyPeerSet(Application& app) : j_(app.getJournal("DummyPeerSet"))
    {
    }

    /** Logs an error; must not be called during offline ledger loading. */
    void
    addPeers(
        std::size_t limit,
        std::function<bool(std::shared_ptr<Peer> const&)> hasItem,
        std::function<void(std::shared_ptr<Peer> const&)> onPeerAdded) override
    {
        JLOG(j_.error()) << "DummyPeerSet addPeers should not be called";
    }

    /** Logs an error; must not be called during offline ledger loading. */
    void
    sendRequest(
        ::google::protobuf::Message const& message,
        protocol::MessageType type,
        std::shared_ptr<Peer> const& peer) override
    {
        JLOG(j_.error()) << "DummyPeerSet sendRequest should not be called";
    }

    /** Logs an error and returns a reference to a static empty set.
     *
     *  @note The static sentinel ensures the returned reference remains valid
     *      for the lifetime of the process, satisfying the interface contract
     *      even though this code path should never be reached in normal operation.
     */
    [[nodiscard]] std::set<Peer::id_t> const&
    getPeerIds() const override
    {
        static std::set<Peer::id_t> const kEMPTY_PEERS;
        JLOG(j_.error()) << "DummyPeerSet getPeerIds should not be called";
        return kEMPTY_PEERS;
    }

private:
    beast::Journal j_;
};

std::unique_ptr<PeerSet>
makeDummyPeerSet(Application& app)
{
    return std::make_unique<DummyPeerSet>(app);
}

}  // namespace xrpl
