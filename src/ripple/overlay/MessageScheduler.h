#ifndef RIPPLE_OVERLAY_MESSAGESCHEDULER_H_INCLUDED
#define RIPPLE_OVERLAY_MESSAGESCHEDULER_H_INCLUDED

#include <ripple/basics/chrono.h>
#include <ripple/beast/utility/Journal.h>
#include <ripple/overlay/Peer.h>
#include <ripple/overlay/impl/ProtocolMessage.h>
#include <ripple.pb.h>

#include <boost/asio/basic_waitable_timer.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/system/detail/error_code.hpp>
#include <google/protobuf/message.h>

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <functional>
#include <iterator>
#include <memory>
#include <mutex>
#include <ostream>
#include <random>

namespace ripple {

// TODO: Maybe we just want static constants instead of an enumeration?
enum class Priority : std::int8_t {
    // A ledger needed to catch up with consensus.
    TOP = 100,
    // A ledger wanted to choose the preferred branch.
    HIGH = 10,
    // A ledger wanted by us for a non-specific reason.
    DEFAULT = 0,
    // A ledger wanted by a peer.
    LOW = -10,
    // A ledger wanted to backfill history.
    BOTTOM = -100,
};

inline std::ostream&
operator<<(std::ostream& out, Priority priority)
{
    auto name = (priority > Priority::TOP) ? "TOP"
        : (priority > Priority::HIGH)      ? "HIGH"
        : (priority > Priority::LOW)       ? "DEFAULT"
        : (priority < Priority::BOTTOM)    ? "BOTTOM"
                                           : "LOW";
    return out << name;
}

// C++23: We may be able to switch to std::randint.
// https://en.cppreference.com/w/cpp/experimental/randint
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

class MessageScheduler
{
public:
    enum class FailureCode {
        // The request timed out.
        TIMEOUT,
        // The peer disconnected.
        DISCONNECT,
        // The message scheduler is shutting down.
        SHUTDOWN,
    };

    using RequestId = uint32_t;

    class PeerOffer;

    class Sender;

    // A receiver has methods for success and failure callbacks.
    // We package them together in the same object because they may share data,
    // and we want to make it easy to control the lifetime of that data.
    class Receiver;

    // The `request_cookie` field on a `TMLedgerData` message represents
    // a peer ID, which is a low number starting at 1 and believed to never
    // exceed 300. By using a non-overlapping set of numbers for our request
    // IDs, `PeerImp` can dispatch incoming responses `TMLedgerData` messages to
    // the correct receiver. Similarly for the `seq` field on
    // `TMGetObjectByHash` messages.
    static constexpr RequestId MINIMUM_REQUEST_ID = 1 << 9;

private:
    using Clock = std::chrono::steady_clock;
    using Timer = boost::asio::basic_waitable_timer<Clock>;
    using Time = std::chrono::time_point<Clock>;

    // An in-flight request.
    struct Request
    {
        RequestId id;
        WeakPeer peer;
        Receiver* receiver;
        Timer timer;
        Time sent;
    };

    boost::asio::io_service& io_service_;
    beast::Journal journal_;

    // Offers are negotiations between peers and senders.
    // This mutex must be locked when handling either set.
    std::mutex offersMutex_;
    std::vector<WeakPeer> peers_;
    // TODO: Use a priority queue.
    std::vector<Sender*> senders_;
    bool stopped_ = false;

    // Randomize the first ID to avoid collisions after a restart.
    std::atomic<RequestId> nextId_ =
        random_int<RequestId>(MINIMUM_REQUEST_ID, 1 << 24);
    std::mutex requestsMutex_;
    // TODO: Might make sense to use a set instead.
    hash_map<RequestId, std::unique_ptr<Request>> requests_;

public:
    MessageScheduler(
        boost::asio::io_service& io_service,
        beast::Journal const& journal)
        : io_service_(io_service), journal_(journal)
    {
    }

    /**
     * If there are any waiting senders, offer these peers to them.
     * Add any remaining unconsumed peers to the pool.
     *
     * TODO: Maybe this can be private.
     */
    void
    connect(std::vector<WeakPeer> peers);

    void
    connect(std::shared_ptr<Peer> peer)
    {
        JLOG(journal_.trace())
            << "connect,id=" << peer->id()
            << ",address=" << peer->getRemoteAddress().to_string();
        WeakPeer wpeer{peer->id(), peer};
        constexpr std::size_t SIMULTANEOUS_REQUESTS_PER_PEER = 1;
        std::vector<WeakPeer> peers(SIMULTANEOUS_REQUESTS_PER_PEER, wpeer);
        return connect(peers);
    }

    /**
     * If this peer is in the pool, remove it.
     * If it is responsible for any in-flight requests,
     * call their failure callbacks.
     * If those callbacks schedule any new senders,
     * offer them the other peers in the pool.
     */
    void
    disconnect(Peer::id_t peerId);

