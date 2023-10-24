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

#ifndef RIPPLE_SYNC_PEERLEDGERGETTER_H_INCLUDED
#define RIPPLE_SYNC_PEERLEDGERGETTER_H_INCLUDED

#include <ripple/app/ledger/Ledger.h>
#include <ripple/app/main/Application.h>
#include <ripple/basics/Coroutine.h>
#include <ripple/beast/utility/Journal.h>
#include <ripple/core/JobScheduler.h>
#include <ripple/ledger/LedgerIdentifier.h>
#include <ripple/nodestore/Database.h>
#include <ripple/peerclient/CommunicationMeter.h>
#include <ripple/peerclient/MessageScheduler.h>
#include <ripple/protocol/messages.h>

#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace ripple {
namespace sync {

/**
 * Copy one ledger, downloading missing pieces from peers.
 */
class CopyLedger : public Coroutine<ConstLedgerPtr>,
                   public MessageScheduler::Sender
{
private:
    using Request = protocol::TMGetObjectByHash;
    using RequestPtr = std::unique_ptr<Request>;
    using ResponsePtr = MessagePtr;

    // TODO: Move to configuration.
    constexpr static std::size_t MAX_OBJECTS_PER_MESSAGE = 20'000;

    // Dependencies.
    beast::Journal journal_;
    JobScheduler& jscheduler_;
    NodeStore::Database& objectDatabase_;
    MessageScheduler& mscheduler_;
    Application& app_;

    // Inputs.
    LedgerDigest digest_;

    // Request queue.
    std::mutex senderMutex_;
    std::vector<RequestPtr> partialRequests_;
    std::vector<RequestPtr> fullRequests_;
    bool scheduled_ = false;

    // Metrics.
    std::mutex metricsMutex_;
    CommunicationMeter receiveMeter_;
    std::size_t requested_ = 0;
    std::size_t received_ = 0;
    std::size_t written_ = 0;

    friend class ObjectRequester;

public:
    CopyLedger(
        Application& app,
        JobScheduler& jscheduler,
        LedgerDigest&& digest)
        : Coroutine<ConstLedgerPtr>(jscheduler)
        , journal_(app.journal("CopyLedger"))
        , jscheduler_(jscheduler)
        , objectDatabase_(app.getNodeStore())
        , mscheduler_(app.getMessageScheduler())
        // TODO: Remove references to `app_`.
        , app_(app)
        , digest_(std::move(digest))
    {
    }

private:
    void
    start_() override;

    void
    schedule();

    /** Add a request to the queue and schedule this sender.
     *
     * Adds the request to either the partial or full queue based on its size.
     * Calls `schedule` only if this sender is not already scheduled.
     */
    void
    send(RequestPtr&& request);

    /** Remove and return one non-full request, if any, from the queue.
     *
     * @return a non-full request or `nullptr`.
     */
    RequestPtr
    unsend();

    void
    receive(RequestPtr&& request, ResponsePtr const& response);
    void
    receive(RequestPtr&& request, protocol::TMGetObjectByHash& response);

public:
    void
    onReady(MessageScheduler::Courier& courier) override;

    void
    onDiscard() override
    {
        // TODO: Handle lifetime.
    }
};

}  // namespace sync
}  // namespace ripple

#endif
