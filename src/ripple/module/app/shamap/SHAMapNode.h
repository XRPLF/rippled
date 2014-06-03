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

#ifndef RIPPLE_SHAMAPNODE_H
#define RIPPLE_SHAMAPNODE_H

#include <functional>

namespace ripple {

// Identifies a node in a SHA256 hash map
class SHAMapNode
{
public:
    SHAMapNode () : mDepth (0), mHash (0)
    {
        ;
    }
    SHAMapNode (int depth, uint256 const& hash);

    int getDepth () const
    {
        return mDepth;
    }
    uint256 const& getNodeID ()  const
    {
        return mNodeID;
    }
    bool isValid () const
    {
        return (mDepth >= 0) && (mDepth < 64);
    }
    bool isRoot () const
    {
        return mDepth == 0;
    }
    size_t getMHash () const
    {
        if (mHash == 0) setMHash ();

        return mHash;
    }

    virtual bool isPopulated () const
    {
        return false;
    }

    SHAMapNode getParentNodeID () const
    {
        assert (mDepth);
        return SHAMapNode (mDepth - 1, mNodeID);
    }

    SHAMapNode getChildNodeID (int m) const;
    int selectBranch (uint256 const& hash) const;

    bool operator< (const SHAMapNode&) const;
    bool operator> (const SHAMapNode&) const;
    bool operator<= (const SHAMapNode&) const;
    bool operator>= (const SHAMapNode&) const;

    bool operator== (const SHAMapNode& n) const
    {
        return (mDepth == n.mDepth) && (mNodeID == n.mNodeID);
    }
    bool operator== (uint256 const& n) const
    {
        return n == mNodeID;
    }
    bool operator!= (const SHAMapNode& n) const
    {
        return (mDepth != n.mDepth) || (mNodeID != n.mNodeID);
    }
    bool operator!= (uint256 const& n) const
    {
        return n != mNodeID;
    }
    void set (SHAMapNode const& from)
    {
        mNodeID = from.mNodeID;
        mDepth = from.mDepth;
        mHash = from.mHash;
    }

    virtual std::string getString () const;
    void dump () const;

    static bool ClassInit ();
    static uint256 getNodeID (int depth, uint256 const& hash);

    // Convert to/from wire format (256-bit nodeID, 1-byte depth)
    void addIDRaw (Serializer& s) const;
    std::string getRawString () const;
    static int getRawIDLength (void)
    {
        return 33;
    }
    SHAMapNode (const void* ptr, int len);

protected:
    SHAMapNode (int depth, uint256 const& id, bool) : mNodeID (id), mDepth (depth), mHash (0)
    {
        ;
    }

private:
    static uint256 smMasks[65]; // AND with hash to get node id

    uint256 mNodeID;
    int     mDepth;
    mutable size_t  mHash;

    void setMHash () const;
};

extern std::size_t hash_value (const SHAMapNode& mn);

inline std::ostream& operator<< (std::ostream& out, const SHAMapNode& node)
{
    return out << node.getString ();
}

class SHAMapNode_hash
{
public:
    typedef ripple::SHAMapNode argument_type;
    typedef std::size_t result_type;

    result_type
    operator() (argument_type const& key) const noexcept
    {
        return key.getMHash ();
    }
};

}

//------------------------------------------------------------------------------

/*
namespace std {

template <>
struct hash <ripple::SHAMapNode>
{
    std::size_t operator() (ripple::SHAMapNode const& value) const
    {
        return value.getMHash ();
    }
};

}
*/

//------------------------------------------------------------------------------

/*
namespace boost {

template <>
struct hash <ripple::SHAMapNode> : std::hash <ripple::SHAMapNode>
{
};

}
*/

#endif
