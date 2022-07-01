#ifndef RIPPLE_OVERLAY_MESSAGESCHEDULER_H_INCLUDED
#define RIPPLE_OVERLAY_MESSAGESCHEDULER_H_INCLUDED

#include <ripple/basics/chrono.h>
#include <ripple/basics/random.h>
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

/**
 * We must hold idle peers by `std::weak_ptr` so that they can destruct upon
 * disconnect.
 * When we remove channels in `remove`, we need to find them by peer ID,
 * because `std::weak_ptr` is not equality-comparable with anything,
 * but we do not want to lock the `std::weak_ptr` to get that ID,
 * so we copy it.
 */
struct Channel
{
    Peer::id_t peerId;
    std::weak_ptr<Peer> peer;
};

inline Channel
toChannel(std::shared_ptr<Peer> peer)
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

    class Offer;

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
        Channel channel;
        Receiver* receiver;
        Timer timer;
        Time sent;
    };

    boost::asio::io_service& io_service_;
    beast::Journal journal_;

    // Offers are negotiations between peers and senders.
    // This mutex must be locked when handling either set.
    std::mutex offersMutex_;
    std::vector<Channel> channels_;
    // TODO: Use a priority queue.
    std::vector<Sender*> senders_;
    bool stopped_ = false;

    // Randomize the first ID to avoid collisions after a restart.
    // C++23: We may be able to switch to std::randint.
    // https://en.cppreference.com/w/cpp/experimental/randint
    std::atomic<RequestId> nextId_ =
        rand_int<RequestId>(MINIMUM_REQUEST_ID, 1 << 24);
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
     * If there are any waiting senders, offer these channels to them.
     * Add any remaining unconsumed channels to the pool.
     *
     * TODO: Maybe this can be private.
     */
    void
    connect(std::vector<Channel> channels);

    void
    connect(std::shared_ptr<Peer> peer)
    {
        JLOG(journal_.trace())
            << "connect,id=" << peer->id()
            << ",address=" << peer->getRemoteAddress().to_string();
        Channel channel{peer->id(), peer};
        // TODO: Let a peer choose its number of channels when it connects.
        constexpr std::size_t CHANNELS_PER_PEER = 1;
        std::vector<Channel> channels(CHANNELS_PER_PEER, channel);
        return connect(channels);
    }

    /**
     * If this peer has any channels in the pool, remove them.
     * If it is responsible for any in-flight requests,
     * call their failure callbacks.
     * If those callbacks schedule any new senders,
     * offer them the other channels in the pool.
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
     * Offer channels to senders, in turn, until senders either
     * (a) consume no channels,
     * in which case we skip over them, or
     * (b) stop scheduling new senders,
     * in which case they are effectively removed.
     *
     * @pre `channels` is not empty.
     * @post Either `channels` is empty,
     * or every sender in `senders` refused to consume any peer.
     */
    void
    _offer(std::vector<Channel>& channels, std::vector<Sender*>& senders);

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
 * `Offer` represents an offer to consume M among N channels, M <= N.
 * M is called the "supply".
 *
 * `Offer` is an interface around a set of channels represented by
 * a `std::vector` of weak pointers.
 * `Offer` does not own the set; it holds the set by reference.
 * The set is owned by the stack.
 * When passed an `Offer`, a `Sender` must use it or lose it.
 * `Sender`s may not save references to `PeerOffers`, or make copies.
 *
 * `Offer` has a companion iterator class, `Offer::Iterator`, that
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
 * `Sender`s may consume channels in the offer by sending messages to them.
 * After `Offer` is destroyed, the set is left with only the remaining
 * unconsumed channels.
 */
class MessageScheduler::Offer
{
public:
    class Iterator;
    friend class Iterator;

    friend class MessageScheduler;

private:
    MessageScheduler& scheduler_;
    std::vector<Channel>& channels_;
    std::vector<RequestId> requestIds_;
    std::size_t supply_;
    std::size_t consumed_ = 0;
    std::size_t end_;

public:
    Offer(
        MessageScheduler& scheduler,
        std::vector<Channel>& channels,
        std::size_t supply)
        : scheduler_(scheduler)
        , channels_(channels)
        , supply_(supply)
        , end_(channels.size())
    {
    }

    // Delete all copy and move constructors and assignments.
    Offer&
    operator=(Offer&&) = delete;

    ~Offer()
    {
        channels_.erase(channels_.begin() + end_, channels_.end());
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
        channels_[index] = std::move(channels_[end_]);
    }
};

class MessageScheduler::Offer::Iterator
{
private:
    Offer& offer_;
    std::shared_ptr<Peer> value_ = nullptr;
    std::size_t index_ = 0;

public:
    Iterator(Offer& offer) : offer_(offer)
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
            auto& channels = offer_.channels_;
            while (index_ < offer_.end_)
            {
                value_ = channels[index_].peer.lock();
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

inline MessageScheduler::Offer::Iterator
MessageScheduler::Offer::begin()
{
    return Iterator(*this);
}

class MessageScheduler::Sender
{
public:
    virtual void
    onOffer(Offer&) = 0;
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
