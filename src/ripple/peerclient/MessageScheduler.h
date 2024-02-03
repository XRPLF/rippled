//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2022 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef RIPPLE_PEERCLIENT_MESSAGESCHEDULER_H_INCLUDED
#define RIPPLE_PEERCLIENT_MESSAGESCHEDULER_H_INCLUDED

#include <ripple/basics/chrono.h>
#include <ripple/basics/random.h>
#include <ripple/beast/utility/Journal.h>
#include <ripple/overlay/Peer.h>
#include <ripple/overlay/impl/ProtocolMessage.h>

#include <boost/asio/basic_waitable_timer.hpp>
#include <boost/asio/io_service.hpp>
#include <google/protobuf/message.h>

#include <algorithm>
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

using PeerId = Peer::id_t;
using PeerPtr = std::shared_ptr<Peer>;

using ChannelCnt = std::uint16_t;

/**
 * We must hold idle peers by `std::weak_ptr`
 * so that they can destruct upon disconnect.
 * When we remove metapeers from our bookkeeping in `disconnect`,
 * we need to find them by peer ID,
 * because `std::weak_ptr` is not equality-comparable with anything,
 * but we do not want to lock the `std::weak_ptr` to get that ID,
 * so we copy it into `MetaPeer`.
 */
struct MetaPeer
{
    std::weak_ptr<Peer> peer;
    PeerId id;
    ChannelCnt nchannels;
    /** Number of channels that are closed. May exceed `nchannels`. */
    ChannelCnt nclosed;

    bool
    hasOpenChannels(std::lock_guard<std::mutex> const& lock) const
    {
        return nchannels > nclosed;
    }
};

using MetaPeerPtr = MetaPeer*;
// A set of peers, ordered by ID.
using MetaPeerSet = std::vector<MetaPeerPtr>;

using MessagePtr = std::shared_ptr<::google::protobuf::Message>;

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

    class Courier;

    class Sender;
    using SenderQueue = std::vector<Sender*>;

    // A receiver has methods for success and failure callbacks.
    // We package them together in the same object because they may share data,
    // and we want to make it easy to control the lifetime of that data.
    class Receiver;

    // We use the `request_cookie` field on `TMLedgerData` messages and the
    // `seq` field on `TMGetObjectByHash` messages to match responses with
    // requests. Call these fields *request identifiers*. `PeerImp` uses these
    // fields to dispatch responses to the correct receiver, one of which is
    // `MessageScheduler`. `MessageScheduler` then uses it do dispatch the
    // response to the correct `MessageScheduler::Receiver`.
    //
    // `MessageScheduler` uses request identifiers in the range
    // [`MINIMUM_REQUEST_ID`, `MAXIMUM_REQUEST_ID`], a span of over 4 billion.
    // `MINIMUM_REQUEST_ID` is chosen to leave enough low numbers that we can
    // reasonably assume that the other senders using request identifiers do
    // not start to creep into our space and unintentionally use our request
    // identifiers. The other senders use peer identifiers, which are low
    // numbers starting from 1 and incrementing for each peer added, never
    // wrapping, and never reusing even after a peer disconnects. We never
    // expect the number of peers to come anywhere close to
    // `MINIMUM_REQUEST_ID` (over 16 million) before a server is shutdown.
    //
    // We expect requests to happen on the order of 10 per second, but
    // even if requests happen at a rate of 1000/second it would take
    // about 50 days for our request identifiers to lap themselves.
    // A timeout should never exceed 24 hours. So it should be safe to wrap
    // our request identifiers back to `MINIMUM_REQUEST_ID` if they should
    // overflow `MAXIMUM_REQUEST_ID`.
    using RequestId = uint32_t;
    static constexpr RequestId MINIMUM_REQUEST_ID = 1 << 24;
    static constexpr RequestId MAXIMUM_REQUEST_ID =
        std::numeric_limits<RequestId>::max();

private:
    using Clock = std::chrono::steady_clock;
    using Timer = boost::asio::basic_waitable_timer<Clock>;
    using Time = std::chrono::time_point<Clock>;

    friend struct MessageSchedulerLogger;

    // An in-flight request.
    struct Request
    {
        RequestId id;
        protocol::MessageType responseType;
        MetaPeerPtr metaPeer;
        Receiver* receiver;
        Timer timer;
        Time sent;
    };

    boost::asio::io_service& io_service_;
    beast::Journal journal_;

    // This mutex protects all members, including peers and requests.
    std::mutex mutex_;
    // This set owns the metapeers.
    MetaPeerSet peers_;
    // The sum, across all connected peers, of all channels.
    ChannelCnt nchannels_ = 0;
    SenderQueue senders_;
    // Once the scheduler is stopped, new senders must be rejected.
    bool stopped_ = false;

    // Randomize the first ID to avoid collisions after a restart.
    // C++23: We may be able to switch to std::randint.
    // https://en.cppreference.com/w/cpp/experimental/randint
    std::atomic<RequestId> prevId_ =
        rand_int<RequestId>(MINIMUM_REQUEST_ID, MAXIMUM_REQUEST_ID);
    // The sum, across all connected peers, of closed channels.
    ChannelCnt nclosed_ = 0;
    // C++20: Look into `unordered_set` with heterogeneous lookup.
    hash_map<RequestId, std::unique_ptr<Request>> requests_;

