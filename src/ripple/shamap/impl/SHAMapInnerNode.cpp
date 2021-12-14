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
#include <ripple/shamap/impl/TaggedPointer.ipp>

#include <openssl/sha.h>

#include <algorithm>
#include <array>
#include <iterator>
#include <mutex>
#include <utility>

namespace ripple {

std::mutex SHAMapInnerNode::childLock;

SHAMapInnerNode::SHAMapInnerNode(
    std::uint32_t cowid,
    std::uint8_t numAllocatedChildren)
    : SHAMapTreeNode(cowid), hashesAndChildren_(numAllocatedChildren)
{
}

SHAMapInnerNode::~SHAMapInnerNode() = default;

template <class F>
void
SHAMapInnerNode::iterChildren(F&& f) const
{
    hashesAndChildren_.iterChildren(isBranch_, std::forward<F>(f));
}

template <class F>
void
SHAMapInnerNode::iterNonEmptyChildIndexes(F&& f) const
{
    hashesAndChildren_.iterNonEmptyChildIndexes(isBranch_, std::forward<F>(f));
}

void
SHAMapInnerNode::resizeChildArrays(std::uint8_t toAllocate)
{
    hashesAndChildren_ =
        TaggedPointer(std::move(hashesAndChildren_), isBranch_, toAllocate);
}

std::optional<int>
SHAMapInnerNode::getChildIndex(int i) const
{
    return hashesAndChildren_.getChildIndex(isBranch_, i);
}

std::shared_ptr<SHAMapTreeNode>
SHAMapInnerNode::clone(std::uint32_t cowid) const
{
    auto const branchCount = getBranchCount();
    auto const thisIsSparse = !hashesAndChildren_.isDense();
    auto p = std::make_shared<SHAMapInnerNode>(cowid, branchCount);
    p->hash_ = hash_;
    p->isBranch_ = isBranch_;
    p->fullBelowGen_ = fullBelowGen_;
    SHAMapHash *cloneHashes, *thisHashes;
    std::shared_ptr<SHAMapTreeNode>*cloneChildren, *thisChildren;
    // structured bindings can't be captured in c++ 17; use tie instead
    std::tie(std::ignore, cloneHashes, cloneChildren) =
        p->hashesAndChildren_.getHashesAndChildren();
    std::tie(std::ignore, thisHashes, thisChildren) =
        hashesAndChildren_.getHashesAndChildren();

    if (thisIsSparse)
    {
        int cloneChildIndex = 0;
        iterNonEmptyChildIndexes([&](auto branchNum, auto indexNum) {
            cloneHashes[cloneChildIndex++] = thisHashes[indexNum];
        });
    }
    else
    {
        iterNonEmptyChildIndexes([&](auto branchNum, auto indexNum) {
            cloneHashes[branchNum] = thisHashes[indexNum];
        });
    }
    std::lock_guard lock(childLock);
    if (thisIsSparse)
    {
        int cloneChildIndex = 0;
        iterNonEmptyChildIndexes([&](auto branchNum, auto indexNum) {
            cloneChildren[cloneChildIndex++] = thisChildren[indexNum];
        });
    }
    else
    {
        iterNonEmptyChildIndexes([&](auto branchNum, auto indexNum) {
            cloneChildren[branchNum] = thisChildren[indexNum];
        });
    }

    return p;
}

std::shared_ptr<SHAMapTreeNode>
SHAMapInnerNode::makeFullInner(
    Slice data,
    SHAMapHash const& hash,
    bool hashValid)
{
    // A full inner node is serialized as 16 256-bit hashes, back to back:
    if (data.size() != branchFactor * uint256::bytes)
        Throw<std::runtime_error>("Invalid FI node");

    auto ret = std::make_shared<SHAMapInnerNode>(0, branchFactor);

    SerialIter si(data);

    auto hashes = ret->hashesAndChildren_.getHashes();

    for (int i = 0; i < branchFactor; ++i)
    {
        hashes[i].as_uint256() = si.getBitString<256>();

        if (hashes[i].isNonZero())
            ret->isBranch_ |= (1 << i);
    }

    ret->resizeChildArrays(ret->getBranchCount());

    if (hashValid)
        ret->hash_ = hash;
    else
        ret->updateHash();

    return ret;
}

std::shared_ptr<SHAMapTreeNode>
SHAMapInnerNode::makeCompressedInner(Slice data)
{
    // A compressed inner node is serialized as a series of 33 byte chunks,
    // representing a one byte "position" and a 256-bit hash:
    constexpr std::size_t chunkSize = uint256::bytes + 1;

    if (auto const s = data.size();
        (s % chunkSize != 0) || (s > chunkSize * branchFactor))
        Throw<std::runtime_error>("Invalid CI node");

    SerialIter si(data);

    auto ret = std::make_shared<SHAMapInnerNode>(0, branchFactor);

    auto hashes = ret->hashesAndChildren_.getHashes();

    while (!si.empty())
    {
        auto const hash = si.getBitString<256>();
        auto const pos = si.get8();

        if (pos >= branchFactor)
            Throw<std::runtime_error>("invalid CI node");

        hashes[pos].as_uint256() = hash;

        if (hashes[pos].isNonZero())
            ret->isBranch_ |= (1 << pos);
    }

    ret->resizeChildArrays(ret->getBranchCount());
    ret->updateHash();
    return ret;
}

void
SHAMapInnerNode::updateHash()
{
    uint256 nh;
    if (isBranch_ != 0)
    {
        sha512_half_hasher h;
        using beast::hash_append;
        hash_append(h, HashPrefix::innerNode);
        iterChildren([&](SHAMapHash const& hh) { hash_append(h, hh); });
        nh = static_cast<typename sha512_half_hasher::result_type>(h);
    }
    hash_ = SHAMapHash{nh};
}

void
SHAMapInnerNode::updateHashDeep()
{
    SHAMapHash* hashes;
    std::shared_ptr<SHAMapTreeNode>* children;
    // structured bindings can't be captured in c++ 17; use tie instead
    std::tie(std::ignore, hashes, children) =
        hashesAndChildren_.getHashesAndChildren();
    iterNonEmptyChildIndexes([&](auto branchNum, auto indexNum) {
        if (children[indexNum] != nullptr)
            hashes[indexNum] = children[indexNum]->getHash();
    });
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
        auto hashes = hashesAndChildren_.getHashes();
        iterNonEmptyChildIndexes([&](auto branchNum, auto indexNum) {
            s.addBitString(hashes[indexNum].as_uint256());
            s.add8(branchNum);
        });
        s.add8(wireTypeCompressedInner);
    }
    else
    {
        iterChildren(
            [&](SHAMapHash const& hh) { s.addBitString(hh.as_uint256()); });
        s.add8(wireTypeInner);
    }
}

