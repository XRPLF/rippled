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

#include <ripple/shamap/SHAMapInnerNode.h>

#include <ripple/basics/ByteUtilities.h>
#include <ripple/basics/Log.h>
#include <ripple/basics/Slice.h>
#include <ripple/basics/contract.h>
#include <ripple/basics/safe_cast.h>
#include <ripple/beast/core/LexicalCast.h>
#include <ripple/protocol/HashPrefix.h>
#include <ripple/protocol/digest.h>
#include <ripple/shamap/SHAMapTreeNode.h>

#include <openssl/sha.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <iterator>
#include <mutex>
#include <utility>

namespace ripple {

std::mutex SHAMapInnerNode::childLock;

std::shared_ptr<SHAMapTreeNode>
SHAMapInnerNode::clone(std::uint32_t cowid) const
{
    auto p = std::make_shared<SHAMapInnerNode>(cowid);
    p->hash_ = hash_;
    p->mIsBranch = mIsBranch;
    p->mFullBelowGen = mFullBelowGen;
    p->mHashes = mHashes;
    std::lock_guard lock(childLock);
    for (int i = 0; i < 16; ++i)
        p->mChildren[i] = mChildren[i];
    return p;
}

std::shared_ptr<SHAMapTreeNode>
SHAMapInnerNode::makeFullInner(
    Slice data,
    SHAMapHash const& hash,
    bool hashValid)
{
    if (data.size() != 512)
        Throw<std::runtime_error>("Invalid FI node");

    auto ret = std::make_shared<SHAMapInnerNode>(0);

    Serializer s(data.data(), data.size());

    for (int i = 0; i < 16; ++i)
    {
        s.getBitString(ret->mHashes[i].as_uint256(), i * 32);

        if (ret->mHashes[i].isNonZero())
            ret->mIsBranch |= (1 << i);
    }

    if (hashValid)
        ret->hash_ = hash;
    else
        ret->updateHash();
    return ret;
}

std::shared_ptr<SHAMapTreeNode>
SHAMapInnerNode::makeCompressedInner(Slice data)
{
    Serializer s(data.data(), data.size());

    int len = s.getLength();

    auto ret = std::make_shared<SHAMapInnerNode>(0);

    for (int i = 0; i < (len / 33); ++i)
    {
        int pos;

        if (!s.get8(pos, 32 + (i * 33)))
            Throw<std::runtime_error>("short CI node");

        if ((pos < 0) || (pos >= 16))
            Throw<std::runtime_error>("invalid CI node");

        s.getBitString(ret->mHashes[pos].as_uint256(), i * 33);

        if (ret->mHashes[pos].isNonZero())
            ret->mIsBranch |= (1 << pos);
    }

    ret->updateHash();

    return ret;
}

void
SHAMapInnerNode::updateHash()
{
    uint256 nh;
    if (mIsBranch != 0)
    {
        sha512_half_hasher h;
        using beast::hash_append;
        hash_append(h, HashPrefix::innerNode);
        for (auto const& hh : mHashes)
            hash_append(h, hh);
        nh = static_cast<typename sha512_half_hasher::result_type>(h);
    }
    hash_ = SHAMapHash{nh};
}

void
SHAMapInnerNode::updateHashDeep()
{
    for (auto pos = 0; pos < 16; ++pos)
    {
        if (mChildren[pos] != nullptr)
            mHashes[pos] = mChildren[pos]->getHash();
    }
    updateHash();
}

void
SHAMapInnerNode::serializeForWire(Serializer& s) const
{
    assert(!isEmpty());

    // If the node is sparse, then only send non-empty branches:
    if (getBranchCount() < 12)
    {
        // compressed node
        for (int i = 0; i < mHashes.size(); ++i)
        {
            if (!isEmptyBranch(i))
            {
                s.addBitString(mHashes[i].as_uint256());
                s.add8(i);
            }
        }

        s.add8(wireTypeCompressedInner);
    }
    else
    {
        for (auto const& hh : mHashes)
            s.addBitString(hh.as_uint256());

        s.add8(wireTypeInner);
    }
}

void
SHAMapInnerNode::serializeWithPrefix(Serializer& s) const
{
    assert(!isEmpty());

    s.add32(HashPrefix::innerNode);
    for (auto const& hh : mHashes)
        s.addBitString(hh.as_uint256());
}

bool
SHAMapInnerNode::isEmpty() const
{
    return mIsBranch == 0;
}

int
SHAMapInnerNode::getBranchCount() const
{
    int count = 0;

    for (int i = 0; i < 16; ++i)
        if (!isEmptyBranch(i))
            ++count;

    return count;
}

std::string
SHAMapInnerNode::getString(const SHAMapNodeID& id) const
{
    std::string ret = SHAMapTreeNode::getString(id);
    for (int i = 0; i < mHashes.size(); ++i)
    {
        if (!isEmptyBranch(i))
        {
            ret += "\n";
            ret += std::to_string(i);
            ret += " = ";
            ret += to_string(mHashes[i]);
        }
    }
    return ret;
}

// We are modifying an inner node
void
SHAMapInnerNode::setChild(int m, std::shared_ptr<SHAMapTreeNode> const& child)
{
    assert((m >= 0) && (m < 16));
    assert(cowid_ != 0);
    assert(child.get() != this);
    mHashes[m].zero();
    hash_.zero();
    if (child)
        mIsBranch |= (1 << m);
    else
        mIsBranch &= ~(1 << m);
    mChildren[m] = child;
}

// finished modifying, now make shareable
void
SHAMapInnerNode::shareChild(int m, std::shared_ptr<SHAMapTreeNode> const& child)
{
    assert((m >= 0) && (m < 16));
    assert(cowid_ != 0);
    assert(child);
    assert(child.get() != this);

    mChildren[m] = child;
}

SHAMapTreeNode*
SHAMapInnerNode::getChildPointer(int branch)
{
    assert(branch >= 0 && branch < 16);

    std::lock_guard lock(childLock);
    return mChildren[branch].get();
}

std::shared_ptr<SHAMapTreeNode>
SHAMapInnerNode::getChild(int branch)
{
    assert(branch >= 0 && branch < 16);

    std::lock_guard lock(childLock);
    return mChildren[branch];
}

std::shared_ptr<SHAMapTreeNode>
SHAMapInnerNode::canonicalizeChild(
    int branch,
    std::shared_ptr<SHAMapTreeNode> node)
{
    assert(branch >= 0 && branch < 16);
    assert(node);
    assert(node->getHash() == mHashes[branch]);

    std::lock_guard lock(childLock);
    if (mChildren[branch])
    {
        // There is already a node hooked up, return it
        node = mChildren[branch];
    }
    else
    {
        // Hook this node up
        mChildren[branch] = node;
    }
    return node;
}

void
SHAMapInnerNode::invariants(bool is_root) const
{
    unsigned count = 0;
    for (int i = 0; i < 16; ++i)
    {
        if (mHashes[i].isNonZero())
        {
            assert((mIsBranch & (1 << i)) != 0);
            if (mChildren[i] != nullptr)
                mChildren[i]->invariants();
            ++count;
        }
        else
        {
            assert((mIsBranch & (1 << i)) == 0);
        }
    }
    if (!is_root)
    {
        assert(hash_.isNonZero());
        assert(count >= 1);
    }
    assert((count == 0) ? hash_.isZero() : hash_.isNonZero());
}

}  // namespace ripple
