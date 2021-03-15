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

#ifndef RIPPLE_SHAMAP_SHAMAPTXLEAFNODE_H_INCLUDED
#define RIPPLE_SHAMAP_SHAMAPTXLEAFNODE_H_INCLUDED

#include <ripple/basics/CountedObject.h>
#include <ripple/protocol/HashPrefix.h>
#include <ripple/protocol/digest.h>
#include <ripple/shamap/SHAMapItem.h>
#include <ripple/shamap/SHAMapLeafNode.h>
#include <ripple/shamap/SHAMapNodeID.h>

namespace ripple {

/** A leaf node for a transaction. No metadata is included. */
class SHAMapTxLeafNode final : public SHAMapLeafNode,
                               public CountedObject<SHAMapTxLeafNode>
{
public:
    SHAMapTxLeafNode(
        std::shared_ptr<SHAMapItem const> item,
        std::uint32_t cowid)
        : SHAMapLeafNode(std::move(item), cowid)
    {
        updateHash();
    }

    SHAMapTxLeafNode(
        std::shared_ptr<SHAMapItem const> item,
        std::uint32_t cowid,
        SHAMapHash const& hash)
        : SHAMapLeafNode(std::move(item), cowid, hash)
    {
    }

    std::shared_ptr<SHAMapTreeNode>
    clone(std::uint32_t cowid) const final override
    {
        return std::make_shared<SHAMapTxLeafNode>(item_, cowid, hash_);
    }

    SHAMapNodeType
    getType() const final override
    {
        return SHAMapNodeType::tnTRANSACTION_NM;
    }

    void
    updateHash() final override
    {
        hash_ =
            SHAMapHash{sha512Half(HashPrefix::transactionID, item_->slice())};
    }

    void
    serializeForWire(Serializer& s) const final override
    {
        s.addRaw(item_->slice());
        s.add8(wireTypeTransaction);
    }

    void
    serializeWithPrefix(Serializer& s) const final override
    {
        s.add32(HashPrefix::transactionID);
        s.addRaw(item_->slice());
    }
};

}  // namespace ripple

#endif