void
SHAMapInnerNode::serializeWithPrefix(Serializer& s) const
{
    assert(!isEmpty());

    s.add32(HashPrefix::innerNode);
    iterChildren(
        [&](SHAMapHash const& hh) { s.addBitString(hh.as_uint256()); });
}

bool
SHAMapInnerNode::isEmpty() const
{
    return isBranch_ == 0;
}

int
SHAMapInnerNode::getBranchCount() const
{
    return popcnt16(isBranch_);
}

std::string
SHAMapInnerNode::getString(const SHAMapNodeID& id) const
{
    std::string ret = SHAMapTreeNode::getString(id);
    auto hashes = hashesAndChildren_.getHashes();
    iterNonEmptyChildIndexes([&](auto branchNum, auto indexNum) {
        ret += "\nb";
        ret += std::to_string(branchNum);
        ret += " = ";
        ret += to_string(hashes[indexNum]);
    });
    return ret;
}

// We are modifying an inner node
void
SHAMapInnerNode::setChild(int m, std::shared_ptr<SHAMapTreeNode> const& child)
{
    assert((m >= 0) && (m < branchFactor));
    assert(cowid_ != 0);
    assert(child.get() != this);

    auto const dstIsBranch = [&] {
        if (child)
            return isBranch_ | (1 << m);
        else
            return isBranch_ & ~(1 << m);
    }();

    auto const dstToAllocate = popcnt16(dstIsBranch);
    // change hashesAndChildren to remove the element, or make room for the
    // added element, if necessary
    hashesAndChildren_ = TaggedPointer(
        std::move(hashesAndChildren_), isBranch_, dstIsBranch, dstToAllocate);

    isBranch_ = dstIsBranch;

    if (child)
    {
        auto const childIndex = *getChildIndex(m);
        auto [_, hashes, children] = hashesAndChildren_.getHashesAndChildren();
        hashes[childIndex].zero();
        children[childIndex] = child;
    }

    hash_.zero();

    assert(getBranchCount() <= hashesAndChildren_.capacity());
}

