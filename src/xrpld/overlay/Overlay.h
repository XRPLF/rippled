#pragma once

#include <xrpld/overlay/Peer.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/net/IPAddress.h>
#include <xrpl/beast/net/IPEndpoint.h>
#include <xrpl/beast/utility/PropertyStream.h>
#include <xrpl/json/json_value.h>
#include <xrpl/peerfinder/PeerfinderManager.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/server/Handoff.h>

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/ssl/ssl_stream.hpp>

#include <xrpl.pb.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <vector>

namespace boost::asio::ssl {
class context;
}  // namespace boost::asio::ssl

namespace xrpl {

/**
 * How much of the ledger range this node needs its peer set can serve.
 *
 * Aggregated from the per-peer ranges advertised in mtSTATUS_CHANGE. The
 * distinction this exists to draw: a node that is behind because no connected
 * peer holds the sequence it wants is a supply problem to be fixed by changing
 * the peer set, while a node whose peers all hold that sequence is a
 * throughput problem. Without the aggregate the two look identical.
 *
 * Example usage -- the supply verdict from a telemetry gauge callback:
 * @code
 * auto const supply = overlay.getPeerLedgerSupply(validatedSeq);
 * if (supply.peersReporting > 0 && supply.peersServingNext == 0)
 *     // peers are connected, but none holds the next ledger needed
 * @endcode
 *
 * Example usage -- edge case: nothing has advertised a range yet:
 * @code
 * auto const supply = overlay.getPeerLedgerSupply(validatedSeq);
 * if (supply.peersReporting == 0)
 *     // supplyMinSeq / supplyMaxSeq are 0 and mean "unknown", not "empty"
 * @endcode
 *
 * @note A peer that has not yet sent a status change advertises [0, 0]. Such
 *       peers are excluded from every field, so `peersReporting` is the
 *       denominator that makes the other counts readable: zero serving out of
 *       zero reporting is silence, zero out of many is a genuine gap.
 */
struct PeerLedgerSupply
{
    /**
     * Connected peers that have advertised a non-empty ledger range.
     */
    std::int64_t peersReporting{0};

    /**
     * Reporting peers whose range covers this node's validated sequence.
     */
    std::int64_t peersServingValidated{0};

    /**
     * Reporting peers whose range covers validated + 1, the next one needed.
     */
    std::int64_t peersServingNext{0};

    /**
     * Lowest sequence any reporting peer offers; 0 when none report.
     */
    std::int64_t supplyMinSeq{0};

    /**
     * Highest sequence any reporting peer offers; 0 when none report.
     */
    std::int64_t supplyMaxSeq{0};
};

/**
 * Manages the set of connected peers.
 */
class Overlay : public beast::PropertyStream::Source
{
protected:
    using socket_type = boost::beast::tcp_stream;
    using stream_type = boost::beast::ssl_stream<socket_type>;

    // VFALCO NOTE The requirement of this constructor is an
    //             unfortunate problem with the API for
    //             PropertyStream
    Overlay() : beast::PropertyStream::Source("peers")
    {
    }

public:
    enum class Promote { Automatic, Never, Always };

    struct Setup
    {
        explicit Setup() = default;

        std::shared_ptr<boost::asio::ssl::context> context;
        beast::ip::Address publicIp;
        int ipLimit = 0;
        std::uint32_t crawlOptions = 0;
        std::optional<std::uint32_t> networkID;
        bool vlEnabled = true;
        bool verifyEndpoints = true;
    };

    using PeerSequence = std::vector<std::shared_ptr<Peer>>;

    ~Overlay() override = default;

    virtual void
    start()
    {
    }

    virtual void
    stop()
    {
    }

    /**
     * Conditionally accept an incoming HTTP request.
     */
    virtual Handoff
    onHandoff(
        std::unique_ptr<stream_type>&& bundle,
        http_request_type&& request,
        boost::asio::ip::tcp::endpoint remoteAddress) = 0;

    /**
     * Establish a peer connection to the specified endpoint.
     * The call returns immediately, the connection attempt is
     * performed asynchronously.
     */
    virtual void
    connect(beast::ip::Endpoint const& address) = 0;

    /**
     * Returns the maximum number of peers we are configured to allow.
     */
    virtual int
    limit() = 0;

    /**
     * Returns the number of active peers.
     * Active peers are only those peers that have completed the
     * handshake and are using the peer protocol.
     */
    [[nodiscard]] virtual std::size_t
    size() const = 0;

    /**
     * Return diagnostics on the status of all peers.
     * @deprecated This is superseded by PropertyStream
     */
    virtual json::Value
    json() = 0;

    /**
     * Returns a sequence representing the current list of peers.
     * The snapshot is made at the time of the call.
     */
    [[nodiscard]] virtual PeerSequence
    getActivePeers() const = 0;

    /**
     * Calls the checkTracking function on each peer
     * @param index the value to pass to the peer's checkTracking function
     */
    virtual void
    checkTracking(std::uint32_t index) = 0;