    /**
     * @return True if the sender was scheduled, guaranteeing that one of its
     * callbacks will be called. False if the sender was immediately
     * discarded (because the scheduler has stopped), returning responsibility
     * for its lifetime to the caller.
     */
    bool
    schedule(Sender* sender);

    RequestId
    send(
        std::shared_ptr<Peer> peer,
        protocol::TMGetLedger& message,
        Receiver* receiver,
        NetClock::duration timeout);
    RequestId
    send(
        std::shared_ptr<Peer> peer,
        protocol::TMGetObjectByHash& message,
        Receiver* receiver,
        NetClock::duration timeout);

    // TODO: Stop using shared_ptr for messages.
    // Instead, pass a unique_ptr to the receiver.
    // We don't need it after they're done.
    // They can either hold it or let it destruct.
    void
    receive(std::shared_ptr<protocol::TMLedgerData> message);
    void
    receive(std::shared_ptr<protocol::TMGetObjectByHash> message);

    void
    stop();

private:
    /**
     * Offer peers to senders, in turn, until senders either
     * (a) consume no peers,
     * in which case we skip over them, or
     * (b) stop scheduling new senders,
     * in which case they are effectively removed.
     *
     * @pre `peers` is not empty.
     * @post Either `peers` is empty,
     * or every sender in `senders` refused to consume any peer.
     */
    void
    _offer(std::vector<WeakPeer>& peers, std::vector<Sender*>& senders);

    template <typename Message>
    void
    _send(
        std::shared_ptr<Peer> peer,
        RequestId requestId,
        Message const& message,
        Receiver* receiver,
        NetClock::duration timeout)
    {
        return _send(
            peer,
            requestId,
            message,
            protocolMessageType(message),
            receiver,
            timeout);
    }

    void
    _send(
        std::shared_ptr<Peer> peer,
        RequestId requestId,
        ::google::protobuf::Message const& message,
        protocol::MessageType type,
        Receiver* receiver,
        NetClock::duration timeout);

    void
    _receive(
        RequestId requestId,
        std::shared_ptr<::google::protobuf::Message> message);
};

constexpr const char*
toString(MessageScheduler::FailureCode code)
{
    switch (code)
    {
        case MessageScheduler::FailureCode::TIMEOUT:
            return "TIMEOUT";
        case MessageScheduler::FailureCode::DISCONNECT:
            return "DISCONNECT";
        case MessageScheduler::FailureCode::SHUTDOWN:
            return "SHUTDOWN";
        default:
            return "<unknown failure code>";
    }
}

inline std::ostream&
operator<<(std::ostream& out, MessageScheduler::FailureCode code)
{
    return out << toString(code);
}

/**
 * `PeerOffer` represents an offering to consume M among N peers, M <= N.
 * M is called the "supply".
 *
 * `PeerOffer` is an interface around a set of peers represented by
 * a `std::vector` of weak pointers.
 * `PeerOffer` does not own the set; it holds the set by reference.
 * The set is owned by the stack.
 * When passed a `PeerOffer`, a `Sender` must use it or lose it.
 * `Sender`s may not save references to `PeerOffers`, or make copies.
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
 * `Sender`s may consume peers in the offer by sending messages to them.
 * After `PeerOffer` is destroyed, the set is left with only the remaining
 * unconsumed peers.
 */
class MessageScheduler::PeerOffer
{
public:
    class Iterator;
    friend class Iterator;

    friend class MessageScheduler;

private:
    MessageScheduler& scheduler_;
    std::vector<WeakPeer>& peers_;
    std::vector<RequestId> requestIds_;
    std::size_t supply_;
    std::size_t consumed_ = 0;
    std::size_t end_;

public:
    PeerOffer(
        MessageScheduler& scheduler,
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

private:
    void
    remove(std::size_t index)
    {
        assert(index < end_);
        --end_;
        peers_[index] = std::move(peers_[end_]);
    }
};

class MessageScheduler::PeerOffer::Iterator
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
    send(Message& message, Receiver* receiver, NetClock::duration timeout)
    {
        auto requestId =
            offer_.scheduler_.send(value_, message, receiver, timeout);
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
                if (value_)
                {
                    break;
                }
                else
                {
                    offer_.remove(index_);
                }
            }
        }
    }
};

inline MessageScheduler::PeerOffer::Iterator
MessageScheduler::PeerOffer::begin()
{
    return Iterator(*this);
}

class MessageScheduler::Sender
{
public:
    virtual void
    onOffer(PeerOffer&) = 0;
    virtual void
    onDiscard() = 0;
    virtual ~Sender()
    {
    }
};

class MessageScheduler::Receiver
{
public:
    virtual void onSuccess(
        RequestId,
        std::shared_ptr<::google::protobuf::Message>) = 0;
    virtual void onFailure(RequestId, FailureCode) = 0;
    virtual ~Receiver()
    {
    }
};

}  // namespace ripple

#endif
