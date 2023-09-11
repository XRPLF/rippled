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
#include <ripple/protocol/LedgerHeader.h>
#include <ripple/protocol/digest.h>
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
    // loaded here. In practice, that is virtually guaranteed to never happen,
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
    // This is the value we want it to be.
    bool scheduled = true;
    {
        std::lock_guard lock(senderMutex_);
        auto queue = (request->objects_size() < MAX_OBJECTS_PER_MESSAGE)
            ? &CopyLedger::partialRequests_
            : &CopyLedger::fullRequests_;
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
    RequestPtr request;
    std::lock_guard lock(senderMutex_);
    auto& requests = partialRequests_;
    // Claw back the last pending request if it has room.
    if (!requests.empty())
    {
        request = std::move(requests.back());
        requests.pop_back();
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

    auto queue = &CopyLedger::fullRequests_;
    std::vector<RequestPtr> requests;
    {
        std::lock_guard lock(senderMutex_);
        if (fullRequests_.empty())
        {
            queue = &CopyLedger::partialRequests_;
        }
        requests.swap(this->*queue);
    }
    // TODO: If holding multiple partial requests,
    // now is a good time to merge them.

    JLOG(journal_.trace()) << "onReady,enter,closed=" << (int)courier.closed()
                           << "/" << (int)courier.limit()
                           << ",requests=" << requests.size();

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
    std::size_t remaining = 0;
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
        if (courier.closed())
        {
            schedule();
        }
    }
    else
    {
        courier.withdraw();
    }

    JLOG(journal_.trace()) << "onReady,exit,closed=" << (int)courier.closed()
                           << "/" << (int)courier.limit()
                           << ",requests=" << remaining;
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
    JLOG(journal_.warn()) << digest_ << " unknown message type " << type;
}

void
CopyLedger::receive(RequestPtr&& request, protocol::TMGetObjectByHash& response)
{
    {
        std::size_t requested = request->objects_size();
        std::size_t returned = response.objects_size();
        if (requested != returned)
        {
            JLOG(journal_.warn())
                << digest_ << " missing " << requested - returned;
        }
    }

    std::size_t received = 0;
    std::size_t written = 0;
    {
        ObjectRequester orequester{*this};
        // `i` is the index in the request. `j` is the index in the response.
        int i = 0;
        for (int j = 0; j < response.objects_size(); ++j)
        {
            auto const& object = response.objects(j);

            // For these first two tests,
            // we can't even be sure what object was requested,
            // so there is no fix.

            if (!object.has_hash())
            {
                JLOG(journal_.warn()) << digest_ << " object is missing digest";
                continue;
            }

            if (object.hash().size() != ObjectDigest::size())
            {
                JLOG(journal_.warn()) << digest_ << " digest is wrong size";
                continue;
            }

            // We assume the response holds a subset of the objects requested,
            // and that objects appear in the response in the same order as
            // their digests appear in the request.
            // Thus, if this object in the response does not match the next
            // object requested, then we conclude the requested object is
            // missing from the response, and repeat until we find a match.
            while (true)
            {
                if (i >= request->objects_size())
                {
                    // The rest of the objects in this response are
                    // unrequested.
                    JLOG(journal_.warn()) << digest_ << " unrequested objects "
                                          << response.objects_size() - j;
                    // Break out of the outer loop,
                    // past the point where we finish iterating the request.
                    goto end_of_request;
                }
                auto const& ihash = request->objects(i).hash();
                ++i;
                if (ihash == object.hash())
                {
                    break;
                }
                auto idigest = ObjectDigest(ihash);
                JLOG(journal_.warn())
                    << digest_ << " missing object " << idigest;
                orequester.rerequest(idigest);
            }

            // For the remaining tests,
            // if they fail,
            // then we should request the object again
            // (from a different peer).

            // This copies. Sad.
            auto digest = ObjectDigest(object.hash());

            if (!object.has_data())
            {
                JLOG(journal_.warn()) << "missing data: " << digest;
                orequester.rerequest(digest);
                continue;
            }

            auto slice = makeSlice(object.data());

            // REVIEWER: Can we get rid of this expensive check?
            if (digest != sha512Half(slice))
            {
                JLOG(journal_.warn()) << "wrong digest";
                orequester.rerequest(digest);
                continue;
            }

            ++received;

            orequester.deserialize(digest, slice);

            NodeObjectType type = NodeObjectType::hotUNKNOWN;
            Blob blob(slice.begin(), slice.end());
            std::uint32_t ledgerSeq = 0;
            objectDatabase_.store(type, std::move(blob), digest, ledgerSeq);
            ++written;
        }

        if (auto remaining = request->objects_size() - i)
        {
            JLOG(journal_.info()) << "still missing: " << remaining;
        }
        for (; i < request->objects_size(); ++i)
        {
            orequester.rerequest(ObjectDigest(request->objects(i).hash()));
        }

    end_of_request:;
    }

    std::size_t requested = 0;
    {
        std::lock_guard lock(metricsMutex_);
        receiveMeter_.addMessage(response.ByteSizeLong());
        JLOG(journal_.trace()) << "download: " << receiveMeter_;
        received_ += received;
        written_ += written;
        requested = requested_;
        received = received_;
    }

    JLOG(journal_.trace()) << "requested = received + remaining: " << requested
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