public:
    MessageScheduler(
        boost::asio::io_service& io_service,
        beast::Journal const& journal)
        : io_service_(io_service), journal_(journal)
    {
    }

    /**
     * If there are any waiting senders,
     * then offer these new open channels to them.
     * Then add this peer to the pool.
     */
    void
    connect(PeerPtr const& peer, ChannelCnt nchannels = 2);

    /**
     * If this peer has any channels in the pool, remove them.
     * If it is responsible for any in-flight requests,
     * call their failure callbacks.
     * If those callbacks schedule any new senders,
     * offer them the other channels in the pool.
     */
    void
    disconnect(PeerId peerId);

    /**
     * @return True if the sender was scheduled, guaranteeing that one of its
     * callbacks will be called. False if the sender was immediately
     * discarded (because the scheduler has stopped), returning responsibility
     * for its lifetime to the caller.
     */
    bool
    schedule(Sender* sender);

    // TODO: Stop using shared_ptr for messages.
    // Instead, pass a unique_ptr to the receiver.
    // We don't need it after they're done.
    // They can either hold it or let it destruct.
    // Requires changes to `PeerImp`, which calls these methods.
    void
    receive(std::shared_ptr<protocol::TMLedgerData> const& message);
    void
    receive(std::shared_ptr<protocol::TMGetObjectByHash> const& message);

    // TODO: Let callers withdraw senders and receivers.

    void
    stop();

private:
    bool
    hasOpenChannels(std::lock_guard<std::mutex> const& lock) const
    {
        return nchannels_ > nclosed_;
    }

    bool
    canSchedule(Sender* sender) const;

    void
    negotiateNewPeers(
        std::lock_guard<std::mutex> const& lock,
        MetaPeerSet& peers);

    void
    negotiateNewSenders(std::lock_guard<std::mutex> const& lock);

    /**
     * Offer channels to senders, in turn, until senders either
     * (a) close no channels,
     * in which case we skip over them, or
     * (b) close some channels but stop scheduling new senders,
     * in which case they are effectively removed from the sender queue.
     *
     * @pre The sum of open channels among `freshPeers` is greater than zero.
     * @post Every sender remaining in `senders` refused to close any channel.
     */
    // REVIEW: How to document the `freshPeers` parameter? It's peers who have
    // opened channels since the last time, if ever, that they were offered to
    // senders. There are basically five situations where these offers are
    // made:
    //
    // 1. A peer connects. All of its channels are open, and it alone is the
    // set of "fresh peers" offered to all waiting senders.
    // 2. A sender schedules. Every peer is fresh to it.
    // 3. A peer disconnects. By the time `disconnect` is called, any
    // `std::weak_ptr` to the peer will not lock, meaning every call to
    // `send_` for that peer will fail. All waiting receivers are notified of
    // failure, and they may schedule new senders. Those senders have to wait
    // until the `receiverMutex_` is unlocked before they can be offered open
    // channels, but this is like scheduling any other sender.
    // 4. A response is received. This case has two subcases:
    //     a. The peer who delivered a response re-opens the channel that
    //     response used. That peer is fresh to all waiting senders.
    //     b. The receiver can schedule more senders. Every peer is fresh to
    //     those senders.
    // 5. A request times out. This case is exactly the same as receiving
    // a response.
    void
    negotiate(
        std::lock_guard<std::mutex> const& lock,
        MetaPeerSet& peers,
        SenderQueue& senders);

    /**
     * Send a request to a peer.
     * Accept its response with a receiver.
     * Diagnose failure if the timeout expires.
     *
     * The message must be taken by non-const reference in order to assign
     * a request identifier.
     *
     * @return zero if the message cannot be sent to the peer; non-zero
     * otherwise
     */
    RequestId
    send(
        std::lock_guard<std::mutex> const& lock,
        MetaPeerPtr const& metaPeer,
        protocol::TMGetLedger& message,
        Receiver* receiver,
        NetClock::duration timeout);
    RequestId
    send(
        std::lock_guard<std::mutex> const& lock,
        MetaPeerPtr const& metaPeer,
        protocol::TMGetObjectByHash& message,
        Receiver* receiver,
        NetClock::duration timeout);

    RequestId
    send_(
        std::lock_guard<std::mutex> const& lock,
        MetaPeerPtr const& metaPeer,
        RequestId requestId,
        ::google::protobuf::Message const& message,
        protocol::MessageType requestType,
        protocol::MessageType responseType,
        Receiver* receiver,
        NetClock::duration timeout);

    void
    receive_(
        RequestId requestId,
        protocol::MessageType type,
        MessagePtr const& message);

    void
    timeout_(RequestId requestId);

    void
    reopen(
        std::lock_guard<std::mutex> const& lock,
        char const* caller,
        MetaPeer* metaPeer,
        std::function<void()> callback);

    RequestId
    nextId_()
    {
        RequestId prevId;
        RequestId nextId;
        do
        {
            prevId = prevId_.load(std::memory_order_relaxed);
            nextId = (prevId == MAXIMUM_REQUEST_ID) ? MINIMUM_REQUEST_ID
                                                    : prevId + 1;
        } while (!prevId_.compare_exchange_weak(
            prevId, nextId, std::memory_order_relaxed));
        return nextId;
    }
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
 * `Courier` represents an offer to close M among N channels, M <= N.
 * M is called the "limit".
 *
 * A `Sender` is passed a `Courier` when `MessageScheduler` calls `onReady()`.
 * The sender must use it or lose it.
 * The sender must not save references or copies of metapeers
 * that outlive the call to `onReady()`.
 *
 * The sender may close channels in the courier by sending messages through
 * them. If the sender closes any channels, or calls `withdraw()`, then it is
 * removed from the sender queue in `MessageScheduler` after it returns from
 * `onReady()`.
 */
