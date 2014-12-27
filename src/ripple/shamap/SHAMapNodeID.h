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

#ifndef RIPPLE_SHAMAPNODEID_H
#define RIPPLE_SHAMAPNODEID_H

#include <ripple/protocol/Serializer.h>
#include <ripple/basics/base_uint.h>
#include <beast/utility/Journal.h>
#include <ostream>
#include <string>
#include <tuple>

namespace ripple {

// Identifies a node in a SHA256 hash map
class SHAMapNodeID
{
private:
    uint256 mNodeID;
    int mDepth;
    mutable size_t  mHash;

public:
    SHAMapNodeID () : mDepth (0), mHash (0)
    {
    }

    SHAMapNodeID (int depth, uint256 const& hash);
    SHAMapNodeID (void const* ptr, int len);

protected:
    SHAMapNodeID (int depth, uint256 const& id, bool)
        : mNodeID (id), mDepth (depth), mHash (0)
    {
    }

public:
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
        if (mHash == 0)
            mHash = calculate_hash (mNodeID, mDepth);
        return mHash;
    }

    SHAMapNodeID getParentNodeID () const
    {
        assert (mDepth);
        return SHAMapNodeID (mDepth - 1, mNodeID);
    }

    SHAMapNodeID getChildNodeID (int m) const;
    int selectBranch (uint256 const& hash) const;

    bool operator< (const SHAMapNodeID& n) const
    {
        return std::tie(mDepth, mNodeID) < std::tie(n.mDepth, n.mNodeID);
    }
    bool operator> (const SHAMapNodeID& n) const {return n < *this;}
    bool operator<= (const SHAMapNodeID& n) const {return !(*this < n);}
    bool operator>= (const SHAMapNodeID& n) const {return !(n < *this);}

    bool operator== (const SHAMapNodeID& n) const
    {
        return (mDepth == n.mDepth) && (mNodeID == n.mNodeID);
    }
    bool operator!= (const SHAMapNodeID& n) const {return !(*this == n);}

    bool operator== (uint256 const& n) const
    {
        return n == mNodeID;
    }
    bool operator!= (uint256 const& n) const {return !(*this == n);}

    virtual std::string getString () const;
    void dump (beast::Journal journal) const;

    static uint256 getNodeID (int depth, uint256 const& hash);

    // Convert to/from wire format (256-bit nodeID, 1-byte depth)
    void addIDRaw (Serializer& s) const;
    std::string getRawString () const;
    static int getRawIDLength (void)
    {
        return 33;
    }

private:
    static
    uint256 const&
    Masks (int depth);

    static
    std::size_t
    calculate_hash (uint256 const& node, int depth);
};

//------------------------------------------------------------------------------

inline std::ostream& operator<< (std::ostream& out, SHAMapNodeID const& node)
{
    return out << node.getString ();
}

//------------------------------------------------------------------------------

class SHAMapNode_hash
{
public:
    typedef ripple::SHAMapNodeID argument_type;
    typedef std::size_t result_type;

    result_type
    operator() (argument_type const& key) const noexcept
    {
        return key.getMHash ();
    }
};

} // ripple

#endif
