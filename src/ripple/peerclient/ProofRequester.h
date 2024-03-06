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

#ifndef RIPPLE_PEERCLIENT_PROOFREQUESTER_H_INCLUDED
#define RIPPLE_PEERCLIENT_PROOFREQUESTER_H_INCLUDED

#include <ripple/app/main/Application.h>
#include <ripple/ledger/LedgerIdentifier.h>
#include <ripple/peerclient/BasicSenderReceiver.h>
#include <ripple/shamap/SHAMapLeafNode.h>

#include <memory>

namespace ripple {

using SHAMapKey = uint256;

class ProofRequester
    : public BasicSenderReceiver<std::shared_ptr<SHAMapLeafNode>>
{
protected:
    using Journaler::journal_;

public:
    using Named::name;

private:
    // TODO: Optimize layout.
    LedgerDigest ledgerDigest_;
    SHAMapKey key_;
    SHAMapNodeID nodeid_{};
    Blacklist blacklist_{};
    NetClock::duration timeout_ = std::chrono::seconds(4);

public:
    ProofRequester(
        Application& app,
        Scheduler& jscheduler,
        LedgerDigest&& ledgerDigest,
        SHAMapKey&& key)
        : BasicSenderReceiver(app, jscheduler, "ProofRequester")
        , ledgerDigest_(std::move(ledgerDigest))
        , key_(std::move(key))
    {
    }

    void
    name(std::ostream& out) const override
    {
        out << ledgerDigest_ << "/" << key_;
    }

    void
    onReady(MessageScheduler::Courier& courier) override;

    void
    onSuccess_(
        MessageScheduler::RequestId requestId,
        MessagePtr const& response) override;
};

}  // namespace ripple

#endif
