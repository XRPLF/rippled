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

#ifndef RIPPLE_PEERCLIENT_BASICSHAMAPREQUESTER_H_INCLUDED
#define RIPPLE_PEERCLIENT_BASICSHAMAPREQUESTER_H_INCLUDED

#include <ripple/app/main/Application.h>
#include <ripple/ledger/LedgerIdentifier.h>
#include <ripple/overlay/impl/Tuning.h>
#include <ripple/peerclient/BasicSenderReceiver.h>
#include <ripple/shamap/SHAMapInnerNode.h>
#include <ripple/shamap/SHAMapLeafNode.h>
#include <ripple/shamap/SHAMapNodeID.h>

namespace ripple {

template <typename T>
class BasicSHAMapRequester : public BasicSenderReceiver<T>
{
protected:
    using Journaler::journal_;

public:
    using Named::name;

protected:
    ObjectDigest digest_;

private:
    protocol::TMLedgerInfoType type_;
    // TODO: Optimize layout.
    hash_set<SHAMapNodeID> nodeids_{};
    Blacklist blacklist_{};
    NetClock::duration timeout_ = std::chrono::seconds(4);

protected:
    BasicSHAMapRequester(
        Application& app,
        Scheduler& jscheduler,
        char const* name,
        protocol::TMLedgerInfoType type,
        ObjectDigest&& digest)
        : BasicSenderReceiver<T>(app, jscheduler, name)
        , digest_(std::move(digest))
        , type_(type)
    {
        // Start with the root key, which is all zeroes.
        nodeids_.insert(SHAMapNodeID());
    }

public:
    void
    name(std::ostream& out) const override
    {
        // TODO: Human-readable name for `type_`.
        out << digest_ << "/" << type_;
    }

    void
    onReady(MessageScheduler::Courier& courier) override
    {
        auto request = std::make_unique<protocol::TMGetLedger>();
        request->set_itype(type_);
        // Do not set `ltype`. We want a validated ledger.
        request->set_ledgerhash(digest_.begin(), digest_.size());
        // We do not have to set `ledgerSeq`. It is optional.
        for (auto& nodeid : nodeids_)
        {
            request->add_nodeids(nodeid.getRawString());
        }
        // Do not set `queryType`. Limit our reach to our immediate peers.
        // Try to get all transactions in one message.
        request->set_querydepth(Tuning::maxQueryDepth);

        // TODO: See getPeerWithLedger in PeerImp.cpp.
        Blaster blaster{courier};
        assert(blaster);
        auto result = blaster.send(blacklist_, *request, this, timeout_);
        if (result == Blaster::SENT) {
            assert(courier.closed() == 1);
            assert(courier.evicting());
            return;
        }
        assert(courier.closed() == 0);
        assert(!courier.evicting());
        if (result == Blaster::RETRY) {
            return;
        }
        assert(result == Blaster::FAILED);
        courier.withdraw();
        assert(courier.evicting());
        return this->throw_("no peer responded in time");
    }

    void
    onSuccess_(
        MessageScheduler::RequestId requestId,
        MessagePtr const& response) override
    {
        auto const& m = static_cast<protocol::TMLedgerData const&>(*response);
        if (m.nodes().empty())
        {
            JLOG(journal_.warn()) << name() << " peer missing object";
            return this->schedule();
        }

        if (m.has_error())
        {
            JLOG(journal_.error()) << name() << m.error();
            return this->schedule();
        }

        auto digest = ObjectDigest(m.ledgerhash());
        if (digest != digest_)
        {
            JLOG(journal_.error()) << name() << " wrong ledger digest";
            return this->schedule();
        }

        for (auto const& node : m.nodes())
        {
            if (!node.has_nodeid())
            {
                JLOG(journal_.error()) << name() << " missing node key";
                continue;
            }
            if (!node.has_nodedata())
            {
                JLOG(journal_.error()) << name() << " missing node data";
                continue;
            }

            auto nodeid = deserializeSHAMapNodeID(node.nodeid());
            if (!nodeid)
            {
                JLOG(journal_.error()) << name() << " bad node key";
                continue;
            }

            auto it = nodeids_.find(*nodeid);
            if (it == nodeids_.end())
            {
                JLOG(journal_.error())
                    << name() << " unrequested node key: " << *nodeid;
                continue;
            }

            auto slice = makeSlice(node.nodedata());
            auto tnode = SHAMapTreeNode::makeFromWire(slice);
            if (!tnode)
            {
                JLOG(journal_.error()) << name() << " bad node data";
                continue;
            }

            if (tnode->isInner())
            {
                auto& inner = static_cast<SHAMapInnerNode&>(*tnode);
                if (this->onInner(*nodeid, inner))
                {
                    // `output_` should be settled,
                    // but we cannot access `this` (because it may be deleted)
                    // or `output` (because it is private).
                    return;
                }
                for (unsigned int i = 0; i < SHAMapInnerNode::branchFactor; ++i)
                {
                    if (inner.isEmptyBranch(i))
                    {
                        continue;
                    }
                    nodeids_.insert(nodeid->getChildNodeID(i));
                }
            }
            else
            {
                assert(tnode->isLeaf());
                auto& leaf = static_cast<SHAMapLeafNode&>(*tnode);
                if (this->onLeaf(*nodeid, leaf))
                {
                    return;
                }
            }

            nodeids_.erase(it);
        }

        if (!nodeids_.empty())
        {
            return this->schedule();
        }

        this->onComplete();
    }

protected:
    /** @return true if the algorithm should stop. */
    // REVIEW: Alternatively,
    // we can stop after we detect the output is settled.
    virtual bool
    onInner(SHAMapNodeID& id, SHAMapInnerNode& inner)
    {
        return false;
    }

    /** @return true if the algorithm should stop. */
    virtual bool
    onLeaf(SHAMapNodeID& id, SHAMapLeafNode& leaf)
    {
        return false;
    }

    virtual void
    onComplete()
    {
    }
};

}  // namespace ripple

#endif
