#ifndef RIPPLE_OVERLAY_PEERSCHEDULER_H_INCLUDED
#define RIPPLE_OVERLAY_PEERSCHEDULER_H_INCLUDED

#include <ripple/basics/chrono.h>
#include <ripple/beast/utility/Journal.h>
#include <ripple/core/JobQueue.h>
#include <ripple/overlay/Peer.h>
#include <ripple/overlay/impl/ProtocolMessage.h>
#include <iterator>
#include <ripple.pb.h>

#include <boost/asio/basic_waitable_timer.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/system/detail/error_code.hpp>
#include <google/protobuf/message.h>

#include <atomic>
#include <cassert>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <ostream>
#include <random>

namespace ripple {

template <typename T>
T
random_int(T low, T high)
{
    std::random_device engine;
    std::uniform_int_distribution<T> distribution(low, high);
    return distribution(engine);
}

struct noop
{
    template <typename... Args>
    void
    operator()(Args&&...)
    {
    }
};

/**
 * We must hold idle peers by `std::weak_ptr` so that they can destruct upon
 * disconnect, but we need to know each peer's ID to find its pointer in
 * `remove`, because `std::weak_ptr` is not equality-comparable with anything.
 */
struct WeakPeer
{
    Peer::id_t id;
    std::weak_ptr<Peer> peer;
};

inline WeakPeer
toWeakPeer(std::shared_ptr<Peer> peer)
{
    return {peer->id(), peer};
}

/**
 *
 */
class PeerScheduler
{
public:
    enum class FailureCode {
        TIMEOUT,
        DISCONNECT,
    };

    using RequestId = uint32_t;
    using SuccessCallback =
        std::function<void(std::shared_ptr<::google::protobuf::Message>)>;
    using FailureCallback = std::function<void(FailureCode)>;

    class PeerOffer;

    using Client = std::function<void(PeerOffer&)>;

    // The `request_cookie` field on a `TMLedgerData` message represents
    // a peer ID, which is a low number starting at 1 and believed to never
    // exceed 300. By using a non-overlapping set of numbers for our request
    // IDs, `PeerImp` can dispatch incoming responses `TMLedgerData` messages
    // to the correct receiver. Similarly for the `seq` field on
    // `TMGetObjectByHash` messages.
    static constexpr RequestId MINIMUM_REQUEST_ID = 1 << 9;

private:
    // An in-flight request.
    struct Request
    {
        RequestId id;
        WeakPeer peer;
        SuccessCallback onSuccess;
        FailureCallback onFailure;
    };

    using Timer = boost::asio::basic_waitable_timer<std::chrono::steady_clock>;
    using TimerId = RequestId;

    boost::asio::io_service& io_service_;
    beast::Journal journal_;
    // Offers are negotiations between peers and clients.
    // This mutex must be locked when handling either set.
    std::mutex offersMutex_;
    std::vector<WeakPeer> peers_;
    // TODO: Use a priority queue.
    std::vector<Client> clients_;
    // Randomize the first ID to avoid collisions after a restart.
    std::atomic<RequestId> nextId_ =
        random_int<RequestId>(MINIMUM_REQUEST_ID, 1 << 24);
    std::mutex requestsMutex_;
    // TODO: Might make sense to use a set instead.
    hash_map<RequestId, std::unique_ptr<Request>> requests_;
    hash_map<TimerId, std::unique_ptr<Timer>> timers_;

    thread_local static std::vector<Client>* newClients_;

public:
    PeerScheduler(
        boost::asio::io_service& io_service,
        beast::Journal const& journal)
        : io_service_(io_service), journal_(journal)
    {
    }

    /**
     * If there are any waiting clients, offer these peers to them.
     * Add any remaining unconsumed peers to the pool.
     *
     * TODO: Maybe this can be private.
     */
    void
    add(std::vector<WeakPeer> peers);

    void
    add(std::shared_ptr<Peer> peer)
    {
        std::vector<WeakPeer> peers{{peer->id(), peer}};
        return add(peers);
    }

    /**
     * If this peer is in the pool, remove it.
     * If it is responsible for any in-flight requests,
     * call their failure callbacks.
     * If those callbacks schedule any new clients,
     * offer them the other peers in the pool.
     */
    void
    remove(Peer::id_t peerId);

    void
    schedule(Client client);

    RequestId
    send(
        std::shared_ptr<Peer> peer,
        protocol::TMGetLedger& message,
        SuccessCallback onSuccess = noop(),
        FailureCallback onFailure = noop());
    RequestId
    send(
        std::shared_ptr<Peer> peer,
        protocol::TMGetObjectByHash& message,
        SuccessCallback onSuccess = noop(),
        FailureCallback onFailure = noop());

    // TODO: Stop using shared_ptr for messages.
    // Instead, pass a unique_ptr to recipient.
    void
    receive(std::shared_ptr<protocol::TMLedgerData> message);
    void
    receive(std::shared_ptr<protocol::TMGetObjectByHash> message);

    void
    timeout(std::vector<RequestId> requestIds, NetClock::duration expiry);

private:
    /**
     * Offer peers to clients, in turn, until clients either
     * (a) consume no peers,
     * in which case we skip over them, or
     * (b) stop scheduling new clients,
     * in which case they are effectively removed.
     *
     * @pre `peers` is not empty.
     * @post Either `peers` is empty,
     * or every client in `clients` refused to consume any peer.
     */
    void
    _offer(std::vector<WeakPeer>& peers, std::vector<Client>& clients);

