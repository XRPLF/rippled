//
// Copyright (c) 2015-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NUDB_TEST_XOR_SHIFT_ENGINE_HPP
#define NUDB_TEST_XOR_SHIFT_ENGINE_HPP

#include <cstdint>
#include <limits>
#include <stdexcept>

namespace nudb {
namespace test {

/** XOR-shift Generator.

    Meets the requirements of UniformRandomNumberGenerator.

    Simple and fast RNG based on:
    http://xorshift.di.unimi.it/xorshift128plus.c
    does not accept seed==0
*/
class xor_shift_engine
{
public:
    using result_type = std::uint64_t;

    xor_shift_engine(xor_shift_engine const&) = default;
    xor_shift_engine& operator=(xor_shift_engine const&) = default;

    explicit
    xor_shift_engine(result_type val = 1977u)
    {
        seed(val);
    }

    void
    seed(result_type seed);

    result_type
    operator()();

    static
    result_type constexpr
    min()
    {
        return std::numeric_limits<result_type>::min();
    }

    static
    result_type constexpr
    max()
    {
        return std::numeric_limits<result_type>::max();
    }

private:
    result_type s_[2];

    static
    result_type
    murmurhash3(result_type x);
};

inline
void
xor_shift_engine::seed(result_type seed)
{
    if(seed == 0)
        throw std::domain_error("invalid seed");
    s_[0] = murmurhash3(seed);
    s_[1] = murmurhash3(s_[0]);
}

inline
auto
xor_shift_engine::operator()() ->
    result_type
{
    result_type s1 = s_[0];
    result_type const s0 = s_[1];
    s_[0] = s0;
    s1 ^= s1<< 23;
    return(s_[1] =(s1 ^ s0 ^(s1 >> 17) ^(s0 >> 26))) + s0;
}

inline
auto
xor_shift_engine::murmurhash3(result_type x)
    -> result_type
{
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ULL;
    return x ^= x >> 33;
}

} // test
} // nudb

#endif
