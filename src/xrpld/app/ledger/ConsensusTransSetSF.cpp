//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#include <xrpld/app/ledger/ConsensusTransSetSF.h>
#include <xrpld/app/ledger/TransactionMaster.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/app/misc/NetworkOPs.h>
#include <xrpld/app/misc/Transaction.h>
#include <xrpld/core/JobQueue.h>
#include <xrpld/nodestore/Database.h>
#include <xrpl/basics/Log.h>
#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/protocol/digest.h>

namespace ripple {

ConsensusTransSetSF::ConsensusTransSetSF(Application& app, NodeCache& nodeCache)
    : app_(app), m_nodeCache(nodeCache), j_(app.journal("TransactionAcquire"))
{
}

void
ConsensusTransSetSF::gotNode(
    bool fromFilter,
    SHAMapHash const& nodeHash,
    std::uint32_t,
    Blob&& nodeData,
    SHAMapNodeType type) const
{
    if (fromFilter)
        return;

    m_nodeCache.insert(nodeHash, nodeData);

    if ((type == SHAMapNodeType::tnTRANSACTION_NM) && (nodeData.size() > 16))
    {
        // this is a transaction, and we didn't have it
        JLOG(j_.debug())
            << "Node on our acquiring TX set is TXN we may not have";

        try
        {
            // skip prefix
            Serializer s(nodeData.data() + 4, nodeData.size() - 4);
            SerialIter sit(s.slice());
            auto stx = std::make_shared<STTx const>(std::ref(sit));
            ASSERT(
                stx->getTransactionID() == nodeHash.as_uint256(),
                "ripple::ConsensusTransSetSF::gotNode : transaction hash "
                "match");
            auto const pap = &app_;
            app_.getJobQueue().addJob(jtTRANSACTION, "TXS->TXN", [pap, stx]() {
                pap->getOPs().submitTransaction(stx);
            });
        }
        catch (std::exception const& ex)
        {
            JLOG(j_.warn())
                << "Fetched invalid transaction in proposed set. Exception: "
                << ex.what();
        }
    }
}

std::optional<Blob>
ConsensusTransSetSF::getNode(SHAMapHash const& nodeHash) const
{
    Blob nodeData;
    if (m_nodeCache.retrieve(nodeHash, nodeData))
        return nodeData;

    auto txn =
        app_.getMasterTransaction().fetch_from_cache(nodeHash.as_uint256());

    if (txn)
    {
        // this is a transaction, and we have it
        JLOG(j_.trace()) << "Node in our acquiring TX set is TXN we have";
        Serializer s;
        s.add32(HashPrefix::transactionID);
        txn->getSTransaction()->add(s);
        ASSERT(
            sha512Half(s.slice()) == nodeHash.as_uint256(),
            "ripple::ConsensusTransSetSF::getNode : transaction hash match");
        nodeData = s.peekData();
        return nodeData;
    }

    return std::nullopt;
}

}  // namespace ripple
