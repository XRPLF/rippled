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
public:
    /**
     * A set of numbers that we track both
     * (a) individually for each step in the algorithm
     * (represented by methods `start` and `receive`) and
     * (b) collectively across all steps in the algorithm.
     *
     * An object is requested if its digest ever appears in a request.
     * An object is received if it is ever found after being requested.
     * An object that is requested (because it was not found in the local
     * database), but not delivered in a response, and when re-requested is
     * found in the local database (because of some other workflow),
     * is still received. In fact, we call this case "indirectly" received
     * (as opposed to "directly" received in a response).
     * `ObjectRequester` can only count indirect receipts.
     * Direct receipts are counted in `CopyLedger::receive`.
     * We account in this way because `CopyLedger` knows it is finished
     * when received (direct and indirect) equals requested.
     */
    struct Metrics
    {
        /**
         * Number of objects requested at least once.
         */
        std::size_t requested = 0;
        /**
         * Number of times requested objects were missing in responses,
         * ignoring timeouts.
         * The same object will be counted multiple times
         * when it is missing in multiple responses.
         * We would like this to be zero, but do not expect it.
         */
        std::size_t missing = 0;
        /**
         * Number of unrequested objects found in responses. Should be zero.
         */
        std::size_t extra = 0;
        /**
         * Number of objects with bad data found in responses. Should be zero.
         */
        std::size_t errors = 0;
        /**
         * Number of objects "directly" received,
         * i.e. requested objects found in a response.
         */
        std::size_t dreceived = 0;
        /**
         * Number of objects "indirectly" received,
         * i.e. requested objects found in the database
         * after missing one or more responses.
         */
        std::size_t ireceived = 0;
        /**
         * Number of times the database was searched for an object.
         * The same object will be counted multiple times
         * when it is unreceived.
         */
        std::size_t searched = 0;
        /**
         * Number of objects found in the database.
         */
        std::size_t loaded = 0;

        constexpr std::size_t
        unreceived() const
        {
            return missing + errors;
        }
        constexpr std::size_t
        received() const
        {
            return dreceived + ireceived;
        }
        /**
         * Number of times objects were re-requested, ignoring timeouts.
         * We would like this to be zero, but do not expect it.
         */
        constexpr std::size_t
        rerequested() const
        {
            return searched - loaded - requested;
        }
        /**
         * The number of objects found in the database
         * from a previous download.
         */
        constexpr std::size_t
        carried() const
        {
            return loaded - ireceived;
        }

        void
        report(beast::Journal& journal)
        {
            assert(unreceived() == ireceived + rerequested());
            if (errors != 0)
            {
                JLOG(journal.warn()) << "errors: " << errors;
            }
            if (extra != 0)
            {
                JLOG(journal.warn()) << "extra: " << extra;
            }
            JLOG(journal.trace())
                << "missing: " << missing << ", dreceived: " << dreceived
                << ", searched: " << searched << ", loaded: " << loaded
                << ", ireceived: " << ireceived << ", carried: " << carried()
                << ", requested: " << requested
                << ", rerequested: " << rerequested();
        }

        friend Metrics&
        operator+=(Metrics& lhs, Metrics const& rhs)
        {
            lhs.requested += rhs.requested;
            lhs.missing += rhs.missing;
            lhs.extra += rhs.extra;
            lhs.errors += rhs.errors;
            lhs.dreceived += rhs.dreceived;
            lhs.ireceived += rhs.ireceived;
            lhs.searched += rhs.searched;
            lhs.loaded += rhs.loaded;
            return lhs;
        }
    };

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
    std::mutex mutex_;
    std::vector<RequestPtr> partialRequests_;
    std::vector<RequestPtr> fullRequests_;
    bool scheduled_ = false;

    // Metrics.
    // The number of sent (or queued, really) requests and received responses.
    // We're done when these numbers match at the comparison in `finish`.
    std::size_t nsent_ = 0;
    std::size_t nreceived_ = 0;
    CommunicationMeter receiveMeter_;
    Metrics metrics_;

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
     * Adds `nsent` to the count of sent requests.
     */
    void
    send(RequestPtr&& request, std::size_t nsent);

    /** Send a new request that should be counted.
     */
    void
    send(RequestPtr&& request)
    {
        return send(std::move(request), 1);
    }

    /** Re-send an old request that should not be double counted. */
    void
    resend(RequestPtr&& request)
    {
        return send(std::move(request), 0);
    }

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

    void
    finish(Metrics& metrics, std::size_t nreceived);

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
