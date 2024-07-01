//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2021 Ripple Labs Inc.

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

#ifndef RIPPLE_BASICS_SHAMAP_HASH_H_INCLUDED
#define RIPPLE_BASICS_SHAMAP_HASH_H_INCLUDED

#include <ripple/basics/base_uint.h>

#include <ostream>

namespace ripple {

// A SHAMapHash is the hash of a node in a SHAMap, and also the
// type of the hash of the entire SHAMap.

class SHAMapHash
{
    uint256 hash_;

public:
    SHAMapHash() = default;
    explicit SHAMapHash(uint256 const& hash) : hash_(hash)
    {
    }

    uint256 const&
    as_uint256() const
    {
        return hash_;
    }
    uint256&
    as_uint256()
    {
        return hash_;
    }
    bool
    isZero() const
    {
        return hash_.isZero();
    }
    bool
    isNonZero() const
    {
        return hash_.isNonZero();
    }
    int
    signum() const
    {
        return hash_.signum();
    }
    void
    zero()
    {
        hash_.zero();
    }

    friend bool
    operator==(SHAMapHash const& x, SHAMapHash const& y)
    {
        return x.hash_ == y.hash_;
    }

    friend bool
    operator<(SHAMapHash const& x, SHAMapHash const& y)
    {
        return x.hash_ < y.hash_;
    }

    friend std::ostream&
    operator<<(std::ostream& os, SHAMapHash const& x)
    {
        return os << x.hash_;
    }

    friend std::string
    to_string(SHAMapHash const& x)
    {
        return to_string(x.hash_);
    }

    template <class H>
    friend void
    hash_append(H& h, SHAMapHash const& x)
    {
        hash_append(h, x.hash_);
    }
};

inline bool
operator!=(SHAMapHash const& x, SHAMapHash const& y)
{
    return !(x == y);
}

}  // namespace ripple

#endif  // RIPPLE_BASICS_SHAMAP_HASH_H_INCLUDED
