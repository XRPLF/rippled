//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2014, Howard Hinnant <howard.hinnant@gmail.com>,
        Vinnie Falco <vinnie.falco@gmail.com

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

#ifndef BEAST_HASH_SPOOKY_H_INCLUDED
#define BEAST_HASH_SPOOKY_H_INCLUDED

#include <ripple/beast/hash/endian.h>
#include <ripple/beast/hash/impl/spookyv2.h>

namespace beast {

// See http://burtleburtle.net/bob/hash/spooky.html
class spooky
{
private:
    SpookyHash state_;

public:
    using result_type = std::size_t;
    static beast::endian const endian = beast::endian::native;

    spooky (std::size_t seed1 = 1, std::size_t seed2 = 2) noexcept
    {
        state_.Init (seed1, seed2);
    }

    void
    operator() (void const* key, std::size_t len) noexcept
    {
        state_.Update (key, len);
    }

    explicit
    operator std::size_t() noexcept
    {
        std::uint64_t h1, h2;
        state_.Final (&h1, &h2);
        return static_cast <std::size_t> (h1);
    }
};

} // beast

#endif
