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

#include <ripple/basics/Log.h>
#include <ripple/basics/chrono.h>
#include <ripple/peerclient/MessageScheduler.h>

#include <cassert>
#include <chrono>
#include <exception>
#include <iterator>
#include <mutex>

namespace ripple {

using RequestId = MessageScheduler::RequestId;

// We use a thread-local variable to track whether a sender is being
// scheduled from a callback in a thread where the scheduler is already
// locked. In that case, trying to lock the scheduler again is undefined
// behavior. Instead, we just save the sender here to be handled after the
// callback returns.
thread_local static MessageScheduler::SenderQueue* tsenders = nullptr;
// The value of `during` is logged at the start of every call to `schedule`,
// when a sender is being added to the queue,
// and `negotiate`, when a channel is being offered to a sender.
// Before every call to those methods,
// it is assigned to the name of the calling method.
// The calls to `schedule` are buried in calls to the sender callbacks,
// `onReady` and `onFailure`.
thread_local static char const* during = "idle";

/**
 * "Push" a value at this point in the call stack.
 * "Pop" the value when exiting the current scope.
 */
template <typename T>
struct push_value
{
    T& variable;
    T previous;

    push_value(T& variable, T next)
        : variable(variable), previous(std::move(variable))
    {
        variable = std::move(next);
    }

