//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2014, Vinnie Falco <vinnie.falco@gmail.com>

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

#ifndef BEAST_HASH_XXHASHER_H_INCLUDED
#define BEAST_HASH_XXHASHER_H_INCLUDED

#ifndef BEAST_NO_XXHASH
#define BEAST_NO_XXHASH 0
#endif

#if ! BEAST_NO_XXHASH

#include <ripple/beast/hash/endian.h>
#include <ripple/beast/hash/impl/xxhash.h>
#include <type_traits>
#include <cstddef>

namespace beast {

class xxhasher
{
private:
    // requires 64-bit std::size_t
    static_assert(sizeof(std::size_t)==8, "");

    detail::XXH64_state_t state_;

public:
    using result_type = std::size_t;

    static beast::endian const endian = beast::endian::native;

    xxhasher() noexcept
    {
        detail::XXH64_reset (&state_, 1);
    }

    template <class Seed,
        std::enable_if_t<
            std::is_unsigned<Seed>::value>* = nullptr>
    explicit
    xxhasher (Seed seed)
    {
        detail::XXH64_reset (&state_, seed);
    }

    template <class Seed,
        std::enable_if_t<
            std::is_unsigned<Seed>::value>* = nullptr>
    xxhasher (Seed seed, Seed)
    {
        detail::XXH64_reset (&state_, seed);
    }

    void
    operator()(void const* key, std::size_t len) noexcept
    {
        detail::XXH64_update (&state_, key, len);
    }

    explicit
    operator std::size_t() noexcept
    {
        return detail::XXH64_digest(&state_);
    }
};

} // beast

#endif

#endif