// finished modifying, now make shareable
void
SHAMapInnerNode::shareChild(int m, std::shared_ptr<SHAMapTreeNode> const& child)
{
    assert((m >= 0) && (m < branchFactor));
    assert(cowid_ != 0);
    assert(child);
    assert(child.get() != this);

    assert(!isEmptyBranch(m));
    hashesAndChildren_.getChildren()[*getChildIndex(m)] = child;
}

SHAMapTreeNode*
SHAMapInnerNode::getChildPointer(int branch)
{
    assert(branch >= 0 && branch < branchFactor);
    assert(!isEmptyBranch(branch));

    std::lock_guard lock(childLock);
    return hashesAndChildren_.getChildren()[*getChildIndex(branch)].get();
}

std::shared_ptr<SHAMapTreeNode>
SHAMapInnerNode::getChild(int branch)
{
    assert(branch >= 0 && branch < branchFactor);
    assert(!isEmptyBranch(branch));

    std::lock_guard lock(childLock);
    return hashesAndChildren_.getChildren()[*getChildIndex(branch)];
}

SHAMapHash const&
SHAMapInnerNode::getChildHash(int m) const
{
    assert((m >= 0) && (m < branchFactor));
    if (auto const i = getChildIndex(m))
        return hashesAndChildren_.getHashes()[*i];

    return zeroSHAMapHash;
}

std::shared_ptr<SHAMapTreeNode>
SHAMapInnerNode::canonicalizeChild(
    int branch,
    std::shared_ptr<SHAMapTreeNode> node)
{
    assert(branch >= 0 && branch < branchFactor);
    assert(node);
    assert(!isEmptyBranch(branch));
    auto const childIndex = *getChildIndex(branch);
    auto [_, hashes, children] = hashesAndChildren_.getHashesAndChildren();
    assert(node->getHash() == hashes[childIndex]);

    std::lock_guard lock(childLock);
    if (children[childIndex])
    {
        // There is already a node hooked up, return it
        node = children[childIndex];
    }
    else
    {
        // Hook this node up
        children[childIndex] = node;
    }
    return node;
}

void
SHAMapInnerNode::invariants(bool is_root) const
{
    unsigned count = 0;
    auto [numAllocated, hashes, children] =
        hashesAndChildren_.getHashesAndChildren();

    if (numAllocated != branchFactor)
    {
        auto const branchCount = getBranchCount();
        for (int i = 0; i < branchCount; ++i)
        {
            assert(hashes[i].isNonZero());
            if (children[i] != nullptr)
                children[i]->invariants();
            ++count;
        }
    }
    else
    {
        for (int i = 0; i < branchFactor; ++i)
        {
            if (hashes[i].isNonZero())
            {
                assert((isBranch_ & (1 << i)) != 0);
                if (children[i] != nullptr)
                    children[i]->invariants();
                ++count;
            }
            else
            {
                assert((isBranch_ & (1 << i)) == 0);
            }
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
