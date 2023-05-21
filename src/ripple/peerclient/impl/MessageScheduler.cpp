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
thread_local static std::vector<MessageScheduler::Sender*>* tsenders = nullptr;
// `caller` is logged whenever a sender is scheduled or a channel is offered.
// It names the method that called either (a) the callback that called
// `schedule` or (b) `ready_`.
// The (b) case can be solved with a parameter (to `offer_`), but the (a) case
// cannot, and if we're using a thread-local variable for the (a) case anyway,
// it doesn't hurt anything to use it for the (b) case too.
thread_local static char const* caller = "none";

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
    std::lock_guard<std::mutex> sendLock(sendMutex_);
    auto id = peer->id();
    auto metaPeer =
        peerList_.insert(peerList_.end(), {peer, id, nchannels, /*closed=*/0});
    // Peers connect in different threads.
    // They are assigned IDs in increasing order,
    // but they may not call this method in that order.
    // We must place them into `peers_` in order of ID.
    auto it = std::find_if(peers_.begin(), peers_.end(), [id](auto& metaPeer) {
        return metaPeer->id > id;
    });
    peers_.insert(it, metaPeer);
    nchannels_ += nchannels;
    if (!senders_.empty())
    {
        push_value top(caller, "connect");
        MetaPeerSet peers{metaPeer};
        ready_(sendLock, peers, senders_);
    }
}

