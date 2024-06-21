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

#include <boost/endian/conversion.hpp>
#include <xxhash.h>

#include <cstddef>
#include <new>
#include <type_traits>

namespace beast {

class xxhasher
{
private:
    // requires 64-bit std::size_t
    static_assert(sizeof(std::size_t) == 8, "");

    XXH3_state_t* state_;

    static XXH3_state_t*
    allocState()
    {
        auto ret = XXH3_createState();
        if (ret == nullptr)
            throw std::bad_alloc();
        return ret;
    }

public:
    using result_type = std::size_t;

    static constexpr auto const endian = boost::endian::order::native;

    xxhasher(xxhasher const&) = delete;
    xxhasher&
    operator=(xxhasher const&) = delete;

    xxhasher()
    {
        state_ = allocState();
        XXH3_64bits_reset(state_);
    }

    ~xxhasher() noexcept
    {
        XXH3_freeState(state_);
    }

    template <
        class Seed,
        std::enable_if_t<std::is_unsigned<Seed>::value>* = nullptr>
    explicit xxhasher(Seed seed)
    {
        state_ = allocState();
        XXH3_64bits_reset_withSeed(state_, seed);
    }

    template <
        class Seed,
        std::enable_if_t<std::is_unsigned<Seed>::value>* = nullptr>
    xxhasher(Seed seed, Seed)
    {
        state_ = allocState();
        XXH3_64bits_reset_withSeed(state_, seed);
    }

    void
    operator()(void const* key, std::size_t len) noexcept
    {
        XXH3_64bits_update(state_, key, len);
    }

    explicit operator std::size_t() noexcept
    {
        return XXH3_64bits_digest(state_);
    }
};

}  // namespace beast

#endif
