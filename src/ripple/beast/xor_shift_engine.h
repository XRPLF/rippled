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

#ifndef BEAST_RANDOM_XOR_SHIFT_ENGINE_H_INCLUDED
#define BEAST_RANDOM_XOR_SHIFT_ENGINE_H_INCLUDED

#include <cstdint>
#include <limits>
#include <stdexcept>

namespace beast {

namespace detail {

template <class = void>
class xor_shift_engine
{
public:
    using result_type = std::uint64_t;

    xor_shift_engine(xor_shift_engine const&) = default;
    xor_shift_engine&
    operator=(xor_shift_engine const&) = default;

    explicit xor_shift_engine(result_type val = 1977u);

    void
    seed(result_type seed);

    result_type
    operator()();

    static result_type constexpr min()
    {
        return std::numeric_limits<result_type>::min();
    }

    static result_type constexpr max()
    {
        return std::numeric_limits<result_type>::max();
    }

private:
    result_type s_[2];

    static result_type
    murmurhash3(result_type x);
};

template <class _>
xor_shift_engine<_>::xor_shift_engine(result_type val)
{
    seed(val);
}

template <class _>
void
xor_shift_engine<_>::seed(result_type seed)
{
    if (seed == 0)
        throw std::domain_error("invalid seed");
    s_[0] = murmurhash3(seed);
    s_[1] = murmurhash3(s_[0]);
}

template <class _>
auto
xor_shift_engine<_>::operator()() -> result_type
{
    result_type s1 = s_[0];
    result_type const s0 = s_[1];
    s_[0] = s0;
    s1 ^= s1 << 23;
    return (s_[1] = (s1 ^ s0 ^ (s1 >> 17) ^ (s0 >> 26))) + s0;
}

template <class _>
auto
xor_shift_engine<_>::murmurhash3(result_type x) -> result_type
{
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ULL;
    return x ^= x >> 33;
}

}  // namespace detail

/** XOR-shift Generator.

    Meets the requirements of UniformRandomNumberGenerator.

    Simple and fast RNG based on:
    http://xorshift.di.unimi.it/xorshift128plus.c
    does not accept seed==0
*/
using xor_shift_engine = detail::xor_shift_engine<>;

}  // namespace beast

#endif
