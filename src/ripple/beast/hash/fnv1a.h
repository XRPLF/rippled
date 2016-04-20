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

#ifndef BEAST_HASH_FNV1A_H_INCLUDED
#define BEAST_HASH_FNV1A_H_INCLUDED

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace beast {

// See http://www.isthe.com/chongo/tech/comp/fnv/
//
class fnv1a
{
private:
    std::uint64_t state_ = 14695981039346656037ULL;

public:
    using result_type = std::size_t;

    fnv1a() = default;

    template <class Seed,
        std::enable_if_t<
            std::is_unsigned<Seed>::value>* = nullptr>
    explicit
    fnv1a (Seed seed)
    {
        append (&seed, sizeof(seed));
    }

    void
    operator() (void const* key, std::size_t len) noexcept
    {
        unsigned char const* p =
            static_cast<unsigned char const*>(key);
        unsigned char const* const e = p + len;
        for (; p < e; ++p)
            state_ = (state_ ^ *p) * 1099511628211ULL;
    }

    explicit
    operator std::size_t() noexcept
    {
        return static_cast<std::size_t>(state_);
    }
};

} // beast

#endif