    ~push_value()
    {
        variable = std::move(previous);
    }
};

void
MessageScheduler::connect(PeerPtr const& peer, ChannelCnt nchannels)
{
    if (nchannels < 1)
    {
        return;
    }
    JLOG(journal_.info()) << "connect,id=" << peer->id() << ",address="
                          << peer->getRemoteAddress().to_string();
    std::lock_guard<std::mutex> lock(mutex_);
    auto id = peer->id();
    auto metaPeer = new MetaPeer{peer, id, nchannels, /*closed=*/0};
    // Peers connect in different threads.
    // They are assigned IDs in increasing order,
    // but they may not call this method in that order.
    // We must place them into `peers_` in order of increasing ID.
    auto it = std::find_if(peers_.begin(), peers_.end(), [id](auto& metaPeer) {
        return metaPeer->id > id;
    });
    peers_.insert(it, metaPeer);
    assert(std::is_sorted(
        peers_.begin(), peers_.end(), [](auto const& a, auto const& b) {
            return a->id < b->id;
        }));
    nchannels_ += nchannels;
    if (!senders_.empty())
    {
        push_value during_(during, "connect");
        assert(!tsenders);
        SenderQueue senders;
        push_value tsenders_(tsenders, &senders);
        MetaPeerSet peers{metaPeer};
        negotiateNewPeers(lock, peers);
    }
}

template <typename Container>
bool
contains(Container const& c, typename Container::value_type const& value)
{
    return std::find(std::begin(c), std::end(c), value) != std::end(c);
}

template <typename Container, typename Pred, typename OutputIterator>
typename Container::size_type
reap_if(Container& c, Pred pred, OutputIterator out)
{
    typename Container::size_type count = 0;
    for (auto it = std::begin(c); it != std::end(c);)
    {
        auto& value = *it;
        if (pred(value))
        {
            *out++ = std::move(value);
            it = c.erase(it);
            ++count;
        }
        else
        {
            ++it;
        }
    }
    return count;
}

template <typename T, typename U>
std::vector<T>&
operator+=(std::vector<T>& lhs, std::vector<U>& rhs)
{
    std::move(std::begin(rhs), std::end(rhs), std::back_inserter(lhs));
    return lhs;
}

void
MessageScheduler::disconnect(PeerId peerId)
{
    JLOG(journal_.warn()) << "disconnect,id=" << peerId;
    push_value during_(during, "disconnect");
    std::vector<typename decltype(requests_)::value_type> requests;

    {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = std::find_if(
            peers_.begin(), peers_.end(), [peerId](auto const& metaPeer) {
                return metaPeer->id == peerId;
            });
        if (it == peers_.end())
        {
            JLOG(journal_.warn())
                << "peer disconnected, but never connected: " << peerId;
            return;
        }
        auto metaPeer = *it;
        assert(metaPeer);
        peers_.erase(it);

        // Reap all of the requests waiting on the peer.
        reap_if(
            requests_,
            [metaPeer](auto const& item) {
                return item.second->metaPeer == metaPeer;
            },
            std::back_inserter(requests));

        assert(requests.size() == metaPeer->nclosed);
        assert(nclosed_ >= metaPeer->nclosed);
        nclosed_ -= metaPeer->nclosed;
        assert(nchannels_ >= metaPeer->nchannels);
        nchannels_ -= metaPeer->nchannels;

        // This does not require the lock,
        // but it looks cleaner to leave it in this block.
        delete metaPeer;
    }

    // Fail all pending requests with reason `DISCONNECT`.
    for (auto const& [_, request] : requests)
    {
        // We cancel the timer here as a courtesy, ignoring the result.
        // If this call returns zero,
        // then the timer has already expired
        // and its callback will be called.
        // That callback will call `timeout`,
        // but it will fail to find the request.
        // If it had already been called and found the request,
        // then we wouldn't have it here.
        request->timer.cancel();
        try
        {
            // This callback may call `schedule`
            // which is fine because we're not holding the lock.
            request->receiver->onFailure(request->id, FailureCode::DISCONNECT);
        }
        catch (...)
        {
            JLOG(journal_.error())
                << "unhandled exception from onFailure(DISCONNECT)";
        }
    }
}

bool
MessageScheduler::schedule(Sender* sender)
{
    if (tsenders)
    {
        // `mutex_` is already locked in this thread.
        if (!canSchedule(sender))
        {
            return false;
        }
        // Stash new senders to be served later.
        tsenders->emplace_back(sender);
        return true;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (!canSchedule(sender))
    {
        return false;
    }
    push_value during_(during, "schedule");
    assert(!tsenders);
    SenderQueue senders{sender};
    push_value tsenders_(tsenders, &senders);
    negotiateNewSenders(lock);
    return true;
}

bool
MessageScheduler::canSchedule(Sender* sender) const
{
    // The lock must be held, but we have no way to assert that.
    // Callers should not even try
    // to schedule a sender that is already scheduled.
    // TODO: This can happen when `onReady` for a sender re-queues that sender
    // before it is evicted from the queue (after `onReady` returns).
    // assert(!contains(senders_, sender));
    // assert(!tsenders || !contains(*tsenders, sender));
    return !stopped_;
}

RequestId
MessageScheduler::send(
    std::lock_guard<std::mutex> const& lock,
    MetaPeerPtr const& metaPeer,
    protocol::TMGetLedger& message,
    Receiver* receiver,
    NetClock::duration timeout)
{
    if (!metaPeer->hasOpenChannels(lock))
    {
        return 0;
    }
    auto requestId = nextId_();
    message.set_requestcookie(requestId);
    JLOG(journal_.debug()) << "send,type=get_ledger";
    return send_(
        lock,
        metaPeer,
        requestId,
        message,
        protocol::mtGET_LEDGER,
        protocol::mtLEDGER_DATA,
        receiver,
        timeout);
}

RequestId
MessageScheduler::send(
    std::lock_guard<std::mutex> const& lock,
    MetaPeerPtr const& metaPeer,
    protocol::TMGetObjectByHash& message,
    Receiver* receiver,
    NetClock::duration timeout)
{
    if (!metaPeer->hasOpenChannels(lock))
    {
        // No open channels.
        return 0;
    }
    auto requestId = nextId_();
    message.set_query(true);
    message.set_seq(requestId);
    JLOG(journal_.debug()) << "send,type=get_objects,count="
                           << message.objects_size();
    return send_(
        lock,
        metaPeer,
        requestId,
        message,
        protocol::mtGET_OBJECTS,
        protocol::mtGET_OBJECTS,
        receiver,
        timeout);
}

void
MessageScheduler::negotiateNewPeers(
    std::lock_guard<std::mutex> const& lock,
    MetaPeerSet& peers)
{
    assert(tsenders);
    assert(!senders_.empty());
    // `negotiate` will assert that `peers` is a non-empty set of open peers.
    negotiate(lock, peers, senders_);
    negotiateNewSenders(lock);
}

void
MessageScheduler::negotiateNewSenders(std::lock_guard<std::mutex> const& lock)
{
    assert(tsenders);
    if (!tsenders->empty() && this->hasOpenChannels(lock))
    {
        MetaPeerSet peers{peers_};
        std::erase_if(peers, [&lock](auto const& metaPeer) {
            return !metaPeer->hasOpenChannels(lock);
        });
        // `negotiate` will assert that `peers` is a non-empty set of open
        // peers.
        negotiate(lock, peers, *tsenders);
    }
    senders_ += *tsenders;
    std::erase(senders_, nullptr);
}

void
MessageScheduler::negotiate(
    std::lock_guard<std::mutex> const& lock,
    MetaPeerSet& peers,
    SenderQueue& senders)
{
    assert(!senders.empty());
    assert(!peers.empty());
    assert(std::all_of(
        std::begin(peers), std::end(peers), [&lock](auto const& metaPeer) {
            return metaPeer->hasOpenChannels(lock);
        }));
    push_value during_(during, "negotiate");
    // We must iterate `senders` using an index
    // because the offer may grow the vector,
    // invalidating any iterators or references.
    for (auto i = 0; i < senders.size() && !peers.empty(); ++i)
    {
        // If this is the last sender, offer it the full set of open channels.
        // If there are more senders waiting, offer one at a time, in turn.
        ChannelCnt limit =
            (i + 1 == senders.size()) ? (nchannels_ - nclosed_) : 1;
        Courier courier{*this, lock, peers, limit};
        try
        {
            senders[i]->onReady(courier);
        }
        catch (...)
        {
            JLOG(journal_.error()) << "unhandled exception from onReady";
        }
        if (courier.evict_)
        {
            senders[i] = nullptr;
        }
        // Remove any closed peers.
        std::erase_if(peers, [&lock](auto const& metaPeer) {
            return !metaPeer->hasOpenChannels(lock);
        });
    }
}

RequestId
MessageScheduler::send_(
    std::lock_guard<std::mutex> const& lock,
    MetaPeerPtr const& metaPeer,
    RequestId requestId,
    ::google::protobuf::Message const& message,
    protocol::MessageType requestType,
    protocol::MessageType responseType,
    Receiver* receiver,
    NetClock::duration timeout)
{
    auto peer = metaPeer->peer.lock();
    if (!peer)
    {
        // Peer is disconnected
        // and will call `disconnect` from its destructor.
        return 0;
    }
    // TODO: Add a factory method to Message for this pattern.
    // It will let us remove the template function `send_`.
    auto packet = std::make_shared<Message>(message, requestType);
    auto request = std::make_unique<Request>(
        requestId,
        responseType,
        metaPeer,
        receiver,
        Timer(io_service_),
        Clock::now());
    request->timer.expires_after(timeout);
    request->timer.async_wait(
        [this, requestId](boost::system::error_code const& error) {
            if (error == boost::asio::error::operation_aborted)
            {
                // Timer cancelled.
                return;
            }
            this->timeout_(requestId);
        });

    requests_.emplace(requestId, std::move(request));
    std::size_t nrequests = requests_.size();

    // REVIEW: `PeerImp::send` seems to have no way to indicate an error.
    // Can we assume it is always successful?
    // If not, then that will undermine the guarantees
    // that `MessageScheduler` makes to `Sender`s.
    peer->send(packet);
    ++metaPeer->nclosed;
    ++nclosed_;
    JLOG(journal_.debug()) << "send,id=" << requestId
                           << ",peerId=" << peer->id()
                           << ",inflight=" << nrequests;
    return requestId;
}

void
MessageScheduler::receive(
    std::shared_ptr<protocol::TMLedgerData> const& message)
{
    JLOG(journal_.debug()) << "receive,type=ledger_data,count="
                           << message->nodes().size();
    if (!message->has_requestcookie())
    {
        JLOG(journal_.warn()) << "LedgerData message missing request ID";
        return;
    }
    auto requestId = message->requestcookie();
    return receive_(requestId, protocolMessageType(*message), message);
}

void
MessageScheduler::receive(
    std::shared_ptr<protocol::TMGetObjectByHash> const& message)
{
    JLOG(journal_.debug()) << "receive,type=get_objects,count="
                           << message->objects().size();
    if (!message->has_seq())
    {
        JLOG(journal_.warn()) << "GetObjectByHash message missing request ID";
        return;
    }
    auto requestId = message->seq();
    return receive_(requestId, protocolMessageType(*message), message);
}

void
MessageScheduler::receive_(
    RequestId requestId,
    protocol::MessageType type,
    MessagePtr const& message)
{
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = requests_.find(requestId);
    if (it == requests_.end())
    {
        // Either we never requested this data,
        // or it took too long to arrive.
        JLOG(journal_.warn()) << "unknown request ID: " << requestId;
        return;
    }
    auto request = std::move(it->second);
    assert(request->id == requestId);

    if (request->responseType != type)
    {
        // Someone tried to fool us!
        JLOG(journal_.warn())
            << "wrong response type (" << type << ") for request " << requestId;
        // We will keep waiting for a correct response.
        return;
    }

    requests_.erase(it);

    // We cancel the timer here as a courtesy, ignoring the result.
    // See explanation in `disconnect()`.
    request->timer.cancel();

    if (auto stream = journal_.trace())
    {
        auto duration = Clock::now() - request->sent;
        auto duration_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(duration);
        stream << "receive,id=" << requestId
               << ",peer" << request->metaPeer->id
               << ",time=" << duration_ms.count()
               << "ms,size=" << message->ByteSizeLong()
               << ",inflight=" << requests_.size();
    }

    reopen(lock, "receive_", request->metaPeer, [&] {
        // Non-trivial callbacks should just schedule a job.
        request->receiver->onSuccess(requestId, message);
    });
}

void
MessageScheduler::timeout_(RequestId requestId)
{
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = requests_.find(requestId);
    if (it == requests_.end())
    {
        // The response already arrived.
        JLOG(journal_.warn()) << "unknown request ID: " << requestId;
        return;
    }
    auto request = std::move(it->second);
    assert(request->id == requestId);

    requests_.erase(it);

    if (auto stream = journal_.warn())
    {
        auto duration = Clock::now() - request->sent;
        auto duration_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(duration);
        stream << "timeout,id=" << requestId
            << ",peer" << request->metaPeer->id
            << ",time=" << duration_ms << "ms";
    }

    reopen(lock, "timeout_", request->metaPeer, [&] {
        // Non-trivial callbacks should just schedule a job.
        request->receiver->onFailure(request->id, FailureCode::TIMEOUT);
    });
}

void
MessageScheduler::reopen(
    std::lock_guard<std::mutex> const& lock,
    char const* caller,
    MetaPeer* metaPeer,
    std::function<void()> callback)
{
    assert(metaPeer);
    assert(metaPeer->nclosed > 0);
    --metaPeer->nclosed;
    assert(nclosed_ > 0);
    --nclosed_;

    push_value during_(during, caller);
    assert(!tsenders);
    SenderQueue senders;
    push_value tsenders_(tsenders, &senders);

    try
    {
        // Non-trivial callbacks should just schedule a job.
        // TODO: Warn when callbacks take too long.
        callback();
    }
    catch (...)
    {
        JLOG(journal_.error()) << "unhandled exception from callback";
    }

    if (!senders_.empty())
    {
        MetaPeerSet peers{metaPeer};
        negotiateNewPeers(lock, peers);
    }
    else if (!tsenders->empty())
    {
        negotiateNewSenders(lock);
    }
}

void
MessageScheduler::stop()
{
    push_value top(during, "stop");
    std::lock_guard<std::mutex> lock(mutex_);
    stopped_ = true;
    for (auto& [id, request] : requests_)
    {
        try
        {
            request->receiver->onFailure(id, FailureCode::SHUTDOWN);
        }
        catch (...)
        {
            JLOG(journal_.error())
                << "unhandled exception from onFailure(SHUTDOWN)";
        }
    }
    requests_.clear();
    for (auto& sender : senders_)
    {
        try
        {
            sender->onDiscard();
        }
        catch (...)
        {
            JLOG(journal_.error()) << "unhandled exception from onDiscard";
        }
    }
    senders_.clear();
}

}  // namespace ripple
