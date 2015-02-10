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

#ifndef RIPPLE_SHAMAP_SHAMAPITEM_H_INCLUDED
#define RIPPLE_SHAMAP_SHAMAPITEM_H_INCLUDED

#include <ripple/protocol/Serializer.h>
#include <ripple/basics/base_uint.h>
#include <beast/utility/Journal.h>

#include <cstddef>

namespace ripple {

// an item stored in a SHAMap
class SHAMapItem
{
private:
    uint256 mTag;
    Serializer mData;

public:
    explicit SHAMapItem (uint256 const& tag);
    SHAMapItem (uint256 const& tag, Blob const & data);
    SHAMapItem (uint256 const& tag, Serializer const& s);

    uint256 const& getTag() const;
    Blob const& peekData() const;
    Serializer& peekSerializer();

public:  // public only to SHAMapTreeNode
    std::size_t size() const;

private:
    explicit SHAMapItem (Blob const& data);

    void const* data() const;
    void addRaw (Blob& s) const;
    void dump (beast::Journal journal);
};

inline
SHAMapItem::SHAMapItem (uint256 const& tag)
    : mTag (tag)
{
}

inline
std::size_t
SHAMapItem::size() const
{
    return mData.peekData().size();
}

inline
void const*
SHAMapItem::data() const
{
    return mData.peekData().data();
}

inline
uint256 const&
SHAMapItem::getTag() const
{
    return mTag;
}

inline
Blob const&
SHAMapItem::peekData() const
{
    return mData.peekData();
}

inline
Serializer& 
SHAMapItem::peekSerializer()
{
    return mData;
}

inline
void
SHAMapItem::addRaw (Blob& s) const
{
    s.insert (s.end (), mData.begin (), mData.end ());
}

} // ripple

#endif
