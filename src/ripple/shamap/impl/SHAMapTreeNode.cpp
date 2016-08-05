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

#include <BeastConfig.h>
#include <ripple/shamap/SHAMapTreeNode.h>
#include <ripple/basics/contract.h>
#include <ripple/basics/Log.h>
#include <ripple/protocol/digest.h>
#include <ripple/basics/Slice.h>
#include <ripple/basics/StringUtilities.h>
#include <ripple/protocol/HashPrefix.h>
#include <ripple/beast/core/LexicalCast.h>
#include <mutex>

#include <openssl/sha.h>

namespace ripple {

std::mutex SHAMapInnerNode::childLock;

SHAMapAbstractNode::~SHAMapAbstractNode() = default;

std::shared_ptr<SHAMapAbstractNode>
SHAMapInnerNode::clone(std::uint32_t seq) const
{
    auto p = std::make_shared<SHAMapInnerNode>(seq);
    p->mHash = mHash;
    p->mIsBranch = mIsBranch;
    p->mFullBelowGen = mFullBelowGen;
    p->mHashes = mHashes;
    std::lock_guard <std::mutex> lock(childLock);
    for (int i = 0; i < 16; ++i)
    {
        p->mChildren[i] = mChildren[i];
        assert(std::dynamic_pointer_cast<SHAMapInnerNodeV2>(p->mChildren[i]) == nullptr);
    }
    return std::move(p);
}

std::shared_ptr<SHAMapAbstractNode>
SHAMapInnerNodeV2::clone(std::uint32_t seq) const
{
    auto p = std::make_shared<SHAMapInnerNodeV2>(seq);
    p->mHash = mHash;
    p->mIsBranch = mIsBranch;
    p->mFullBelowGen = mFullBelowGen;
    p->mHashes = mHashes;
    p->common_ = common_;
    p->depth_ = depth_;
    std::lock_guard <std::mutex> lock(childLock);
    for (int i = 0; i < 16; ++i)
    {
        p->mChildren[i] = mChildren[i];
        if (p->mChildren[i] != nullptr)
            assert(std::dynamic_pointer_cast<SHAMapInnerNodeV2>(p->mChildren[i]) != nullptr ||
                   std::dynamic_pointer_cast<SHAMapTreeNode>(p->mChildren[i]) != nullptr);
    }
    return std::move(p);
}

std::shared_ptr<SHAMapAbstractNode>
SHAMapTreeNode::clone(std::uint32_t seq) const
{
    return std::make_shared<SHAMapTreeNode>(mItem, mType, seq, mHash);
}

SHAMapTreeNode::SHAMapTreeNode (std::shared_ptr<SHAMapItem const> const& item,
                                TNType type, std::uint32_t seq)
    : SHAMapAbstractNode(type, seq)
    , mItem (item)
{
    assert (item->peekData ().size () >= 12);
    updateHash();
}

SHAMapTreeNode::SHAMapTreeNode (std::shared_ptr<SHAMapItem const> const& item,
                                TNType type, std::uint32_t seq, SHAMapHash const& hash)
    : SHAMapAbstractNode(type, seq, hash)
    , mItem (item)
{
    assert (item->peekData ().size () >= 12);
}

std::shared_ptr<SHAMapAbstractNode>
SHAMapAbstractNode::make(Slice const& rawNode, std::uint32_t seq, SHANodeFormat format,
                         SHAMapHash const& hash, bool hashValid, beast::Journal j,
                         SHAMapNodeID const& id)
{
    if (format == snfWIRE)
    {
        if (rawNode.empty ())
            return {};

        Serializer s (rawNode.data(), rawNode.size() - 1);
        int type = rawNode[rawNode.size() - 1];
        int len = s.getLength ();

        if ((type < 0) || (type > 6))
            return {};
        if (type == 0)
        {
            // transaction
            auto item = std::make_shared<SHAMapItem const>(
                sha512Half(HashPrefix::transactionID,
                    Slice(s.data(), s.size())),
                        s.peekData());
            if (hashValid)
                return std::make_shared<SHAMapTreeNode>(item, tnTRANSACTION_NM, seq, hash);
            return std::make_shared<SHAMapTreeNode>(item, tnTRANSACTION_NM, seq);
        }
        else if (type == 1)
        {
            // account state
            if (len < (256 / 8))
                Throw<std::runtime_error> ("short AS node");

            uint256 u;
            s.get256 (u, len - (256 / 8));
            s.chop (256 / 8);

            if (u.isZero ()) Throw<std::runtime_error> ("invalid AS node");

            auto item = std::make_shared<SHAMapItem const> (u, s.peekData ());
            if (hashValid)
                return std::make_shared<SHAMapTreeNode>(item, tnACCOUNT_STATE, seq, hash);
            return std::make_shared<SHAMapTreeNode>(item, tnACCOUNT_STATE, seq);
        }
        else if (type == 2)
        {
            // full inner
            if (len != 512)
                Throw<std::runtime_error> ("invalid FI node");

            auto ret = std::make_shared<SHAMapInnerNode>(seq);
            for (int i = 0; i < 16; ++i)
            {
                s.get256 (ret->mHashes[i].as_uint256(), i * 32);

                if (ret->mHashes[i].isNonZero ())
                    ret->mIsBranch |= (1 << i);
            }
            if (hashValid)
                ret->mHash = hash;
            else
                ret->updateHash();
            return ret;
        }
        else if (type == 3)
        {
            auto ret = std::make_shared<SHAMapInnerNode>(seq);
            // compressed inner
            for (int i = 0; i < (len / 33); ++i)
            {
                int pos;
                if (! s.get8 (pos, 32 + (i * 33)))
                    Throw<std::runtime_error> ("short CI node");
                if ((pos < 0) || (pos >= 16))
                    Throw<std::runtime_error> ("invalid CI node");
                s.get256 (ret->mHashes[pos].as_uint256(), i * 33);
                if (ret->mHashes[pos].isNonZero ())
                    ret->mIsBranch |= (1 << pos);
            }
            if (hashValid)
                ret->mHash = hash;
            else
                ret->updateHash();
            return ret;
        }
        else if (type == 4)
        {
            // transaction with metadata
            if (len < (256 / 8))
                Throw<std::runtime_error> ("short TM node");

            uint256 u;
            s.get256 (u, len - (256 / 8));
            s.chop (256 / 8);

            if (u.isZero ())
                Throw<std::runtime_error> ("invalid TM node");

            auto item = std::make_shared<SHAMapItem const> (u, s.peekData ());
            if (hashValid)
                return std::make_shared<SHAMapTreeNode>(item, tnTRANSACTION_MD, seq, hash);
            return std::make_shared<SHAMapTreeNode>(item, tnTRANSACTION_MD, seq);
        }
        else if (type == 5)
        {
            // full v2 inner
            if (len != 512)
                Throw<std::runtime_error> ("invalid FI node");

            auto ret = std::make_shared<SHAMapInnerNodeV2>(seq);
            for (int i = 0; i < 16; ++i)
            {
                s.get256 (ret->mHashes[i].as_uint256(), i * 32);

                if (ret->mHashes[i].isNonZero ())
                    ret->mIsBranch |= (1 << i);
            }
            ret->set_common(id.getDepth(), id.getNodeID());
            if (hashValid)
                ret->mHash = hash;
            else
                ret->updateHash();
            return ret;
        }
        else if (type == 6)
        {
            auto ret = std::make_shared<SHAMapInnerNodeV2>(seq);
            // compressed v2 inner
            for (int i = 0; i < (len / 33); ++i)
            {
                int pos;
                if (! s.get8 (pos, 32 + (i * 33)))
                    Throw<std::runtime_error> ("short CI node");
                if ((pos < 0) || (pos >= 16))
                    Throw<std::runtime_error> ("invalid CI node");
                s.get256 (ret->mHashes[pos].as_uint256(), i * 33);
                if (ret->mHashes[pos].isNonZero ())
                    ret->mIsBranch |= (1 << pos);
            }
            ret->set_common(id.getDepth(), id.getNodeID());
            if (hashValid)
                ret->mHash = hash;
            else
                ret->updateHash();
            return ret;
        }
    }

    else if (format == snfPREFIX)
    {
        if (rawNode.size () < 4)
        {
            JLOG (j.info()) << "size < 4";
            Throw<std::runtime_error> ("invalid P node");
        }

        std::uint32_t prefix = rawNode[0];
        prefix <<= 8;
        prefix |= rawNode[1];
        prefix <<= 8;
        prefix |= rawNode[2];
        prefix <<= 8;
        prefix |= rawNode[3];
        Serializer s (rawNode.data() + 4, rawNode.size() - 4);

        if (prefix == HashPrefix::transactionID)
        {
            auto item = std::make_shared<SHAMapItem const>(
                sha512Half(rawNode),
                    s.peekData ());
            if (hashValid)
                return std::make_shared<SHAMapTreeNode>(item, tnTRANSACTION_NM, seq, hash);
            return std::make_shared<SHAMapTreeNode>(item, tnTRANSACTION_NM, seq);
        }
        else if (prefix == HashPrefix::leafNode)
        {
            if (s.getLength () < 32)
                Throw<std::runtime_error> ("short PLN node");

            uint256 u;
            s.get256 (u, s.getLength () - 32);
            s.chop (32);

            if (u.isZero ())
            {
                JLOG (j.info()) << "invalid PLN node";
                Throw<std::runtime_error> ("invalid PLN node");
            }

            auto item = std::make_shared<SHAMapItem const> (u, s.peekData ());
            if (hashValid)
                return std::make_shared<SHAMapTreeNode>(item, tnACCOUNT_STATE, seq, hash);
            return std::make_shared<SHAMapTreeNode>(item, tnACCOUNT_STATE, seq);
        }
        else if ((prefix == HashPrefix::innerNode) || (prefix == HashPrefix::innerNodeV2))
        {
            auto len = s.getLength();
            bool isV2 = (prefix == HashPrefix::innerNodeV2);

            if ((len < 512) || (!isV2 && (len != 512)) || (isV2 && (len == 512)))
                Throw<std::runtime_error> ("invalid PIN node");

            std::shared_ptr<SHAMapInnerNode> ret;
            if (isV2)
                ret = std::make_shared<SHAMapInnerNodeV2>(seq);
            else
                ret = std::make_shared<SHAMapInnerNode>(seq);

            for (int i = 0; i < 16; ++i)
            {
                s.get256 (ret->mHashes[i].as_uint256(), i * 32);

                if (ret->mHashes[i].isNonZero ())
                    ret->mIsBranch |= (1 << i);
            }

            if (isV2)
            {
                auto temp = std::static_pointer_cast<SHAMapInnerNodeV2>(ret);
                s.get8(temp->depth_, 512);
                auto n = (temp->depth_ + 1) / 2;
                if (len != 512 + 1 + n)
                    Throw<std::runtime_error> ("invalid PIN node");
                auto x = temp->common_.begin();
                for (auto i = 0; i < n; ++i, ++x)
                {
                    int byte;
                    s.get8(byte, 512+1+i);
                    *x = byte;
                }
            }
            if (hashValid)
                ret->mHash = hash;
            else
                ret->updateHash();
            return ret;
        }
        else if (prefix == HashPrefix::txNode)
        {
            // transaction with metadata
            if (s.getLength () < 32)
                Throw<std::runtime_error> ("short TXN node");

            uint256 txID;
            s.get256 (txID, s.getLength () - 32);
            s.chop (32);
            auto item = std::make_shared<SHAMapItem const> (txID, s.peekData ());
            if (hashValid)
                return std::make_shared<SHAMapTreeNode>(item, tnTRANSACTION_MD, seq, hash);
            return std::make_shared<SHAMapTreeNode>(item, tnTRANSACTION_MD, seq);
        }
        else
        {
            JLOG (j.info()) << "Unknown node prefix " << std::hex << prefix << std::dec;
            Throw<std::runtime_error> ("invalid node prefix");
        }
    }
    assert (false);
    Throw<std::runtime_error> ("Unknown format");
    return{}; // Silence compiler warning.
}

bool
SHAMapInnerNode::updateHash()
{
    uint256 nh;
    if (mIsBranch != 0)
    {
        sha512_half_hasher h;
        using beast::hash_append;
        hash_append(h, HashPrefix::innerNode);
        for(auto const& hh : mHashes)
            hash_append(h, hh);
        nh = static_cast<typename
            sha512_half_hasher::result_type>(h);
    }
    if (nh == mHash.as_uint256())
        return false;
    mHash = SHAMapHash{nh};
    return true;
}

void
SHAMapInnerNode::updateHashDeep()
{
    for (auto pos = 0; pos < 16; ++pos)
    {
        if (mChildren[pos] != nullptr)
            mHashes[pos] = mChildren[pos]->getNodeHash();
    }
    updateHash();
}

bool
SHAMapTreeNode::updateHash()
{
    uint256 nh;
    if (mType == tnTRANSACTION_NM)
    {
        nh = sha512Half(HashPrefix::transactionID,
            makeSlice(mItem->peekData()));
    }
    else if (mType == tnACCOUNT_STATE)
    {
        nh = sha512Half(HashPrefix::leafNode,
            makeSlice(mItem->peekData()),
                mItem->key());
    }
    else if (mType == tnTRANSACTION_MD)
    {
        nh = sha512Half(HashPrefix::txNode,
            makeSlice(mItem->peekData()),
                mItem->key());
    }
    else
        assert (false);

    if (nh == mHash.as_uint256())
        return false;

    mHash = SHAMapHash{nh};
    return true;
}

void
SHAMapInnerNode::addRaw(Serializer& s, SHANodeFormat format) const
{
    assert ((format == snfPREFIX) || (format == snfWIRE) || (format == snfHASH));

    if (mType == tnERROR)
        Throw<std::runtime_error> ("invalid I node type");

    if (format == snfHASH)
    {
        s.add256 (mHash.as_uint256());
    }
    else if (mType == tnINNER)
    {
        assert (!isEmpty ());

        if (format == snfPREFIX)
        {
            s.add32 (HashPrefix::innerNode);

            for (auto const& hh : mHashes)
                s.add256 (hh.as_uint256());
        }
        else  // format == snfWIRE
        {
            if (getBranchCount () < 12)
            {
                // compressed node
                for (int i = 0; i < mHashes.size(); ++i)
                    if (!isEmptyBranch (i))
                    {
                        s.add256 (mHashes[i].as_uint256());
                        s.add8 (i);
                    }

                s.add8 (3);
            }
            else
            {
                for (auto const& hh : mHashes)
                    s.add256 (hh.as_uint256());

                s.add8 (2);
            }
        }
    }
    else
        assert (false);
}

void
SHAMapInnerNodeV2::addRaw(Serializer& s, SHANodeFormat format) const
{
    if (format == snfPREFIX)
    {
        s.add32 (HashPrefix::innerNodeV2);

        for (int i = 0 ; i < 16; ++i)
            s.add256 (mHashes[i].as_uint256());

        s.add8(depth_);

        auto x = common_.begin();
        for (auto i = 0; i < (depth_+1)/2; ++i, ++x)
            s.add8(*x);
    }
    else
    {
        SHAMapInnerNode::addRaw(s, format);
        if (format == snfWIRE)
        {
            auto& data = s.modData();
            data.back() += 3;
        }
    }
}

bool
SHAMapInnerNodeV2::updateHash()
{
    uint256 nh;

    if (mIsBranch != 0)
    {
        Serializer s(580);
        addRaw (s, snfPREFIX);
        nh = s.getSHA512Half();
    }

    if (nh == mHash.as_uint256())
        return false;
    mHash = SHAMapHash{nh};
    return true;
}

void
SHAMapTreeNode::addRaw(Serializer& s, SHANodeFormat format) const
{
    assert ((format == snfPREFIX) || (format == snfWIRE) || (format == snfHASH));

    if (mType == tnERROR)
        Throw<std::runtime_error> ("invalid I node type");

    if (format == snfHASH)
    {
        s.add256 (mHash.as_uint256());
    }
    else if (mType == tnACCOUNT_STATE)
    {
        if (format == snfPREFIX)
        {
            s.add32 (HashPrefix::leafNode);
            s.addRaw (mItem->peekData ());
            s.add256 (mItem->key());
        }
        else
        {
            s.addRaw (mItem->peekData ());
            s.add256 (mItem->key());
            s.add8 (1);
        }
    }
    else if (mType == tnTRANSACTION_NM)
    {
        if (format == snfPREFIX)
        {
            s.add32 (HashPrefix::transactionID);
            s.addRaw (mItem->peekData ());
        }
        else
        {
            s.addRaw (mItem->peekData ());
            s.add8 (0);
        }
    }
    else if (mType == tnTRANSACTION_MD)
    {
        if (format == snfPREFIX)
        {
            s.add32 (HashPrefix::txNode);
            s.addRaw (mItem->peekData ());
            s.add256 (mItem->key());
        }
        else
        {
            s.addRaw (mItem->peekData ());
            s.add256 (mItem->key());
            s.add8 (4);
        }
    }
    else
        assert (false);
}

bool SHAMapTreeNode::setItem (std::shared_ptr<SHAMapItem const> const& i, TNType type)
{
    mType = type;
    mItem = i;
    assert (isLeaf ());
    assert (mSeq != 0);
    return updateHash ();
}

bool SHAMapInnerNode::isEmpty () const
{
    return mIsBranch == 0;
}

int SHAMapInnerNode::getBranchCount () const
{
    assert (isInner ());
    int count = 0;

    for (int i = 0; i < 16; ++i)
        if (!isEmptyBranch (i))
            ++count;

    return count;
}

#ifdef BEAST_DEBUG

void
SHAMapAbstractNode::dump(const SHAMapNodeID & id, beast::Journal journal)
{
    JLOG(journal.debug()) <<
        "SHAMapTreeNode(" << id.getNodeID () << ")";
}

#endif  // BEAST_DEBUG

std::string
SHAMapAbstractNode::getString(const SHAMapNodeID & id) const
{
    std::string ret = "NodeID(";
    ret += beast::lexicalCastThrow <std::string> (id.getDepth ());
    ret += ",";
    ret += to_string (id.getNodeID ());
    ret += ")";
    return ret;
}

std::string
SHAMapInnerNode::getString(const SHAMapNodeID & id) const
{
    std::string ret = SHAMapAbstractNode::getString(id);
    for (int i = 0; i < mHashes.size(); ++i)
    {
        if (!isEmptyBranch (i))
        {
            ret += "\nb";
            ret += beast::lexicalCastThrow <std::string> (i);
            ret += " = ";
            ret += to_string (mHashes[i]);
        }
    }
    return ret;
}

std::string
SHAMapTreeNode::getString(const SHAMapNodeID & id) const
{
    std::string ret = SHAMapAbstractNode::getString(id);
    if (mType == tnTRANSACTION_NM)
        ret += ",txn\n";
    else if (mType == tnTRANSACTION_MD)
        ret += ",txn+md\n";
    else if (mType == tnACCOUNT_STATE)
        ret += ",as\n";
    else
        ret += ",leaf\n";

    ret += "  Tag=";
    ret += to_string (peekItem()->key());
    ret += "\n  Hash=";
    ret += to_string (mHash);
    ret += "/";
    ret += beast::lexicalCast <std::string> (mItem->size());
    return ret;
}

// We are modifying an inner node
void
SHAMapInnerNode::setChild(int m, std::shared_ptr<SHAMapAbstractNode> const& child)
{
    assert ((m >= 0) && (m < 16));
    assert (mType == tnINNER);
    assert (mSeq != 0);
    assert (child.get() != this);
    mHashes[m].zero();
    mHash.zero();
    if (child)
        mIsBranch |= (1 << m);
    else
        mIsBranch &= ~ (1 << m);
    mChildren[m] = child;
}

// finished modifying, now make shareable
void SHAMapInnerNode::shareChild (int m, std::shared_ptr<SHAMapAbstractNode> const& child)
{
    assert ((m >= 0) && (m < 16));
    assert (mType == tnINNER);
    assert (mSeq != 0);
    assert (child);
    assert (child.get() != this);

    mChildren[m] = child;
}

SHAMapAbstractNode*
SHAMapInnerNode::getChildPointer (int branch)
{
    assert (branch >= 0 && branch < 16);
    assert (isInner());

    std::lock_guard <std::mutex> lock (childLock);
    return mChildren[branch].get ();
}

std::shared_ptr<SHAMapAbstractNode>
SHAMapInnerNode::getChild (int branch)
{
    assert (branch >= 0 && branch < 16);
    assert (isInner());

    std::lock_guard <std::mutex> lock (childLock);
    return mChildren[branch];
}

std::shared_ptr<SHAMapAbstractNode>
SHAMapInnerNode::canonicalizeChild(int branch, std::shared_ptr<SHAMapAbstractNode> node)
{
    assert (branch >= 0 && branch < 16);
    assert (isInner());
    assert (node);
    assert (node->getNodeHash() == mHashes[branch]);

    std::lock_guard <std::mutex> lock (childLock);
    if (mChildren[branch])
    {
        // There is already a node hooked up, return it
        node = mChildren[branch];
    }
    else
    {
        // Hook this node up
        // node must not be a v2 inner node
        assert(std::dynamic_pointer_cast<SHAMapInnerNodeV2>(node) == nullptr);
        mChildren[branch] = node;
    }
    return node;
}

std::shared_ptr<SHAMapAbstractNode>
SHAMapInnerNodeV2::canonicalizeChild(int branch, std::shared_ptr<SHAMapAbstractNode> node)
{
    assert (branch >= 0 && branch < 16);
    assert (isInner());
    assert (node);
    assert (node->getNodeHash() == mHashes[branch]);

    std::lock_guard <std::mutex> lock (childLock);
    if (mChildren[branch])
    {
        // There is already a node hooked up, return it
        node = mChildren[branch];
    }
    else
    {
        // Hook this node up
        // node must not be a v1 inner node
        assert(std::dynamic_pointer_cast<SHAMapInnerNodeV2>(node) != nullptr ||
               std::dynamic_pointer_cast<SHAMapTreeNode>(node)    != nullptr);
        mChildren[branch] = node;
    }
    return node;
}

bool
SHAMapInnerNodeV2::has_common_prefix(uint256 const& key) const
{
    auto x = common_.begin();
    auto y = key.begin();
    for (unsigned i = 0; i < depth_/2; ++i, ++x, ++y)
    {
        if (*x != *y)
            return false;
    }
    if (depth_ & 1)
    {
        auto i = depth_/2;
        return (*(common_.begin() + i) & 0xF0) == (*(key.begin() + i) & 0xF0);
    }
    return true;
}

int
SHAMapInnerNodeV2::get_common_prefix(uint256 const& key) const
{
    auto x = common_.begin();
    auto y = key.begin();
    auto r = 0;
    for (unsigned i = 0; i < depth_/2; ++i, ++x, ++y, r += 2)
    {
        if (*x != *y)
        {
            if ((*x & 0xF0) == (*y & 0xF0))
                ++r;
            return r;
        }
    }
    if (depth_ & 1)
    {
        auto i = depth_/2;
        if ((*(common_.begin() + i) & 0xF0) == (*(key.begin() + i) & 0xF0))
            ++r;
    }
    return r;
}

void
SHAMapInnerNodeV2::setChildren(std::shared_ptr<SHAMapTreeNode> const& child1,
                               std::shared_ptr<SHAMapTreeNode> const& child2)
{
    assert(child1->peekItem()->key() != child2->peekItem()->key());
    auto k1 = child1->peekItem()->key().begin();
    auto k2 = child2->peekItem()->key().begin();
    auto k = common_.begin();
    for (depth_ = 0; *k1 == *k2; ++depth_, ++k1, ++k2, ++k)
        *k = *k1;
    unsigned b1;
    unsigned b2;
    if ((*k1 & 0xF0) == (*k2 & 0xF0))
    {
        *k = *k1 & 0xF0;
        b1 = *k1 & 0x0F;
        b2 = *k2 & 0x0F;
        depth_ = 2*depth_ + 1;
    }
    else
    {
        b1 = *k1 >> 4;
        b2 = *k2 >> 4;
        depth_ = 2*depth_;
    }
    mChildren[b1] = child1;
    mIsBranch |= 1 << b1;
    mChildren[b2] = child2;
    mIsBranch |= 1 << b2;
}

void
SHAMapInnerNodeV2::set_common(int depth, uint256 const& common)
{
    depth_ = depth;
    common_ = common;
}

uint256 const&
SHAMapInnerNode::key() const
{
    Throw<std::logic_error>("SHAMapInnerNode::key() should never be called");
    static uint256 x;
    return x;
}

uint256 const&
SHAMapInnerNodeV2::key() const
{
    return common_;
}

uint256 const&
SHAMapTreeNode::key() const
{
    return mItem->key();
}

void
SHAMapInnerNode::invariants(bool is_v2, bool is_root) const
{
    assert(!is_v2);
    assert(mType == tnINNER);
    unsigned count = 0;
    for (int i = 0; i < 16; ++i)
    {
        if (mHashes[i].isNonZero())
        {
            assert((mIsBranch & (1 << i)) != 0);
            if (mChildren[i] != nullptr)
                mChildren[i]->invariants(is_v2);
            ++count;
        }
        else
        {
            assert((mIsBranch & (1 << i)) == 0);
        }
    }
    if (!is_root)
    {
        assert(mHash.isNonZero());
        assert(count >= 1);
    }
    assert((count == 0) ? mHash.isZero() : mHash.isNonZero());
}

void
SHAMapInnerNodeV2::invariants(bool is_v2, bool is_root) const
{
    assert(is_v2);
    assert(mType == tnINNER);
    unsigned count = 0;
    for (int i = 0; i < 16; ++i)
    {
        if (mHashes[i].isNonZero())
        {
            assert((mIsBranch & (1 << i)) != 0);
            if (mChildren[i] != nullptr)
            {
                assert(mHashes[i] == mChildren[i]->getNodeHash());
#ifndef NDEBUG
                auto const& childID = mChildren[i]->key();

                // Make sure this child it attached to the correct branch
                SHAMapNodeID nodeID {depth(), common()};
                assert (i == nodeID.selectBranch(childID));
#endif
                assert(has_common_prefix(childID));
                mChildren[i]->invariants(is_v2);
            }
            ++count;
        }
        else
        {
            assert((mIsBranch & (1 << i)) == 0);
        }
    }
    if (!is_root)
    {
        assert(mHash.isNonZero());
        assert(count >= 2);
        assert(depth_ > 0);
    }
    else
    {
        assert(depth_ == 0);
    }
    if (count == 0)
        assert(mHash.isZero());
    else
        assert(mHash.isNonZero());
}

void
SHAMapTreeNode::invariants(bool, bool) const
{
    assert(mType >= tnTRANSACTION_NM);
    assert(mHash.isNonZero());
    assert(mItem != nullptr);
}

} // ripple
