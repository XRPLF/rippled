#include <ripple/basics/Log.h>
#include <ripple/overlay/PeerScheduler.h>
#include <mutex>

namespace ripple {

using RequestId = PeerScheduler::RequestId;

thread_local std::vector<PeerScheduler::Client>* PeerScheduler::newClients_ =
    nullptr;

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
PeerScheduler::add(std::vector<WeakPeer> peers)
{
    if (peers.empty())
    {
        return;
    }
    std::lock_guard<std::mutex> lock(offersMutex_);
    if (!clients_.empty())
    {
        push_value top(newClients_, &clients_);
        _offer(peers, clients_);
    }
    peers_.reserve(peers_.size() + peers.size());
    std::move(peers.begin(), peers.end(), std::back_inserter(peers_));
}

void
PeerScheduler::remove(Peer::id_t peerId)
{
    // We have to acquire both locks for this operation.
    // Always acquire the offers lock first.
    std::lock_guard<std::mutex> lock1(offersMutex_);
    {
        auto end = std::remove_if(
            peers_.begin(), peers_.end(), [peerId](auto const& peer) {
                return peer.id == peerId;
            });
        peers_.erase(end, peers_.end());
    }
    std::vector<Client> clients;
    push_value top(newClients_, &clients);
    {
        std::lock_guard<std::mutex> lock2(requestsMutex_);
        // C++20: Use `std::erase_if`.
        for (auto it = requests_.begin(); it != requests_.end();)
        {
            if (it->second->peer.id == peerId)
            {
                // This callback may add a client to `clients`.
                it->second->onFailure(FailureCode::DISCONNECT);
                it = requests_.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }
    if (!clients.empty())
    {
        if (!peers_.empty())
        {
            _offer(peers_, clients);
        }
        std::move(clients.begin(), clients.end(), std::back_inserter(clients_));
    }
}

void
PeerScheduler::schedule(Client client)
{
    JLOG(journal_.trace()) << "PeerScheduler.schedule";
    if (newClients_)
    {
        // The scheduler is already locked in this thread.
        // Save new clients to be served later.
        // TODO: Insert in priority order.
        newClients_->emplace_back(std::move(client));
        return;
    }
    std::lock_guard<std::mutex> lock(offersMutex_);
    std::vector<Client> clients{client};
    if (!peers_.empty())
    {
        _offer(peers_, clients);
    }
    std::move(clients.begin(), clients.end(), std::back_inserter(clients_));
}

RequestId
PeerScheduler::send(
    std::shared_ptr<Peer> peer,
    protocol::TMGetLedger& message,
    SuccessCallback onSuccess,
    FailureCallback onFailure)
{
    // REVIEWER: Do we want random request IDs? Responses should be signed by
    // the peer, so we shouldn't have to worry about fake responses.
    auto requestId = ++nextId_;
    message.set_requestcookie(requestId);
    _send(peer, requestId, message, std::move(onSuccess), std::move(onFailure));
    return requestId;
}

RequestId
PeerScheduler::send(
    std::shared_ptr<Peer> peer,
    protocol::TMGetObjectByHash& message,
    SuccessCallback onSuccess,
    FailureCallback onFailure)
{
    auto requestId = ++nextId_;
    message.set_seq(requestId);
    _send(peer, requestId, message, std::move(onSuccess), std::move(onFailure));
    return requestId;
}

void
PeerScheduler::_offer(
    std::vector<WeakPeer>& peers,
    std::vector<Client>& clients)
{
    assert(!peers.empty());
    assert(!clients.empty());
    for (auto i = 0; i < clients.size(); ++i)
    {
        auto& client = clients[i];
        // If this is the last client, offer it the full set of peers.
        // If there are more clients waiting, offer one at a time, in turn.
        auto supply = (i + 1 == clients.size()) ? peers.size() : 1;
        PeerOffer offer{*this, peers, supply};
        push_value top(newClients_, &clients);
        client(offer);
        if (offer.consumed())
        {
            client = nullptr;
        }
        if (peers.empty())
        {
            break;
        }
    }
    auto end = std::remove(clients.begin(), clients.end(), nullptr);
    clients.erase(end, clients.end());
}

void
PeerScheduler::_send(
    std::shared_ptr<Peer> peer,
    RequestId requestId,
    ::google::protobuf::Message const& message,
    protocol::MessageType type,
    SuccessCallback&& onSuccess,
    FailureCallback&& onFailure)
{
    auto packet = std::make_shared<Message>(message, type);
    // C++20: Switch to make_unique<Request>(...).
    // https://stackoverflow.com/a/55144743/618906
    auto request = std::unique_ptr<Request>(new Request{
        requestId,
        toWeakPeer(peer),
        std::move(onSuccess),
        std::move(onFailure)});
    {
        std::lock_guard<std::mutex> lock(requestsMutex_);
        requests_.emplace(requestId, std::move(request));
    }
    // REVIEWER: Should we send before or after bookkeeping?
    peer->send(packet);
    JLOG(journal_.trace()) << "send," << requestId;
}

void
PeerScheduler::timeout(
    std::vector<RequestId> requestIds,
    NetClock::duration timeout)
{
    auto timerId = ++nextId_;
    // We must declare that the return type is an lvalue reference,
    // or else it will be deduced as an rvalue reference.
    auto& timer = [&]() -> auto&
    {
        auto timer = std::make_unique<Timer>(io_service_);
        auto& ref = *timer;
        {
            std::lock_guard<std::mutex> lock(requestsMutex_);
            timers_.emplace(timerId, std::move(timer));
        }
        return ref;
    }
    ();
    timer.expires_after(timeout);
    timer.async_wait([this, timerId, requestIds = std::move(requestIds)](
                         boost::system::error_code const& error) mutable {
        if (error == boost::asio::error::operation_aborted)
            return;
        JLOG(journal_.trace()) << "timer.stop," << timerId;
        std::lock_guard<std::mutex> lock(offersMutex_);
        push_value top(newClients_, &clients_);
        {
            std::lock_guard<std::mutex> lock(requestsMutex_);
            // REVIEWER: Safe for callback to delete the timer that called it?
            timers_.erase(timerId);
            // If a request is still around to be erased, then it did not
            // get a response. Move those request IDs to the front and erase
            // the rest.
            for (auto requestId : requestIds)
            {
                auto it = requests_.find(requestId);
                if (it == requests_.end())
                {
                    continue;
                }
                auto request = std::move(it->second);
                requests_.erase(it);
                request->onFailure(FailureCode::TIMEOUT);
                peers_.emplace_back(std::move(request->peer));
            }
        }
        _offer(peers_, clients_);
    });
    JLOG(journal_.trace()) << "timer.start," << timerId;
}

void
PeerScheduler::receive(std::shared_ptr<protocol::TMLedgerData> message)
{
    if (!message->has_requestcookie())
    {
        JLOG(journal_.warn()) << "LedgerData message missing request ID";
        return;
    }
    auto requestId = message->requestcookie();
    return _receive(requestId, message);
}

void
PeerScheduler::receive(std::shared_ptr<protocol::TMGetObjectByHash> message)
{
    if (!message->has_seq())
    {
        JLOG(journal_.warn()) << "LedgerData message missing request ID";
        return;
    }
    auto requestId = message->seq();
    return _receive(requestId, message);
}

void
PeerScheduler::_receive(
    RequestId requestId,
    std::shared_ptr<::google::protobuf::Message> message)
{
    JLOG(journal_.trace()) << "receive," << requestId;
    std::vector<Client> clients;
    push_value top(newClients_, &clients);
    {
        std::lock_guard<std::mutex> lock(requestsMutex_);
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
        // TODO: Document assumption that callbacks are trivial.
        // Non-trivial callbacks should just schedule a job.
        request->onSuccess(message);
    }
    if (!clients.empty())
    {
        std::lock_guard<std::mutex> lock(offersMutex_);
        if (!peers_.empty())
        {
            _offer(peers_, clients);
        }
        std::move(clients.begin(), clients.end(), std::back_inserter(clients_));
    }
}

}  // namespace ripple
