//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2023 Ripple Labs Inc.

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

#ifndef RIPPLE_PEERCLIENT_OBJECTSREQUESTER_H_INCLUDED
#define RIPPLE_PEERCLIENT_OBJECTSREQUESTER_H_INCLUDED

#include <ripple/app/main/Application.h>
#include <ripple/basics/promises.h>
#include <ripple/ledger/LedgerIdentifier.h>
#include <ripple/peerclient/BasicSenderReceiver.h>
#include <ripple/protocol/messages.h>

#include <chrono>
#include <memory>

namespace ripple {

// TODO: Race peers, at most k simultaneously, in random order,
// until one succeeds or all failed.
// TODO: Request multiple objects.
class ObjectsRequester
    : public BasicSenderReceiver<std::shared_ptr<protocol::TMGetObjectByHash>>
{
protected:
    using Journaler::journal_;

public:
    using Named::name;

public:
    using Clock = std::chrono::steady_clock;
    using RequestPtr = std::unique_ptr<protocol::TMGetObjectByHash>;
    using ResponsePtr = std::shared_ptr<protocol::TMGetObjectByHash>;

private:
    // TODO: Optimize layout.
    ObjectDigest digest_;
    RequestPtr request_;
    std::vector<Peer::id_t> tried_;
    Clock::time_point start_ = Clock::now();
    Clock::duration timeout_ = std::chrono::seconds(30);

public:
    ObjectsRequester(
        Application& app,
        Scheduler& jscheduler,
        protocol::TMGetObjectByHash::ObjectType type,
        ObjectDigest&& digest)
        : BasicSenderReceiver(app, jscheduler, "ObjectsRequester")
        , digest_(std::move(digest))
    {
        request_ = std::make_unique<protocol::TMGetObjectByHash>();
        request_->set_type(type);
        request_->add_objects()->set_hash(digest.begin(), digest.size());
    }

    void
    onReady(MessageScheduler::Courier& courier) override;

    void
    onSuccess(MessageScheduler::RequestId requestId, MessagePtr const& response)
        override;

    void
    name(std::ostream& out) const override
    {
        out << digest_;
    }
};

}  // namespace ripple

#endif
