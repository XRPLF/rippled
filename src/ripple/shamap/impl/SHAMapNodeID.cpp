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

#include <ripple/basics/Log.h>
#include <ripple/beast/core/LexicalCast.h>
#include <ripple/crypto/csprng.h>
#include <ripple/shamap/SHAMapNodeID.h>
#include <boost/format.hpp>
#include <cassert>
#include <cstring>

namespace ripple {

uint256 const&
SHAMapNodeID::Masks(int depth)
{
    enum { mask_size = 65 };

    struct masks_t
    {
        uint256 entry[mask_size];

        masks_t()
        {
            uint256 selector;
            for (int i = 0; i < mask_size - 1; i += 2)
            {
                entry[i] = selector;
                *(selector.begin() + (i / 2)) = 0xF0;
                entry[i + 1] = selector;
                *(selector.begin() + (i / 2)) = 0xFF;
            }
            entry[mask_size - 1] = selector;
        }
    };
    static masks_t const masks;
    return masks.entry[depth];
}

// canonicalize the hash to a node ID for this depth
SHAMapNodeID::SHAMapNodeID(int depth, uint256 const& hash)
    : mNodeID(hash), mDepth(depth)
{
    assert((depth >= 0) && (depth < 65));
    assert(mNodeID == (mNodeID & Masks(depth)));
}

SHAMapNodeID::SHAMapNodeID(void const* ptr, int len)
{
    if (len < 33)
        mDepth = -1;
    else
    {
        std::memcpy(mNodeID.begin(), ptr, 32);
        mDepth = *(static_cast<unsigned char const*>(ptr) + 32);
    }
}

std::string
SHAMapNodeID::getString() const
{
    if ((mDepth == 0) && (mNodeID.isZero()))
        return "NodeID(root)";

    return "NodeID(" + std::to_string(mDepth) + "," + to_string(mNodeID) + ")";
}

void
SHAMapNodeID::addIDRaw(Serializer& s) const
{
    s.add256(mNodeID);
    s.add8(mDepth);
}

std::string
SHAMapNodeID::getRawString() const
{
    Serializer s(33);
    addIDRaw(s);
    return s.getString();
}

// This can be optimized to avoid the << if needed
SHAMapNodeID
SHAMapNodeID::getChildNodeID(int m) const
{
    assert((m >= 0) && (m < 16));
    assert(mDepth < 64);

    uint256 child(mNodeID);
    child.begin()[mDepth / 2] |= (mDepth & 1) ? m : (m << 4);

    return SHAMapNodeID(mDepth + 1, child);
}

// Which branch would contain the specified hash
int
SHAMapNodeID::selectBranch(uint256 const& hash) const
{
    int branch = *(hash.begin() + (mDepth / 2));

    if (mDepth & 1)
        branch &= 0xf;
    else
        branch >>= 4;

    assert((branch >= 0) && (branch < 16));

    return branch;
}

bool
SHAMapNodeID::has_common_prefix(SHAMapNodeID const& other) const
{
    assert(mDepth <= other.mDepth);
    auto x = mNodeID.begin();
    auto y = other.mNodeID.begin();
    for (unsigned i = 0; i < mDepth / 2; ++i, ++x, ++y)
    {
        if (*x != *y)
            return false;
    }
    if (mDepth & 1)
    {
        auto i = mDepth / 2;
        return (*(mNodeID.begin() + i) & 0xF0) ==
            (*(other.mNodeID.begin() + i) & 0xF0);
    }
    return true;
}

void
SHAMapNodeID::dump(beast::Journal journal) const
{
    JLOG(journal.debug()) << getString();
}

}  // namespace ripple