class MessageScheduler::Courier
{
private:
    friend class MessageScheduler;

    MessageScheduler& scheduler_;
    /**
     * `lock_` is a lock on `scheduler_.mutex_`.
     * Its presence guarantees that
     * no other thread is reading or writing the sender queue.
     * It is required to call `MessageScheduler::send()`.
     */
    std::lock_guard<std::mutex> const& lock_;
    /** `peers_` is owned by the stack. */
    MetaPeerSet const& peers_;
    ChannelCnt limit_;
    ChannelCnt closed_ = 0;
    bool evict_ = false;

public:
    Courier(
        MessageScheduler& scheduler,
        std::lock_guard<std::mutex> const& lock,
        MetaPeerSet const& peers,
        ChannelCnt limit)
        : scheduler_(scheduler), lock_(lock), peers_(peers), limit_(limit)
    {
        assert(!peers.empty());
        assert(std::all_of(
            std::begin(peers), std::end(peers), [&lock](auto const& metaPeer) {
                return metaPeer->hasOpenChannels(lock);
            }));
        assert(limit_ > closed_);
    }

    // Delete all copy and move constructors and assignments.
    Courier&
    operator=(Courier&&) = delete;

    MetaPeerSet const&
    allPeers() const
    {
        return scheduler_.peers_;
    }

    // REVIEW: Call these `freshPeers` or `newPeers`? Feel like "new" implies
    // "newly connected", but most of the time it's a long-connected peer with
    // a recently re-opened channel. `openPeers` would be even worse because
    // there can be peers in `allPeers` that have open channels too.
    MetaPeerSet const&
    peers() const
    {
        return peers_;
    }

    /**
     * @return zero if the message cannot be sent to the peer; non-zero
     * otherwise
     */
    template <typename Message>
    RequestId
    send(
        MetaPeerPtr const& metaPeer,
        Message& message,
        Receiver* receiver,
        NetClock::duration timeout)
    {
        auto requestId =
            scheduler_.send(lock_, metaPeer, message, receiver, timeout);
        if (requestId)
        {
            ++closed_;
            evict_ = true;
        }
        return requestId;
    }

    template <typename Message, typename Duration>
    RequestId
    send(
        MetaPeerPtr const& metaPeer,
        Message& message,
        Receiver* receiver,
        Duration timeout)
    {
        return send(
            metaPeer,
            message,
            receiver,
            std::chrono::duration_cast<NetClock::duration>(timeout));
    }

    ChannelCnt
    limit() const
    {
        return limit_;
    }

    ChannelCnt
    closed() const
    {
        return closed_;
    }

    void
    withdraw()
    {
        evict_ = true;
    }

    bool
    evicting() const
    {
        return evict_;
    }
};

using Blacklist = std::vector<PeerId>;

/**
 * Round-robin after random shuffle, until limit exhausted.
 *
 * // `blaster` constructs itself with a copy of the set of all peers.
 * auto blaster = Blaster(courier);
 * // `blaster` is truthy until either:
 * // a. there are no peers in the set, or
 * // b. the number of closed channels meets or exceeds the courier's limit.
 * while (blaster) {
 *     // Trying to send a message either:
 *     // a. removes a closed peer from the set, or
 *     // b. increments the number of closed channels.
 *     auto sent = blaster.send(request, receiver, timeout);
 *     if (sent) {
 *         break;
 *     }
 * }
 */
