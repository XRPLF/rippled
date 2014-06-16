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

namespace ripple {

SHAMapTreeNode::SHAMapTreeNode (std::uint32_t seq, const SHAMapNode& nodeID)
    : SHAMapNode (nodeID)
    , mHash (std::uint64_t(0))
    , mSeq (seq)
    , mAccessSeq (seq)
    , mType (tnERROR)
    , mIsBranch (0)
    , mFullBelow (false)
{
}

SHAMapTreeNode::SHAMapTreeNode (const SHAMapTreeNode& node, std::uint32_t seq) : SHAMapNode (node),
    mHash (node.mHash), mSeq (seq), mType (node.mType), mIsBranch (node.mIsBranch), mFullBelow (false)
{
    if (node.mItem)
        mItem = node.mItem;
    else
        memcpy (mHashes, node.mHashes, sizeof (mHashes));
}

SHAMapTreeNode::SHAMapTreeNode (const SHAMapNode& node, SHAMapItem::ref item,
                                TNType type, std::uint32_t seq) :
    SHAMapNode (node), mItem (item), mSeq (seq), mType (type), mIsBranch (0), mFullBelow (false)
{
    assert (item->peekData ().size () >= 12);
    updateHash ();
}

SHAMapTreeNode::SHAMapTreeNode (const SHAMapNode& id, Blob const& rawNode, std::uint32_t seq,
                                SHANodeFormat format, uint256 const& hash, bool hashValid) :
    SHAMapNode (id), mSeq (seq), mType (tnERROR), mIsBranch (0), mFullBelow (false)
{
    if (format == snfWIRE)
    {
        Serializer s (rawNode);
        int type = s.removeLastByte ();
        int len = s.getLength ();

        if ((type < 0) || (type > 4))
        {
#ifdef BEAST_DEBUG
            Log::out() << "Invalid wire format node";
            Log::out() << strHex (rawNode);
            assert (false);
#endif
            throw std::runtime_error ("invalid node AW type");
        }

        if (type == 0)
        {
            // transaction
            mItem = std::make_shared<SHAMapItem> (s.getPrefixHash (HashPrefix::transactionID), s.peekData ());
            mType = tnTRANSACTION_NM;
        }
        else if (type == 1)
        {
            // account state
            if (len < (256 / 8))
                throw std::runtime_error ("short AS node");

            uint256 u;
            s.get256 (u, len - (256 / 8));
            s.chop (256 / 8);

            if (u.isZero ()) throw std::runtime_error ("invalid AS node");

            mItem = std::make_shared<SHAMapItem> (u, s.peekData ());
            mType = tnACCOUNT_STATE;
        }
        else if (type == 2)
        {
            // full inner
            if (len != 512)
                throw std::runtime_error ("invalid FI node");

            for (int i = 0; i < 16; ++i)
            {
                s.get256 (mHashes[i], i * 32);

                if (mHashes[i].isNonZero ())
                    mIsBranch |= (1 << i);
            }

            mType = tnINNER;
        }
        else if (type == 3)
        {
            // compressed inner
            for (int i = 0; i < (len / 33); ++i)
            {
                int pos;
                s.get8 (pos, 32 + (i * 33));

                if ((pos < 0) || (pos >= 16)) throw std::runtime_error ("invalid CI node");

                s.get256 (mHashes[pos], i * 33);

                if (mHashes[pos].isNonZero ())
                    mIsBranch |= (1 << pos);
            }

            mType = tnINNER;
        }
        else if (type == 4)
        {
            // transaction with metadata
            if (len < (256 / 8))
                throw std::runtime_error ("short TM node");

            uint256 u;
            s.get256 (u, len - (256 / 8));
            s.chop (256 / 8);

            if (u.isZero ())
                throw std::runtime_error ("invalid TM node");

            mItem = std::make_shared<SHAMapItem> (u, s.peekData ());
            mType = tnTRANSACTION_MD;
        }
    }

    else if (format == snfPREFIX)
    {
        if (rawNode.size () < 4)
        {
            WriteLog (lsINFO, SHAMapNode) << "size < 4";
            throw std::runtime_error ("invalid P node");
        }

        std::uint32_t prefix = rawNode[0];
        prefix <<= 8;
        prefix |= rawNode[1];
        prefix <<= 8;
        prefix |= rawNode[2];
        prefix <<= 8;
        prefix |= rawNode[3];
        Serializer s (rawNode.begin () + 4, rawNode.end ());

        if (prefix == HashPrefix::transactionID)
        {
            mItem = std::make_shared<SHAMapItem> (Serializer::getSHA512Half (rawNode), s.peekData ());
            mType = tnTRANSACTION_NM;
        }
        else if (prefix == HashPrefix::leafNode)
        {
            if (s.getLength () < 32)
                throw std::runtime_error ("short PLN node");

            uint256 u;
            s.get256 (u, s.getLength () - 32);
            s.chop (32);

            if (u.isZero ())
            {
                WriteLog (lsINFO, SHAMapNode) << "invalid PLN node";
                throw std::runtime_error ("invalid PLN node");
            }

            mItem = std::make_shared<SHAMapItem> (u, s.peekData ());
            mType = tnACCOUNT_STATE;
        }
        else if (prefix == HashPrefix::innerNode)
        {
            if (s.getLength () != 512)
                throw std::runtime_error ("invalid PIN node");

            for (int i = 0; i < 16; ++i)
            {
                s.get256 (mHashes[i], i * 32);

                if (mHashes[i].isNonZero ())
                    mIsBranch |= (1 << i);
            }

            mType = tnINNER;
        }
        else if (prefix == HashPrefix::txNode)
        {
            // transaction with metadata
            if (s.getLength () < 32)
                throw std::runtime_error ("short TXN node");

            uint256 txID;
            s.get256 (txID, s.getLength () - 32);
            s.chop (32);
            mItem = std::make_shared<SHAMapItem> (txID, s.peekData ());
            mType = tnTRANSACTION_MD;
        }
        else
        {
            WriteLog (lsINFO, SHAMapNode) << "Unknown node prefix " << std::hex << prefix << std::dec;
            throw std::runtime_error ("invalid node prefix");
        }
    }

    else
    {
        assert (false);
        throw std::runtime_error ("Unknown format");
    }

    if (hashValid)
    {
        mHash = hash;
#if RIPPLE_VERIFY_NODEOBJECT_KEYS
        updateHash ();
        assert (mHash == hash);
#endif
    }
    else
        updateHash ();
}

bool SHAMapTreeNode::updateHash ()
{
    uint256 nh;

    if (mType == tnINNER)
    {
        if (mIsBranch != 0)
        {
            nh = Serializer::getPrefixHash (HashPrefix::innerNode, reinterpret_cast<unsigned char*> (mHashes), sizeof (mHashes));
#if RIPPLE_VERIFY_NODEOBJECT_KEYS
            Serializer s;
            s.add32 (HashPrefix::innerNode);

            for (int i = 0; i < 16; ++i)
                s.add256 (mHashes[i]);

            assert (nh == s.getSHA512Half ());
#endif
        }
        else
            nh.zero ();
    }
    else if (mType == tnTRANSACTION_NM)
    {
        nh = Serializer::getPrefixHash (HashPrefix::transactionID, mItem->peekData ());
    }
    else if (mType == tnACCOUNT_STATE)
    {
        Serializer s (mItem->peekSerializer ().getDataLength () + (256 + 32) / 8);
        s.add32 (HashPrefix::leafNode);
        s.addRaw (mItem->peekData ());
        s.add256 (mItem->getTag ());
        nh = s.getSHA512Half ();
    }
    else if (mType == tnTRANSACTION_MD)
    {
        Serializer s (mItem->peekSerializer ().getDataLength () + (256 + 32) / 8);
        s.add32 (HashPrefix::txNode);
        s.addRaw (mItem->peekData ());
        s.add256 (mItem->getTag ());
        nh = s.getSHA512Half ();
    }
    else
        assert (false);

    if (nh == mHash)
        return false;

    mHash = nh;
    return true;
}

void SHAMapTreeNode::addRaw (Serializer& s, SHANodeFormat format)
{
    assert ((format == snfPREFIX) || (format == snfWIRE) || (format == snfHASH));

    if (mType == tnERROR)
        throw std::runtime_error ("invalid I node type");

    if (format == snfHASH)
    {
        s.add256 (getNodeHash ());
    }
    else if (mType == tnINNER)
    {
        assert (!isEmpty ());

        if (format == snfPREFIX)
        {
            s.add32 (HashPrefix::innerNode);

            for (int i = 0; i < 16; ++i)
                s.add256 (mHashes[i]);
        }
        else
        {
            if (getBranchCount () < 12)
            {
                // compressed node
                for (int i = 0; i < 16; ++i)
                    if (!isEmptyBranch (i))
                    {
                        s.add256 (mHashes[i]);
                        s.add8 (i);
                    }

                s.add8 (3);
            }
            else
            {
                for (int i = 0; i < 16; ++i)
                    s.add256 (mHashes[i]);

                s.add8 (2);
            }
        }
    }
    else if (mType == tnACCOUNT_STATE)
    {
        if (format == snfPREFIX)
        {
            s.add32 (HashPrefix::leafNode);
            s.addRaw (mItem->peekData ());
            s.add256 (mItem->getTag ());
        }
        else
        {
            s.addRaw (mItem->peekData ());
            s.add256 (mItem->getTag ());
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
            s.add256 (mItem->getTag ());
        }
        else
        {
            s.addRaw (mItem->peekData ());
            s.add256 (mItem->getTag ());
            s.add8 (4);
        }
    }
    else
        assert (false);
}

bool SHAMapTreeNode::setItem (SHAMapItem::ref i, TNType type)
{
    mType = type;
    mItem = i;
    assert (isLeaf ());
    assert (mSeq != 0);
    return updateHash ();
}

bool SHAMapTreeNode::isEmpty () const
{
    return mIsBranch == 0;
}

int SHAMapTreeNode::getBranchCount () const
{
    assert (isInner ());
    int count = 0;

    for (int i = 0; i < 16; ++i)
        if (!isEmptyBranch (i))
            ++count;

    return count;
}

void SHAMapTreeNode::makeInner ()
{
    mItem.reset ();
    mIsBranch = 0;
    memset (mHashes, 0, sizeof (mHashes));
    mType = tnINNER;
    mHash.zero ();
}

void SHAMapTreeNode::dump ()
{
    WriteLog (lsDEBUG, SHAMapNode) << "SHAMapTreeNode(" << getNodeID () << ")";
}

std::string SHAMapTreeNode::getString () const
{
    std::string ret = "NodeID(";
    ret += beast::lexicalCastThrow <std::string> (getDepth ());
    ret += ",";
    ret += to_string (getNodeID ());
    ret += ")";

    if (isInner ())
    {
        for (int i = 0; i < 16; ++i)
            if (!isEmptyBranch (i))
            {
                ret += "\nb";
                ret += beast::lexicalCastThrow <std::string> (i);
                ret += " = ";
                ret += to_string (mHashes[i]);
            }
    }

    if (isLeaf ())
    {
        if (mType == tnTRANSACTION_NM)
            ret += ",txn\n";
        else if (mType == tnTRANSACTION_MD)
            ret += ",txn+md\n";
        else if (mType == tnACCOUNT_STATE)
            ret += ",as\n";
        else
            ret += ",leaf\n";

        ret += "  Tag=";
        ret += to_string (getTag ());
        ret += "\n  Hash=";
        ret += to_string (mHash);
        ret += "/";
        ret += beast::lexicalCast <std::string> (mItem->peekSerializer ().getDataLength ());
    }

    return ret;
}

bool SHAMapTreeNode::setChildHash (int m, uint256 const& hash)
{
    assert ((m >= 0) && (m < 16));
    assert (mType == tnINNER);
    assert (mSeq != 0);

    if (mHashes[m] == hash)
        return false;

    mHashes[m] = hash;

    if (hash.isNonZero ())
        mIsBranch |= (1 << m);
    else
        mIsBranch &= ~ (1 << m);

    return updateHash ();
}

} // ripple