    template <typename Message>
    void
    _send(
        std::shared_ptr<Peer> peer,
        RequestId requestId,
        Message const& message,
        SuccessCallback&& onSuccess,
        FailureCallback&& onFailure)
    {
        return _send(
            peer,
            requestId,
            message,
            protocolMessageType(message),
            std::move(onSuccess),
            std::move(onFailure));
    }

    void
    _send(
        std::shared_ptr<Peer> peer,
        RequestId requestId,
        ::google::protobuf::Message const& message,
        protocol::MessageType type,
        SuccessCallback&& onSuccess,
        FailureCallback&& onFailure);

    void
    _receive(
        RequestId requestId,
        std::shared_ptr<::google::protobuf::Message> message);
};

inline std::ostream&
operator<<(std::ostream& out, PeerScheduler::FailureCode code)
{
    auto name = (code == PeerScheduler::FailureCode::TIMEOUT) ? "TIMEOUT"
                                                              : "DISCONNECT";
    return out << name;
}

/**
 * `PeerOffer` represents an offering to consume M among N peers, M <= N.
 * M is called the "supply".
 *
 * `PeerOffer` is an interface around a set of peers represented by
 * a `std::vector` of weak pointers.
 * `PeerOffer` does not own the set; it holds the set by reference.
 * The set is owned by the stack.
 * When passed a `PeerOffer`, a `Client` must use it or lose it.
 * `Client`s may not save references to `PeerOffers`, or make copies.
 *
 * `PeerOffer` has a companion iterator class, `PeerOffer::Iterator`, that
 * provides a convenient interface for skipping over dead weak pointers and
 * detecting supply exhaustion.
 * Its intended usage pattern is different from that of STL iterators:
 *
 * auto it = offer.begin();
 * while (it) {
 *     Peer& peer = *it;
 *     if (!isAcceptable(peer)) {
 *         it.skip();
 *         continue;
 *     }
 *     it.send(...);
 * }
 *
 * `Client`s may consume peers in the offer by sending messages to them.
 * After `PeerOffer` is destroyed, the set is left with only the remaining
 * unconsumed peers.
 */
class PeerScheduler::PeerOffer
{
public:
    class Iterator;
    friend class Iterator;

    friend class PeerScheduler;

private:
    PeerScheduler& scheduler_;
    std::vector<WeakPeer>& peers_;
    std::vector<RequestId> requestIds_;
    std::size_t supply_;
    std::size_t consumed_ = 0;
    std::size_t end_;

public:
    PeerOffer(
        PeerScheduler& scheduler,
        std::vector<WeakPeer>& peers,
        std::size_t supply)
        : scheduler_(scheduler)
        , peers_(peers)
        , supply_(supply)
        , end_(peers.size())
    {
    }

    // Delete all copy and move constructors and assignments.
    PeerOffer&
    operator=(PeerOffer&&) = delete;

    ~PeerOffer()
    {
        peers_.erase(peers_.begin() + end_, peers_.end());
    }

    Iterator
    begin();

    /**
     * Return the remaining supply.
     */
    std::size_t
    supply() const
    {
        return supply_ - consumed_;
    }

    std::size_t
    consumed() const
    {
        return consumed_;
    }

    /**
     * Create one common timeout for all messages sent since the last timeout.
     *
     * When the timeout expires, for all remaining in-flight requests,
     * resolve them as failed with the `TIMEOUT` reason.
     * Collect any new clients, return all timed out peers to the pool,
     * and then serve all clients with all peers.
     */
    void
    timeout(NetClock::duration expiry = std::chrono::seconds{5})
    {
        if (!requestIds_.empty())
        {
            scheduler_.timeout(std::move(requestIds_), expiry);
            requestIds_.clear();
        }
    }

private:
    void
    remove(std::size_t index)
    {
        assert(index < end_);
        --end_;
        peers_[index] = std::move(peers_[end_]);
    }
};

class PeerScheduler::PeerOffer::Iterator
{
private:
    PeerOffer& offer_;
    std::shared_ptr<Peer> value_ = nullptr;
    std::size_t index_ = 0;

public:
    Iterator(PeerOffer& offer) : offer_(offer)
    {
        next();
    }

    operator bool() const
    {
        return static_cast<bool>(value_);
    }

    void
    skip()
    {
        ++index_;
        next();
    }

    template <typename Message>
    void
    send(
        Message& message,
        SuccessCallback onSuccess = noop(),
        FailureCallback onFailure = noop())
    {
        auto requestId = offer_.scheduler_.send(
            value_, message, std::move(onSuccess), std::move(onFailure));
        offer_.requestIds_.push_back(requestId);
        ++offer_.consumed_;
        offer_.remove(index_);
        next();
    }

private:
    /**
     * Sets `value_` to the next available peer starting at `index_`, or to
     * `nullptr` if none remaining. Removes every missing peer along the
     * way. A peer is available if its `std::weak_ptr<Peer>` is lockable.
     */
    void
    next()
    {
        value_.reset();
        if (offer_.supply_)
        {
            auto& peers = offer_.peers_;
            while (index_ < offer_.end_)
            {
                value_ = peers[index_].peer.lock();
                if (!value_)
                {
                    offer_.remove(index_);
                }
            }
        }
    }
};

inline PeerScheduler::PeerOffer::Iterator
PeerScheduler::PeerOffer::begin()
{
    return Iterator(*this);
}

}  // namespace ripple

#endif
