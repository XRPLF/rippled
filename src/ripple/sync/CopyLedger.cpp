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

#include <ripple/sync/CopyLedger.h>

#include <ripple/app/main/Application.h>
#include <ripple/core/JobQueue.h>
#include <ripple/peerclient/Log.h>
#include <ripple/protocol/LedgerHeader.h>
#include <ripple/sync/ObjectRequester.h>

namespace ripple {
namespace sync {

void
CopyLedger::start_()
{
    JLOG(journal_.info()) << digest_ << " start";
    {
        // TODO: Should start with two GET_LEDGER requests for the top
        // 3 levels of the account and transaction trees.
        ObjectRequester orequester{*this};
        // Remember: this calls `schedule` if we are not yet scheduled.
        orequester.request(digest_);
    }

    // TODO: Technically there is a chance that the ledger is completely
    // loaded here, without ever requesting a single object,
    // which means `receive` is never called.
    // In practice, that is virtually guaranteed to never happen,
    // but we should handle the possibility anyway.
}

void
CopyLedger::schedule()
{
    if (!mscheduler_.schedule(this))
    {
        // TODO: Handle shutdown.
    }
}

void
CopyLedger::send(RequestPtr&& request)
{
    auto queue = (request->objects_size() < MAX_OBJECTS_PER_MESSAGE)
        ? &CopyLedger::partialRequests_
        : &CopyLedger::fullRequests_;
    // This is the value we want it to be.
    bool scheduled = true;
    {
        std::lock_guard lock(senderMutex_);
        (this->*queue).emplace_back(std::move(request));
        std::swap(scheduled, scheduled_);
        // Now it is the value it was.
    }
    if (!scheduled)
    {
        schedule();
    }
}

CopyLedger::RequestPtr
CopyLedger::unsend()
{
    RequestPtr request = nullptr;
    std::size_t before = 0;
    std::size_t after = 0;
    {
        std::lock_guard lock(senderMutex_);
        before = partialRequests_.size();
        auto& requests = partialRequests_;
        // Claw back the last non-full request.
        if (!requests.empty())
        {
            request = std::move(requests.back());
            requests.pop_back();
        }
        after = partialRequests_.size();
    }
    return request;
}

void
CopyLedger::onReady(MessageScheduler::Courier& courier)
{
    struct Receiver : public MessageScheduler::Receiver
    {
        CopyLedger& copier_;
        RequestPtr request_;

        Receiver(CopyLedger& copier, RequestPtr&& request)
            : copier_(copier), request_(std::move(request))
        {
        }

        void
        onSuccess(MessageScheduler::RequestId, ResponsePtr const& response)
            override
        {
            auto receiver = std::shared_ptr<Receiver>(this);
            copier_.jscheduler_.schedule([receiver = std::move(receiver),
                                          response = std::move(response)]() {
                receiver->copier_.receive(
                    std::move(receiver->request_), std::move(response));
            });
        }

        void
        onFailure(
            MessageScheduler::RequestId,
            MessageScheduler::FailureCode code) override
        {
            auto receiver = std::shared_ptr<Receiver>(this);
            JLOG(copier_.journal_.warn())
                << copier_.digest_ << " onFailure " << code;
            copier_.jscheduler_.schedule(
                [receiver = std::move(receiver)]() mutable {
                    // Re-send the request.
                    receiver->copier_.send(std::move(receiver->request_));
                });
        }
    };

    // Take the first non-empty queue in the sequence [fulls, partials].
    // Do not change the value of `scheduled_`, which must be `true`.
    // There is a chance that, by the time this method returns,
    // more requests have been added to one or the other queue,
    // but `MessageScheduler::schedule` will not have been called.
    auto queue = &CopyLedger::fullRequests_;
    std::vector<RequestPtr> requests;
    std::size_t remaining = 0;
    {
        std::lock_guard lock(senderMutex_);
        assert(scheduled_);
        remaining = partialRequests_.size() + fullRequests_.size();
        if (fullRequests_.empty())
        {
            queue = &CopyLedger::partialRequests_;
        }
        requests.swap(this->*queue);
    }
    // TODO: If holding multiple partial requests,
    // now is a good time to merge them.

    assert(courier.closed() == 0);
    assert(courier.limit() > 0);

    auto blaster = Blaster(courier);
    auto requesti = requests.begin();
    while (requesti != requests.end() && blaster)
    {
        auto& requestp = *requesti;
        auto& request = *requestp;
        auto receiver = std::make_unique<Receiver>(*this, std::move(requestp));
        using namespace std::chrono_literals;
        // Compute timeout from size of request.
        double n = request.objects_size();
        // TODO: Move these constants to configuration variables.
        auto timeout = 1s + (n / MAX_OBJECTS_PER_MESSAGE) * 59s;
        if (blaster.send(request, receiver.get(), timeout))
        {
            receiver.release();
            ++requesti;
        }
        else
        {
            requestp = std::move(receiver->request_);
        }
    }
    auto end = std::remove(requests.begin(), requests.end(), nullptr);
    requests.erase(end, requests.end());

    // At this point we are either out of requests or out of peers.
    // If we are out of peers but there are more requests,
    // then we must request another offer.
    // If we are out of requests, but there are more peers,
    // then we don't care.

    // `scheduled_` must be true for this method to have been called.
    // If there are no more requests, then we should set it to false and exit.
    // If there are more requests, then we should leave it true and call
    // `MessageScheduler::schedule`.
    remaining = 0;
    {
        std::lock_guard lock(senderMutex_);
        assert(scheduled_);
        std::move(
            requests.begin(), requests.end(), std::back_inserter(this->*queue));
        remaining = partialRequests_.size() + fullRequests_.size();
        scheduled_ = remaining;
    }
    if (remaining)
    {
        if (courier.evicting())
        {
            schedule();
        }
    }
    else
    {
        // Make sure we leave the sender queue.
        courier.withdraw();
    }
}

void
CopyLedger::receive(RequestPtr&& request, ResponsePtr const& response)
{
    auto descriptor = response->GetDescriptor();
    auto const& type = descriptor->name();
    if (type == "TMGetObjectByHash")
        return receive(
            std::move(request),
            static_cast<protocol::TMGetObjectByHash&>(*response));
    JLOG(journal_.warn()) << digest_ << " ignoring unknown message type " << type;
}

void
CopyLedger::receive(RequestPtr&& request, protocol::TMGetObjectByHash& response)
{
    {
        ObjectRequester orequester{*this};
        orequester.receive(request, response);
    }

    std::size_t received = 0;
    std::size_t requested = 0;
    {
        std::lock_guard lock(metricsMutex_);
        receiveMeter_.addMessage(response.ByteSizeLong());
        JLOG(journal_.trace()) << "download: " << receiveMeter_;
        received = metrics_.received();
        requested = metrics_.requested;
    }

    JLOG(journal_.trace()) << "requested = received + pending: " << requested
                           << " = " << received << " + "
                           << (requested - received);
    if (received < requested)
    {
        return;
    }
    assert(received == requested);

    // Only need header and root nodes.
    auto objHeader = objectDatabase_.fetchNodeObject(digest_);
    assert(objHeader);
    auto header =
        deserializePrefixedHeader(objHeader->getData(), /*hasHash=*/false);
    header.hash = digest_;
    auto ledger =
        std::make_shared<Ledger>(header, app_.config(), app_.getNodeFamily());

    auto djTxRoot = header.txHash;
    auto objTxRoot = objectDatabase_.fetchNodeObject(djTxRoot);
    assert(objTxRoot);
    auto nodeTxRoot = SHAMapTreeNode::makeFromPrefix(
        objTxRoot->getData(), SHAMapHash(djTxRoot));
    assert(nodeTxRoot);
    app_.getNodeFamily()
        .getTreeNodeCache(header.seq)
        ->canonicalize_replace_client(djTxRoot, nodeTxRoot);
    ledger->txMap().setRootNode(nodeTxRoot);

    auto djStRoot = header.accountHash;
    auto objStRoot = objectDatabase_.fetchNodeObject(djStRoot);
    assert(objStRoot);
    auto nodeStRoot = SHAMapTreeNode::makeFromPrefix(
        objStRoot->getData(), SHAMapHash(djStRoot));
    assert(nodeStRoot);
    app_.getNodeFamily()
        .getTreeNodeCache(header.seq)
        ->canonicalize_replace_client(djStRoot, nodeStRoot);
    ledger->stateMap().setRootNode(nodeStRoot);

    ledger->setFull();
    ledger->txMap().clearSynching();
    ledger->stateMap().clearSynching();
    ledger->setImmutable(/*rehash=*/false);
    JLOG(journal_.info()) << digest_ << " finish";
    return return_(std::move(ledger));
}

}  // namespace sync
}  // namespace ripple
