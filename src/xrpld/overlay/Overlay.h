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

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <vector>

namespace boost::asio::ssl {
class context;
}  // namespace boost::asio::ssl

namespace xrpl {

/**
 * How much of what this node needs next its peer set can serve.
 *
 * @code
 *   PeerImp::ledgerRange() --> tallyPeerLedgerSupply() --> PeerLedgerSupply
 *   (one range per peer)       (this node's validated seq)  (counts, window)
 * @endcode
 *
 * The needed ledger is validated + 1. A node with no validated ledger yet
 * fetches the network's newest ledger instead, so for it the needed ledger is
 * the highest sequence any peer reports.
 *
 * Zero peers serving the needed ledger means one of two things, and
 * `peersAhead` tells them apart. With no peer ahead, nothing newer exists in
 * the peer set, which is normal at the tip. With peers ahead, none of them
 * offers the ledger after this node's: it still catches up by fetching the
 * newest ledger by hash, but cannot fill the ledgers in between from these
 * peers.
 *
 * Example usage -- the supply verdict from a telemetry gauge callback:
 * @code
 * auto const supply = overlay.getPeerLedgerSupply(validatedSeq);
 * if (supply.peersAhead > 0 && supply.peersServingNext == 0)
 *     // peers hold newer ledgers, but none holds the one after ours
 * @endcode
 *
 * Example usage -- edge case: at the tip there is nothing to fetch:
 * @code
 * auto const supply = overlay.getPeerLedgerSupply(validatedSeq);
 * if (supply.peersReporting > 0 && supply.peersAhead == 0)
 *     // peersServingNext is 0 because validated + 1 does not exist yet
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
 *       zero reporting is silence.
 * @note A peer advertises up to its published ledger, and can also serve its
 *       newer last closed ledger by hash, so these counts are a floor.
 */
struct PeerLedgerSupply
{
    /**
     * Connected peers that have advertised a non-empty ledger range.
     */
    std::int64_t peersReporting{0};

    /**
     * Reporting peers whose range ends above this node's validated sequence.
     * Zero means no peer holds anything newer, which is normal at the tip.
     */
    std::int64_t peersAhead{0};

    /**
     * Reporting peers whose range covers this node's validated sequence.
     */
    std::int64_t peersServingValidated{0};

    /**
     * Reporting peers whose range covers the needed ledger: validated + 1, or
     * the newest reported ledger on a node with no validated ledger yet.
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
 * One peer's advertised ledger range, as tallyPeerLedgerSupply() reads it.
 *
 * @code
 *   PeerImp::ledgerRange() --> PeerLedgerRange --> tallyPeerLedgerSupply()
 * @endcode
 *
 * A range is either [0, 0], meaning the peer has not reported, or
 * 1 <= minSeq <= maxSeq. PeerImp zeroes any other range it receives, so a
 * caller that builds ranges by hand must keep to the same rule.
 *
 * @code
 * // A peer holding ledgers 1000 through 4000.
 * PeerLedgerRange const held{.minSeq = 1000, .maxSeq = 4000};
 *
 * // A peer that has not reported yet: the tally skips it.
 * PeerLedgerRange const silent{};
 * @endcode
 *
 * @note A plain value with no lock; copy it freely.
 */
struct PeerLedgerRange
{
    /**
     * Oldest sequence the peer offers.
     */
    std::uint32_t minSeq{0};

    /**
     * Newest sequence the peer offers.
     */
    std::uint32_t maxSeq{0};
};

/**
 * Tally what a set of advertised peer ranges can serve a node.
 *
 * Kept inline and free of the overlay so a test can run it on fixed ranges.
 *
 * @param ranges One range per connected peer. A [0, 0] range has not been
 *        reported and is skipped.
 * @param validatedSeq This node's validated sequence; 0 before the first one.
 * @return The counts and the sequence window the reporting peers cover.
 */
[[nodiscard]] inline PeerLedgerSupply
tallyPeerLedgerSupply(std::span<PeerLedgerRange const> ranges, std::uint32_t validatedSeq)
{
    PeerLedgerSupply supply;
    auto lowest = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t highest = 0;

    for (auto const& range : ranges)
    {
        if (range.maxSeq == 0)
            continue;

        ++supply.peersReporting;
        if (range.maxSeq > validatedSeq)
            ++supply.peersAhead;
        if (validatedSeq >= range.minSeq && validatedSeq <= range.maxSeq)
            ++supply.peersServingValidated;

        lowest = std::min(lowest, range.minSeq);
        highest = std::max(highest, range.maxSeq);
    }

    // Nothing reported: the window stays 0, meaning unknown.
    if (supply.peersReporting == 0)
        return supply;

    supply.supplyMinSeq = lowest;
    supply.supplyMaxSeq = highest;

    // A node with no validated ledger fetches the newest one, not ledger 1.
    // Widened so validated + 1 cannot wrap to 0.
    auto const neededSeq =
        validatedSeq == 0 ? std::uint64_t{highest} : std::uint64_t{validatedSeq} + 1;

    for (auto const& range : ranges)
    {
        if (neededSeq >= range.minSeq && neededSeq <= range.maxSeq)
            ++supply.peersServingNext;
    }
    return supply;
}

/**
 * Manages the set of connected peers.
 */
class Overlay : public beast::PropertyStream::Source
{
protected:
    using SocketType = boost::beast::tcp_stream;
    using StreamType = boost::beast::ssl_stream<SocketType>;

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
        std::unique_ptr<StreamType>&& bundle,
        HttpRequestType&& request,
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
    findPeerByShortID(Peer::ID const& id) const = 0;

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
    virtual std::set<Peer::ID>
    relay(protocol::TMProposeSet const& m, UInt256 const& uid, PublicKey const& validator) = 0;

    /**
     * Relay a validation.
     * @param m the serialized validation
     * @param uid the id used to identify this validation
     * @param validator The pubkey of the validator that issued this validation
     * @return the set of peers which have already sent us this validation
     */
    virtual std::set<Peer::ID>
    relay(protocol::TMValidation const& m, UInt256 const& uid, PublicKey const& validator) = 0;

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
        UInt256 const& hash,
        std::optional<std::reference_wrapper<protocol::TMTransaction>> m,
        std::set<Peer::ID> const& toSkip) = 0;

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
     * Returns how much of what this node needs next its peers can serve.
     *
     * Reads the range each active peer last advertised in mtSTATUS_CHANGE and
     * passes them to tallyPeerLedgerSupply(), which holds the arithmetic.
     *
     * @param validatedSeq This node's validated sequence; 0 before the first
     *        one, which makes the needed ledger the newest one peers report.
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
