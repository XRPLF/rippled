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

#include <ripple/peerclient/TxSetRequester.h>

#include <ripple/protocol/messages.h>
#include <ripple/shamap/SHAMapInnerNode.h>
#include <ripple/shamap/SHAMapTreeNode.h>

namespace ripple {

bool
TxSetRequester::onLeaf(SHAMapNodeID& id, SHAMapLeafNode& leaf)
{
    assert(leaf.getType() == SHAMapNodeType::tnTRANSACTION_MD);
    auto slice = leaf.peekItem()->slice();
    SerialIter txMetaSit(slice);
    SerialIter txSit(txMetaSit.getSlice(txMetaSit.getVLDataLength()));
    SerialIter metaSit(txMetaSit.getSlice(txMetaSit.getVLDataLength()));
    auto tx = std::make_shared<STTx const>(txSit);
    if (!tx)
    {
        JLOG(journal_.error()) << name() << " bad node data";
        return false;
    }
    STObject meta(metaSit, sfMetadata);
    txns_.emplace(meta[sfTransactionIndex], std::move(tx));
    return false;
}

void
TxSetRequester::onComplete()
{
    JLOG(journal_.info()) << name() << " finished size=" << txns_.size();
    return return_(std::move(txns_));
}

}  // namespace ripple
