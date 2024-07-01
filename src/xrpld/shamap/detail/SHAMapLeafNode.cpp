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

#include <ripple/basics/contract.h>
#include <ripple/beast/core/LexicalCast.h>
#include <ripple/shamap/SHAMapLeafNode.h>

namespace ripple {

SHAMapLeafNode::SHAMapLeafNode(
    boost::intrusive_ptr<SHAMapItem const> item,
    std::uint32_t cowid)
    : SHAMapTreeNode(cowid), item_(std::move(item))
{
    assert(item_->size() >= 12);
}

SHAMapLeafNode::SHAMapLeafNode(
    boost::intrusive_ptr<SHAMapItem const> item,
    std::uint32_t cowid,
    SHAMapHash const& hash)
    : SHAMapTreeNode(cowid, hash), item_(std::move(item))
{
    assert(item_->size() >= 12);
}

boost::intrusive_ptr<SHAMapItem const> const&
SHAMapLeafNode::peekItem() const
{
    return item_;
}

bool
SHAMapLeafNode::setItem(boost::intrusive_ptr<SHAMapItem const> item)
{
    assert(cowid_ != 0);
    item_ = std::move(item);

    auto const oldHash = hash_;

    updateHash();

    return (oldHash != hash_);
}

std::string
SHAMapLeafNode::getString(const SHAMapNodeID& id) const
{
    std::string ret = SHAMapTreeNode::getString(id);

    auto const type = getType();

    if (type == SHAMapNodeType::tnTRANSACTION_NM)
        ret += ",txn\n";
    else if (type == SHAMapNodeType::tnTRANSACTION_MD)
        ret += ",txn+md\n";
    else if (type == SHAMapNodeType::tnACCOUNT_STATE)
        ret += ",as\n";
    else
        ret += ",leaf\n";

    ret += "  Tag=";
    ret += to_string(item_->key());
    ret += "\n  Hash=";
    ret += to_string(hash_);
    ret += "/";
    ret += std::to_string(item_->size());
    return ret;
}

void
SHAMapLeafNode::invariants(bool) const
{
    assert(hash_.isNonZero());
    assert(item_ != nullptr);
}

}  // namespace ripple