    /**
     * Returns the peer with the matching short id, or null.
     */
    [[nodiscard]] virtual std::shared_ptr<Peer>
    findPeerByShortID(Peer::id_t const& id) const = 0;

    /**
     * Returns the peer with the matching public key, or null.
     */
    virtual std::shared_ptr<Peer>
    findPeerByPublicKey(PublicKey const& pubKey) = 0;

    /**
     * Broadcast a proposal.
     */
    virtual void
    broadcast(protocol::TMProposeSet const& m) = 0;

    /**
     * Broadcast a validation.
     */
    virtual void
    broadcast(protocol::TMValidation const& m) = 0;

    /**
     * Relay a proposal.
     * @param m the serialized proposal
     * @param uid the id used to identify this proposal
     * @param validator The pubkey of the validator that issued this proposal
     * @return the set of peers which have already sent us this proposal
     */
    virtual std::set<Peer::id_t>
    relay(protocol::TMProposeSet const& m, uint256 const& uid, PublicKey const& validator) = 0;

    /**
     * Relay a validation.
     * @param m the serialized validation
     * @param uid the id used to identify this validation
     * @param validator The pubkey of the validator that issued this validation
     * @return the set of peers which have already sent us this validation
     */
    virtual std::set<Peer::id_t>
    relay(protocol::TMValidation const& m, uint256 const& uid, PublicKey const& validator) = 0;

    /**
     * Relay a transaction. If the tx reduce-relay feature is enabled then
     * randomly select peers to relay to and queue transaction's hash
     * for the rest of the peers.
     * @param hash transaction's hash
     * @param m transaction's protocol message to relay
     * @param toSkip peers which have already seen this transaction
     */
    virtual void
    relay(
        uint256 const& hash,
        std::optional<std::reference_wrapper<protocol::TMTransaction>> m,
        std::set<Peer::id_t> const& toSkip) = 0;

    /**
     * Visit every active peer.
     *
     * The visitor must be invocable as:
     *     Function(std::shared_ptr<Peer> const& peer);
     *
     * @param f the invocable to call with every peer
     */
    template <class Function>
    void
    foreach(Function f) const
    {
        for (auto const& p : getActivePeers())
            f(p);
    }

    /**
     * Increment and retrieve counter for transaction job queue overflows.
     */
    virtual void
    incJqTransOverflow() = 0;
    [[nodiscard]] virtual std::uint64_t
    getJqTransOverflow() const = 0;

    /**
     * Increment and retrieve counters for total peer disconnects, and
     * disconnects we initiate for excessive resource consumption.
     */
    virtual void
    incPeerDisconnect() = 0;
    [[nodiscard]] virtual std::uint64_t
    getPeerDisconnect() const = 0;
    virtual void
    incPeerDisconnectCharges() = 0;
    [[nodiscard]] virtual std::uint64_t
    getPeerDisconnectCharges() const = 0;

    /**
     * Returns the ID of the network this server is configured for, if any.
     *
     * The ID is just a numerical identifier, with the IDs 0, 1 and 2 used to
     * identify the mainnet, the testnet and the devnet respectively.
     *
     * @return The numerical identifier configured by the administrator of the
     *         server. An unseated optional, otherwise.
     */
    [[nodiscard]] virtual std::optional<std::uint32_t>
    networkID() const = 0;

    /**
     * Returns tx reduce-relay metrics
     * @return json value of tx reduce-relay metrics
     */
    [[nodiscard]] virtual json::Value
    txMetrics() const = 0;

    /**
     * Returns how much of the sequence range this node needs its peers can
     * actually serve.
     *
     * Each peer advertises the smallest and largest ledger it holds in
     * mtSTATUS_CHANGE, which the connection caches. Those ranges are never
     * compared against each other, so "no peer on the network holds the
     * ledger I need" is today indistinguishable from "my peers are slow".
     * This aggregates them into that answer.
     *
     * @param validatedSeq This node's validated sequence; the ledger it is
     *        currently able to serve from.
     * @return The supply counts and the sequence window the peer set covers.
     *
     * @note O(peers), taking the peer-list lock once. Intended for a ~10 s
     *       telemetry poll, never a per-message path.
     */
    [[nodiscard]] virtual PeerLedgerSupply
    getPeerLedgerSupply(std::uint32_t validatedSeq) const = 0;

    /**
     * Returns PeerFinder slot occupancy and address-cache depth.
     *
     * Forwarded from PeerFinder, which owns the counts. Exposed on Overlay
     * because that is the only handle the rest of the server holds; the
     * PeerFinder itself is private to the overlay implementation.
     *
     * Not `const`: the PeerFinder lock is a plain member, so no method on
     * that path can be const.
     *
     * @return One consistent snapshot of all nine fields.
     */
    [[nodiscard]] virtual peer_finder::SlotCensus
    getSlotCensus() = 0;
};

}  // namespace xrpl
