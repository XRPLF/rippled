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

#include <ripple/basics/Buffer.h>
#include <ripple/basics/CountedObject.h>
#include <ripple/basics/Slice.h>
#include <ripple/basics/base_uint.h>

namespace ripple {

// an item stored in a SHAMap
class SHAMapItem : public CountedObject<SHAMapItem>
{
private:
    uint256 tag_;
    Buffer data_;

public:
    SHAMapItem() = delete;

    SHAMapItem(uint256 const& tag, Slice data) : tag_(tag), data_(data)
    {
    }

    uint256 const&
    key() const
    {
        return tag_;
    }

    Slice
    slice() const
    {
        return static_cast<Slice>(data_);
    }

    std::size_t
    size() const
    {
        return data_.size();
    }

    void const*
    data() const
    {
        return data_.data();
    }
};

}  // namespace ripple

#endif
