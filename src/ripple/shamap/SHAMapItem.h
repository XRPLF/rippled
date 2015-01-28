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

#ifndef RIPPLE_SHAMAPITEM_H
#define RIPPLE_SHAMAPITEM_H

#include <ripple/basics/CountedObject.h>
#include <ripple/protocol/Serializer.h>
#include <ripple/basics/base_uint.h>
#include <beast/utility/Journal.h>
#include <memory>

namespace ripple {

// an item stored in a SHAMap
class SHAMapItem
    : public CountedObject <SHAMapItem>
{
public:
    static char const* getCountedObjectName () { return "SHAMapItem"; }

    typedef std::shared_ptr<SHAMapItem>           pointer;
    typedef const std::shared_ptr<SHAMapItem>&    ref;

public:
    explicit SHAMapItem (uint256 const& tag)
        : mTag (tag)
    {
    }
    
    // tag computed from hash of data
    explicit SHAMapItem (Blob const& data);
    
    SHAMapItem (uint256 const& tag, Blob const & data);

    SHAMapItem (uint256 const& tag, Serializer const& s);

    std::size_t
    size() const
    {
        return mData.peekData().size();
    }

    void const*
    data() const
    {
        return mData.peekData().data();
    }

    uint256 const& getTag() const
    {
        return mTag;
    }

    Blob const& peekData() const
    {
        return mData.peekData();
    }

    Serializer& peekSerializer()
    {
        return mData;
    }

    void addRaw (Blob& s) const
    {
        s.insert (s.end (), mData.begin (), mData.end ());
    }

    // VFALCO Why is this virtual?
    virtual void dump (beast::Journal journal);

private:
    uint256 mTag;
    Serializer mData;
};

} // ripple

#endif