class Blaster
{
public:
    enum Result {
        SENT,
        RETRY,
        FAILED,
    };

private:
    MessageScheduler::Courier& courier_;
    MetaPeerSet openPeers_;
    MetaPeerSet closedPeers_{};
    std::size_t index_ = 0;

public:
    Blaster(MessageScheduler::Courier& courier)
        : courier_(courier), openPeers_(courier.allPeers())
    {
        // Move closed peers to their own set.
        auto mid = std::partition(
                openPeers_.begin(), openPeers_.end(),
                [&](MetaPeerPtr peer){ return peer->nclosed < peer->nchannels; });
        std::move(mid, openPeers_.end(), std::back_inserter(closedPeers_));
        openPeers_.erase(mid, std::end(openPeers_));
        assert(!openPeers_.empty());
        std::random_device rd;
        std::mt19937 urbg(rd());
        std::shuffle(openPeers_.begin(), openPeers_.end(), urbg);
    }

    operator bool() const
    {
        // Limit is assumed to be no greater than number of open channels,
        // implying there should be no possibility of an infinite loop.
        return !openPeers_.empty() && courier_.limit() > courier_.closed();
    }

    template <typename Message, typename Duration>
    Result
    send(
        Blacklist& blacklist,
        Message& message,
        MessageScheduler::Receiver* receiver,
        Duration timeout)
    {
        assert(*this);
        std::size_t skipped = 0;
        while (skipped != openPeers_.size()) {
            assert(skipped < openPeers_.size());
            auto const& peer = openPeers_[index_];
            if (peer->nclosed >= peer->nchannels) {
                // We closed this peer's last channel on a previous pass
                // but are just now discovering it.
                // Move it to the closed set and continue at the same index.
                closedPeers_.push_back(peer);
                std::swap(openPeers_[index_], openPeers_.back());
                openPeers_.pop_back();
                continue;
            }
            if (std::find(blacklist.begin(), blacklist.end(), peer->id) != blacklist.end()) {
                index_ = (index_ + 1) % openPeers_.size();
                ++skipped;
                continue;
            }
            auto sent = courier_.send(peer, message, receiver, timeout);
            // Add to the blacklist unconditionally. Either:
            // - It belongs in the blacklist: the message could not be sent,
            // or it will timeout, or the peer will disconnect before
            // responding.
            // - Or the blacklist won't matter: the receiver will receive
            // a response, or the application will shut down.
            blacklist.push_back(peer->id);
            if (sent) {
                return SENT;
            }
            closedPeers_.push_back(peer);
            std::swap(openPeers_[index_], openPeers_.back());
            openPeers_.pop_back();
            break;
        }
        for (auto const& peer : closedPeers_) {
            if (std::find(blacklist.begin(), blacklist.end(), peer->id) == blacklist.end()) {
                return RETRY;
            }
        }
        return FAILED;
    }
};

class MessageScheduler::Sender
{
public:
    /**
     * Called when channels are open.
     *
     * Each offer has a size that limits the number of messages the sender may
     * send.
     * Senders should respect this limit, but it is not enforced.
     * `Offer::Iterator`, provided for the sender's convenience,
     * respects the offer size.
     *
     * Senders may not save references to peers or channels found in this
     * offer.
     * The channels in this offer are good only for the lifetime of the offer,
     * i.e. the duration of the call to `Sender::onReady`.
     *
     * Senders may filter through the offer, selecting channels for messages
     * based on any arbitrary condition.
     * Senders may send as few or as many messages as they want, from zero to
     * the offer size, inclusive.
     *
     * @see MessageScheduler::Offer
     * @see MessageScheduler::Offer::Iterator
     */
    // TODO: Fix this documentation.
    // TODO: Should this method return true/false whether the sender should be
    // removed from the queue even if it did not consume any/all of its quota?
    virtual void
    onReady(Courier&) = 0;

    /**
     * Called when the message scheduler is shutting down but no one withdrew
     * the sender.
     */
    virtual void
    onDiscard() = 0;

    virtual ~Sender() = default;
};

class MessageScheduler::Receiver
{
public:
    /**
     * Called when a response has arrived.
     *
     * Callbacks should be trivial.
     * Non-trivial work should be scheduled in a job.
     */
    virtual void
    onSuccess(RequestId, MessagePtr const&) = 0;

    /**
     * Called under a few failure conditions:
     *
     * - The request timed out.
     * - The peer disconnected.
     * - The message scheduler is shutting down but no one withdrew the
     *   receiver.
     */
    virtual void onFailure(RequestId, FailureCode) = 0;

    virtual ~Receiver() = default;
};

}  // namespace ripple

#endif
