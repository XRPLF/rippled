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

#include <ripple/peerclient/ProofRequester.h>

#include <ripple/shamap/SHAMap.h>

namespace ripple {

void
ProofRequester::onReady(MessageScheduler::Courier& courier)
{
    auto request = std::make_unique<protocol::TMGetLedger>();
    request->set_itype(protocol::liAS_NODE);
    // Do not set `ltype`. We want a validated ledger.
    request->set_ledgerhash(ledgerDigest_.begin(), ledgerDigest_.size());
    // We do not have to set `ledgerSeq`. It is optional.

    request->add_nodeids(nodeid_.getRawString());
    assert(request->nodeids_size() == 1);

    // Do not set `queryType`. Limit our reach to our immediate peers.
    // Step one level at a time.
    // TODO: Step up to two levels at a time.
    request->set_querydepth(0);
    // TODO: See getPeerWithLedger in PeerImp.cpp.
    for (auto& peer : courier.freshPeers())
    {
        if (courier.send(peer, *request, this, timeout_))
        {
            return;
        }
    }
    // TODO: Shuffle.
    for (auto& peer : courier.allPeers())
    {
        if (courier.send(peer, *request, this, timeout_))
        {
            return;
        }
    }
    // Never sent. We will be offered again later.
}

void
ProofRequester::onSuccess_(
    MessageScheduler::RequestId requestId,
    MessagePtr const& response)
{
    auto const& m = static_cast<protocol::TMLedgerData const&>(*response);
    if (m.has_error() || m.nodes().empty())
    {
        // TODO: Track tried peers like `ObjectsRequester`.
        // TODO: Return true to reschedule.
        return schedule();
    }

    auto ledgerDigest = LedgerDigest(m.ledgerhash());
    if (ledgerDigest != ledgerDigest_)
    {
        JLOG(journal_.error()) << name() << " wrong ledger digest";
        return schedule();
    }

    if (m.nodes_size() < 1)
    {
        // This should be expected when the peer did not have the node,
        // thus `INFO` level.
        JLOG(journal_.info()) << name() << " missing node";
        return schedule();
    }

    if (m.nodes_size() > 1)
    {
        JLOG(journal_.error()) << name() << " extra nodes";
    }

    auto const& node = m.nodes(0);
    auto const nodeid = deserializeSHAMapNodeID(node.nodeid());
    if (!nodeid)
    {
        JLOG(journal_.error()) << name() << " missing node key";
        return schedule();
    }

    if (nodeid != nodeid_)
    {
        JLOG(journal_.error()) << name() << " wrong node key";
        return schedule();
    }

    auto slice = makeSlice(node.nodedata());
    auto tnode = SHAMapTreeNode::makeFromWire(slice);
    if (!tnode)
    {
        JLOG(journal_.error()) << name() << " bad node data";
        return schedule();
    }

    if (tnode->isInner())
    {
        auto const inner = std::static_pointer_cast<SHAMapInnerNode>(tnode);
        auto const branch = selectBranch(nodeid_, key_);
        if (inner->isEmptyBranch(branch))
            return throw_("key does not exist in tree");
        nodeid_ = nodeid_.getChildNodeID(branch);
        return schedule();
    }

    auto leaf = std::static_pointer_cast<SHAMapLeafNode>(tnode);
    JLOG(journal_.info()) << name() << " finished";
    return return_(std::move(leaf));
}

}  // namespace ripple
