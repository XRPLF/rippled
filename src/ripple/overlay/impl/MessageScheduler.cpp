#include <ripple/basics/Log.h>
#include <ripple/basics/chrono.h>
#include <ripple/overlay/MessageScheduler.h>

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
// `schedule` or (b) `_offer`.
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
MessageScheduler::connect(std::vector<Channel> channels)
{
    if (channels.empty())
    {
        return;
    }
    std::lock_guard<std::mutex> lock(offersMutex_);
    if (!senders_.empty())
    {
        push_value top(caller, "connect");
        _offer(channels, senders_);
    }
    channels_.reserve(channels_.size() + channels.size());
    std::move(channels.begin(), channels.end(), std::back_inserter(channels_));
}

void
MessageScheduler::disconnect(Peer::id_t peerId)
{
    JLOG(journal_.trace()) << "disconnect,id=" << peerId;
    // We have to acquire both locks for this operation.
    // Always acquire the offers lock first.
    push_value top(caller, "disconnect");
    std::lock_guard<std::mutex> lock(offersMutex_);
    {
        auto end = std::remove_if(
            channels_.begin(), channels_.end(), [peerId](auto const& channel) {
                return channel.peerId == peerId;
            });
        channels_.erase(end, channels_.end());
    }
    std::vector<Sender*> senders;
    {
        push_value top(tsenders, &senders);
        std::lock_guard<std::mutex> lock(requestsMutex_);
        // C++20: Use `std::erase_if`.
        for (auto it = requests_.begin(); it != requests_.end();)
        {
            if (it->second->channel.peerId == peerId)
            {
                // This callback may add a sender to `senders`.
                try
                {
                    it->second->receiver->onFailure(
                        it->second->id, FailureCode::DISCONNECT);
                }
                catch (std::exception const&)
                {
                }
                it = requests_.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }
    if (!senders.empty())
    {
        if (!channels_.empty())
        {
            _offer(channels_, senders);
        }
        std::move(senders.begin(), senders.end(), std::back_inserter(senders_));
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
    std::lock_guard<std::mutex> lock(offersMutex_);
    if (stopped_)
    {
        return false;
    }
    std::vector<Sender*> senders = {sender};
    if (!channels_.empty())
    {
        push_value top(caller, "schedule");
        _offer(channels_, senders);
    }
    std::move(senders.begin(), senders.end(), std::back_inserter(senders_));
    return true;
}

RequestId
MessageScheduler::send(
    std::shared_ptr<Peer> peer,
    protocol::TMGetLedger& message,
    Receiver* receiver,
    NetClock::duration timeout)
{
    auto requestId = ++nextId_;
    message.set_requestcookie(requestId);
    _send(peer, requestId, message, receiver, timeout);
    return requestId;
}

RequestId
MessageScheduler::send(
    std::shared_ptr<Peer> peer,
    protocol::TMGetObjectByHash& message,
    Receiver* receiver,
    NetClock::duration timeout)
{
    auto requestId = ++nextId_;
    message.set_seq(requestId);
    JLOG(journal_.trace()) << "send,type=get_objects,count="
                           << message.objects_size();
    _send(peer, requestId, message, receiver, timeout);
    return requestId;
}

void
MessageScheduler::_offer(
    std::vector<Channel>& channels,
    std::vector<Sender*>& senders)
{
    JLOG(journal_.trace()) << "offer,during=" << caller
                           << ",channels=" << channels.size()
                           << ",senders=" << senders.size();
    assert(!channels.empty());
    assert(!senders.empty());
    push_value top1(tsenders, &senders);
    // We must iterate `senders` using an index
    // because the offer may grow the vector,
    // invalidating any iterators or references.
    for (auto i = 0; i < senders.size(); ++i)
    {
        // If this is the last sender, offer it the full set of channels.
        // If there are more senders waiting, offer one at a time, in turn.
        auto size = (i + 1 == senders.size()) ? channels.size() : 1;
        {
            Offer offer{*this, channels, size};
            try
            {
                senders[i]->onOffer(offer);
            }
            catch (std::exception const&)
            {
            }
            if (offer.closed())
            {
                senders[i] = nullptr;
            }
            // Closed channels are removed here,
            // in the destructor of the offer.
        }
        if (channels.empty())
        {
            break;
        }
    }
    auto end = std::remove(senders.begin(), senders.end(), nullptr);
    senders.erase(end, senders.end());
}

void
MessageScheduler::_send(
    std::shared_ptr<Peer> peer,
    RequestId requestId,
    ::google::protobuf::Message const& message,
    protocol::MessageType type,
    Receiver* receiver,
    NetClock::duration timeout)
{
    // TODO: Add a factory method to Message for this pattern.
    // It will let us remove the template function `_send`.
    auto packet = std::make_shared<Message>(message, type);
    // C++20: Switch to make_shared<Request>(...).
    // https://stackoverflow.com/a/55144743/618906
    auto request = std::unique_ptr<Request>(new Request{
        requestId,
        toChannel(peer),
        receiver,
        Timer(io_service_),
        Clock::now()});
    auto onTimer = [this, requestId](boost::system::error_code const& error) {
        if (error == boost::asio::error::operation_aborted)
            return;
        JLOG(journal_.trace()) << "timeout,id=" << requestId;
        push_value top(caller, "timeout");
        std::lock_guard<std::mutex> lock(offersMutex_);
        {
            push_value top(tsenders, &senders_);
            std::lock_guard<std::mutex> lock(requestsMutex_);
            // If a request is still around to be erased, then it did not
            // get a response. Move those request IDs to the front and erase
            // the rest.
            do
            {
                auto it = requests_.find(requestId);
                if (it == requests_.end())
                {
                    break;
                }
                auto channel = std::move(it->second->channel);
                try
                {
                    it->second->receiver->onFailure(
                        it->second->id, FailureCode::TIMEOUT);
                }
                catch (std::exception const&)
                {
                }
                requests_.erase(it);
                channels_.emplace_back(channel);
            } while (false);
        }
        if (!channels_.empty() && !senders_.empty())
        {
            _offer(channels_, senders_);
        }
    };
    {
        std::lock_guard<std::mutex> lock(requestsMutex_);
        request->timer.expires_after(timeout);
        request->timer.async_wait(std::move(onTimer));
        requests_.emplace(requestId, std::move(request));
    }
    peer->send(packet);
    JLOG(journal_.trace()) << "send,id=" << requestId << ",peer=" << peer->id();
}

void
MessageScheduler::receive(std::shared_ptr<protocol::TMLedgerData> message)
{
    if (!message->has_requestcookie())
    {
        JLOG(journal_.info()) << "LedgerData message missing request ID";
        return;
    }
    auto requestId = message->requestcookie();
    return _receive(requestId, message);
}

void
MessageScheduler::receive(std::shared_ptr<protocol::TMGetObjectByHash> message)
{
    JLOG(journal_.trace()) << "receive,type=get_objects,count="
                           << message->objects().size();
    if (!message->has_seq())
    {
        JLOG(journal_.info()) << "GetObjectByHash message missing request ID";
        return;
    }
    auto requestId = message->seq();
    return _receive(requestId, message);
}

void
MessageScheduler::_receive(
    RequestId requestId,
    std::shared_ptr<::google::protobuf::Message> message)
{
    push_value top(caller, "receive");
    std::vector<Sender*> senders;
    std::vector<Channel> channels;
    {
        // We push `tsenders` here to keep `onSuccess` callbacks from
        // trying to lock offers after requests have been locked.
        push_value top(tsenders, &senders);
        std::lock_guard<std::mutex> lock(requestsMutex_);
        auto it = requests_.find(requestId);
        if (it == requests_.end())
        {
            // Either we never requested this data,
            // or it took too long to arrive.
            JLOG(journal_.info()) << "unknown request ID: " << requestId;
            return;
        }
        assert(it->second->id == requestId);
        if (it->second->timer.cancel() < 1)
        {
            // Timer has already expired,
            // and the `onFailure` callback has or will be executed.
            return;
        }
        // TODO: Document assumption that callbacks are trivial.
        // Non-trivial callbacks should just schedule a job.
        if (auto stream = journal_.trace())
        {
            auto duration = Clock::now() - it->second->sent;
            auto duration_ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(duration);
            stream << "receive,id=" << requestId
                   << ",time=" << duration_ms.count()
                   << ",size=" << message->ByteSizeLong();
        }
        channels.emplace_back(std::move(it->second->channel));
        try
        {
            it->second->receiver->onSuccess(requestId, message);
        }
        catch (std::exception const&)
        {
        }
        requests_.erase(it);
    }
    assert(channels.size() == 1);
    {
        std::lock_guard<std::mutex> lock(offersMutex_);
        if (!senders_.empty())
        {
            // Offer the released channel to waiting senders.
            _offer(channels, senders_);
        }
        // If the channel was not closed, then add it to the pool.
        std::move(
            channels.begin(), channels.end(), std::back_inserter(channels_));
        if (!channels_.empty() && !senders.empty())
        {
            // Offer the waiting channels to new senders.
            _offer(channels_, senders);
        }
        // If any senders were unsatisfied, add them to the queue.
        std::move(senders.begin(), senders.end(), std::back_inserter(senders_));
    }
}

void
MessageScheduler::stop()
{
    std::lock_guard<std::mutex> lock1(offersMutex_);
    stopped_ = true;
    {
        std::lock_guard<std::mutex> lock2(requestsMutex_);
        for (auto& [id, request] : requests_)
        {
            try
            {
                request->receiver->onFailure(id, FailureCode::SHUTDOWN);
            }
            catch (std::exception const&)
            {
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
        catch (std::exception const&)
        {
        }
    }
    senders_.clear();
}

}  // namespace ripple