void
MessageScheduler::disconnect(Peer::id_t peerId)
{
    JLOG(journal_.warn()) << "disconnect,id=" << peerId;
    // We have to acquire both locks for this operation.
    // Always acquire the offers lock first.
    push_value top(caller, "disconnect");
    std::lock_guard<std::mutex> sendLock(sendMutex_);
    {
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
        nclosed_ -= metaPeer->nclosed;
        nchannels_ -= metaPeer->nchannels;
        peers_.erase(it);
        peerList_.erase(metaPeer);
    }
    std::vector<Sender*> senders;
    {
        // Fail all pending requests with reason `DISCONNECT`.
        push_value top(tsenders, &senders);
        std::lock_guard<std::mutex> receiveLock(receiveMutex_);
        // C++20: Use `std::erase_if`.
        for (auto it = requests_.begin(); it != requests_.end();)
        {
            auto& request = it->second;
            if (request->metaPeer->id == peerId)
            {
                // This callback may add a sender to `senders`.
                try
                {
                    request->receiver->onFailure(
                        request->id, FailureCode::DISCONNECT);
                }
                catch (...)
                {
                    JLOG(journal_.error())
                        << "unhandled exception from onFailure(DISCONNECT)";
                }
                it = requests_.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }
    // If failing those requests tried to add any new senders,
    // then add them now.
    if (!senders.empty())
    {
        schedule_(sendLock, senders);
    }
}

bool
MessageScheduler::schedule(Sender* sender)
{
    JLOG(journal_.trace()) << "schedule,during=" << caller;
    if (tsenders)
    {
        // The scheduler is already locked in this thread.
        // Save new senders to be served later.
        // TODO: Insert in priority order.
        tsenders->emplace_back(sender);
        return true;
    }
    std::lock_guard<std::mutex> sendLock(sendMutex_);
    push_value top(caller, "schedule");
    std::vector<Sender*> senders = {sender};
    return schedule_(sendLock, senders);
}

bool
MessageScheduler::schedule_(
    std::lock_guard<std::mutex> const& sendLock,
    std::vector<Sender*>& senders)
{
    if (stopped_)
    {
        return false;
    }
    if (hasOpenChannels())
    {
        ready_(sendLock, peers_, senders);
    }
    std::move(senders.begin(), senders.end(), std::back_inserter(senders_));
    return true;
}

RequestId
MessageScheduler::send(
    std::lock_guard<std::mutex> const& sendLock,
    MetaPeerPtr const& metaPeer,
    protocol::TMGetLedger& message,
    Receiver* receiver,
    NetClock::duration timeout)
{
    if (metaPeer->isClosed())
    {
        // No open channels.
        return 0;
    }
    auto requestId = nextId_();
    message.set_requestcookie(requestId);
    JLOG(journal_.trace()) << "send,type=get_ledger";
    return send_(
        sendLock,
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
    std::lock_guard<std::mutex> const& sendLock,
    MetaPeerPtr const& metaPeer,
    protocol::TMGetObjectByHash& message,
    Receiver* receiver,
    NetClock::duration timeout)
{
    if (metaPeer->isClosed())
    {
        // No open channels.
        return 0;
    }
    auto requestId = nextId_();
    message.set_query(true);
    message.set_seq(requestId);
    JLOG(journal_.trace()) << "send,type=get_objects,count="
                           << message.objects_size();
    return send_(
        sendLock,
        metaPeer,
        requestId,
        message,
        protocol::mtGET_OBJECTS,
        protocol::mtGET_OBJECTS,
        receiver,
        timeout);
}

void
MessageScheduler::ready_(
    std::lock_guard<std::mutex> const& sendLock,
    MetaPeerSet const& peers,
    std::vector<Sender*>& senders)
{
    JLOG(journal_.trace()) << "ready_,during=" << caller
                           << ",peers=" << peers.size()
                           << ",senders=" << senders.size();
    assert(!peers.empty());
    assert(!senders.empty());
    assert(nclosed_ < nchannels_);
    push_value top1(tsenders, &senders);
    // We must iterate `senders` using an index
    // because the offer may grow the vector,
    // invalidating any iterators or references.
    for (auto i = 0; i < senders.size() && hasOpenChannels(); ++i)
    {
        // If this is the last sender, offer it the full set of open channels.
        // If there are more senders waiting, offer one at a time, in turn.
        ChannelCnt limit =
            (i + 1 == senders.size()) ? (nchannels_ - nclosed_) : 1;
        {
            Courier courier{*this, sendLock, peers, limit};
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
        }
    }
    auto end = std::remove(senders.begin(), senders.end(), nullptr);
    senders.erase(end, senders.end());
}

RequestId
MessageScheduler::send_(
    std::lock_guard<std::mutex> const& sendLock,
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
        return 0;
    }
    // TODO: Add a factory method to Message for this pattern.
    // It will let us remove the template function `send_`.
    auto packet = std::make_shared<Message>(message, requestType);
    // C++20: Switch to make_unique<Request>(...)?
    // https://stackoverflow.com/a/55144743/618906
    auto request = std::unique_ptr<Request>(new Request{
        requestId,
        responseType,
        metaPeer,
        receiver,
        Timer(io_service_),
        Clock::now()});
    auto onTimer = [this, requestId](boost::system::error_code const& error) {
        if (error == boost::asio::error::operation_aborted)
            return;
        JLOG(journal_.trace()) << "timeout,id=" << requestId;
        push_value top(caller, "timeout");
        std::lock_guard<std::mutex> sendLock(sendMutex_);
        {
            push_value top(tsenders, &senders_);
            std::lock_guard<std::mutex> receiveLock(receiveMutex_);
            // If a request is still around to be erased, then it did not
            // get a response. Move those request IDs to the front and erase
            // the rest.
            auto it = requests_.find(requestId);
            if (it != requests_.end())
            {
                auto& request = it->second;
                --request->metaPeer->nclosed;
                --nclosed_;
                try
                {
                    request->receiver->onFailure(
                        request->id, FailureCode::TIMEOUT);
                }
                catch (...)
                {
                    JLOG(journal_.error())
                        << "unhandled exception from onFailure(TIMEOUT)";
                }
                requests_.erase(it);
            }
        }
        if (hasOpenChannels() && !senders_.empty())
        {
            ready_(sendLock, peers_, senders_);
        }
    };
    std::size_t nrequests;
    {
        std::lock_guard<std::mutex> receiveLock(receiveMutex_);
        request->timer.expires_after(timeout);
        request->timer.async_wait(std::move(onTimer));
        requests_.emplace(requestId, std::move(request));
        nrequests = requests_.size();
    }
    // REVIEW: Do we need to wrap this in a try-catch?
    peer->send(packet);
    ++metaPeer->nclosed;
    ++nclosed_;
    JLOG(journal_.trace()) << "send,id=" << requestId
                           << ",peerId=" << peer->id()
                           << ",inflight=" << nrequests;
    return requestId;
}

void
MessageScheduler::receive(
    std::shared_ptr<protocol::TMLedgerData> const& message)
{
    JLOG(journal_.trace()) << "receive,type=ledger_data,count="
                           << message->nodes().size();
    if (!message->has_requestcookie())
    {
        JLOG(journal_.info()) << "LedgerData message missing request ID";
        return;
    }
    auto requestId = message->requestcookie();
    return receive_(requestId, protocolMessageType(*message), message);
}

void
MessageScheduler::receive(
    std::shared_ptr<protocol::TMGetObjectByHash> const& message)
{
    JLOG(journal_.trace()) << "receive,type=get_objects,count="
                           << message->objects().size();
    if (!message->has_seq())
    {
        JLOG(journal_.info()) << "GetObjectByHash message missing request ID";
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
    push_value top(caller, "receive");
    std::vector<Sender*> senders;
    MetaPeerSet peers;
    {
        // We push `tsenders` here to keep `onSuccess` callbacks from
        // trying to lock offers after requests have been locked.
        push_value top(tsenders, &senders);
        std::lock_guard<std::mutex> receiveLock(receiveMutex_);
        auto it = requests_.find(requestId);
        if (it == requests_.end())
        {
            // Either we never requested this data,
            // or it took too long to arrive.
            JLOG(journal_.warn()) << "unknown request ID: " << requestId;
            return;
        }
        auto request = std::move(it->second);
        requests_.erase(it);
        assert(request->id == requestId);
        if (request->responseType != type)
        {
            // Someone has tried to fool us!
            JLOG(journal_.warn()) << "wrong response type (" << type
                                  << ") for request " << requestId;
            return;
        }
        if (request->timer.cancel() < 1)
        {
            // Timer has already expired,
            // and the `onFailure` callback has been or will be called.
            return;
        }
        if (auto stream = journal_.trace())
        {
            auto duration = Clock::now() - request->sent;
            auto duration_ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(duration);
            stream << "receive,id=" << requestId
                   << ",time=" << duration_ms.count()
                   << "ms,size=" << message->ByteSizeLong()
                   << ",inflight=" << requests_.size();
        }
        peers.emplace_back(std::move(request->metaPeer));
        try
        {
            // Non-trivial callbacks should just schedule a job.
            request->receiver->onSuccess(requestId, message);
        }
        catch (...)
        {
            JLOG(journal_.error()) << "unhandled exception from onSucess";
        }
    }
    assert(peers.size() == 1);
    {
        std::lock_guard<std::mutex> sendLock(sendMutex_);
        --nclosed_;
        --peers.front()->nclosed;
        if (!senders_.empty())
        {
            // Offer the opened channel to waiting senders.
            ready_(sendLock, peers, senders_);
        }
        if (hasOpenChannels() && !senders.empty())
        {
            // Offer the waiting channels to new senders.
            ready_(sendLock, peers_, senders);
        }
        // If any senders were unsatisfied, add them to the queue.
        std::move(senders.begin(), senders.end(), std::back_inserter(senders_));
    }
}

void
MessageScheduler::stop()
{
    std::lock_guard<std::mutex> lock1(sendMutex_);
    stopped_ = true;
    {
        std::lock_guard<std::mutex> lock2(receiveMutex_);
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
    }
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
