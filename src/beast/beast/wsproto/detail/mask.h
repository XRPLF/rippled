//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

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

#ifndef BEAST_WSPROTO_DETAIL_MASKGEN_H_INCLUDED
#define BEAST_WSPROTO_DETAIL_MASKGEN_H_INCLUDED

#include <boost/endian/conversion.hpp>
#include <array>
#include <cstdint>
#include <random>
#include <type_traits>

namespace beast {
namespace wsproto {
namespace detail {

// Pseudo-random source of mask keys
//
template<class = void>
class maskgen_t
{
private:
    std::size_t n_;
    std::mt19937 g_;

public:
    using result_type = typename std::mt19937::result_type;

    maskgen_t(maskgen_t const&) = delete;
    maskgen_t& operator=(maskgen_t const&) = delete;

    maskgen_t();

    result_type
    operator()() noexcept;

    void
    rekey();
};

template<class _>
maskgen_t<_>::maskgen_t()
{
    rekey();
}

template<class _>
auto
maskgen_t<_>::operator()() noexcept ->
    result_type
{
    if(++n_ == 10000)
        rekey();
    for(;;)
    {
        if(auto key = g_())
            return key;
    }
}

template<class _>
void
maskgen_t<_>::rekey()
{
    n_ = 0;
    std::random_device rng;
    std::array<std::uint32_t, 32> e;
    for(auto& i : e)
        i = rng();
    std::seed_seq ss(e.begin(), e.end());
    g_.seed(ss);
}

using maskgen = maskgen_t<>;

//------------------------------------------------------------------------------

// VFALCO TODO Support wider and optimized prepared keys
//
//using prepared_key_type = std::size_t;
using prepared_key_type = std::uint32_t;

inline
void
prepare_key(std::uint32_t& prepared, std::uint32_t key)
{
    prepared = key;
}

inline
void
prepare_key(std::uint64_t& prepared, std::uint32_t key)
{
    prepared =
        (static_cast<std::uint64_t>(key) << 32) | key;
}

// Apply mask in place to a buffer
//  generic unoptimized 32-bit version
// TODO: Intel optimized 32/64,
//       generic optimized 32/64
template<class = void>
void
mask_inplace(
    boost::asio::mutable_buffer const& b,
        std::uint32_t& key)
{
    using namespace boost::asio;
    auto n = buffer_size(b);
    auto p = buffer_cast<std::uint8_t*>(b);
    for(auto i = n / 4; i; --i)
    {
        *p = *p ^  key      ; ++p;
        *p = *p ^ (key >> 8); ++p;
        *p = *p ^ (key >>16); ++p;
        *p = *p ^ (key >>24); ++p;
    }
    n %= 4;
    switch(n)
    {
    case 3: p[2] = p[2] ^ (key >>16);
    case 2: p[1] = p[1] ^ (key >> 8);
    case 1: p[0] = p[0] ^  key;
        n *= 8;
        key = (key << n) | (key >> (32 - n));
    default:
        break;
    }
}

// Apply mask in place
//
template<class MutableBuffers>
void
mask_inplace(
    MutableBuffers const& bs, std::uint32_t& key)
{
    for(auto const& b : bs)
        mask_inplace(b, key);
}

} // detail
} // wsproto
} // beast

#endif
