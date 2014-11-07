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

#include <ripple/app/shamap/SHAMapNodeID.h>
#include <ripple/crypto/RandomNumbers.h>
#include <beast/module/core/text/LexicalCast.h>
#include <beast/utility/static_initializer.h>
#include <boost/format.hpp>
#include <cassert>
#include <cstring>

namespace ripple {

uint256 const&
SHAMapNodeID::Masks (int depth)
{
    enum
    {
        mask_size = 65
    };

    struct masks_t
    {
        uint256 entry [mask_size];

        masks_t()
        {
            uint256 selector;
            for (int i = 0; i < mask_size-1; i += 2)
            {
                entry[i] = selector;
                * (selector.begin () + (i / 2)) = 0xF0;
                entry[i + 1] = selector;
                * (selector.begin () + (i / 2)) = 0xFF;
            }
            entry[mask_size-1] = selector;
        }
    };
    static beast::static_initializer <masks_t> masks;
    return masks->entry[depth];
}

std::size_t
SHAMapNodeID::calculate_hash (uint256 const& node, int depth)
{
    struct HashParams
    {
        HashParams ()
            : golden_ratio (0x9e3779b9)
        {
            RandomNumbers::getInstance ().fill (&cookie_value);
        }

        // The cookie value protects us against algorithmic complexity attacks.
        std::size_t cookie_value;
        std::size_t golden_ratio;
    };

    static beast::static_initializer <HashParams> params;

    std::size_t h = params->cookie_value + (depth * params->golden_ratio);

    auto ptr = reinterpret_cast <const unsigned int*> (node.cbegin ());

    for (int i = (depth + 7) / 8; i != 0; --i)
        h = (h * params->golden_ratio) ^ *ptr++;

    return h;
}

// canonicalize the hash to a node ID for this depth
SHAMapNodeID::SHAMapNodeID (int depth, uint256 const& hash)
    : mNodeID (hash), mDepth (depth), mHash (0)
{
    assert ((depth >= 0) && (depth < 65));
    mNodeID &= Masks(depth);
}

SHAMapNodeID::SHAMapNodeID (void const* ptr, int len) : mHash (0)
{
    if (len < 33)
        mDepth = -1;
    else
    {
        std::memcpy (mNodeID.begin (), ptr, 32);
        mDepth = * (static_cast<unsigned char const*> (ptr) + 32);
    }
}

std::string SHAMapNodeID::getString () const
{
    if ((mDepth == 0) && (mNodeID.isZero ()))
        return "NodeID(root)";

    return "NodeID(" + std::to_string (mDepth) +
        "," + to_string (mNodeID) + ")";
}

uint256 SHAMapNodeID::getNodeID (int depth, uint256 const& hash)
{
    assert ((depth >= 0) && (depth <= 64));
    return hash & Masks(depth);
}

void SHAMapNodeID::addIDRaw (Serializer& s) const
{
    s.add256 (mNodeID);
    s.add8 (mDepth);
}

std::string SHAMapNodeID::getRawString () const
{
    Serializer s (33);
    addIDRaw (s);
    return s.getString ();
}

// This can be optimized to avoid the << if needed
SHAMapNodeID SHAMapNodeID::getChildNodeID (int m) const
{
    assert ((m >= 0) && (m < 16));
    assert (mDepth <= 64);

    uint256 child (mNodeID);
    child.begin ()[mDepth / 2] |= (mDepth & 1) ? m : (m << 4);

    return SHAMapNodeID (mDepth + 1, child, true);
}

// Which branch would contain the specified hash
int SHAMapNodeID::selectBranch (uint256 const& hash) const
{
#if RIPPLE_VERIFY_NODEOBJECT_KEYS

    if (mDepth >= 64)
    {
        assert (false);
        return -1;
    }

    if ((hash & Masks(mDepth)) != mNodeID)
    {
        std::cerr << "selectBranch(" << getString () << std::endl;
        std::cerr << "  " << hash << " off branch" << std::endl;
        assert (false);
        return -1;  // does not go under this node
    }

#endif

    int branch = * (hash.begin () + (mDepth / 2));

    if (mDepth & 1)
        branch &= 0xf;
    else
        branch >>= 4;

    assert ((branch >= 0) && (branch < 16));

    return branch;
}

void SHAMapNodeID::dump () const
{
    WriteLog (lsDEBUG, SHAMapNodeID) << getString ();
}

} // ripple
