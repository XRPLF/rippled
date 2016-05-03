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

#ifndef BEAST_WEBSOCKETDETAIL_MASKGEN_HPP
#define BEAST_WEBSOCKETDETAIL_MASKGEN_HPP

#include <boost/asio/buffer.hpp>
#include <array>
#include <climits>
#include <cstdint>
#include <random>
#include <type_traits>

namespace beast {
namespace websocket {
namespace detail {

// Pseudo-random source of mask keys
//
template<class = void>
class maskgen_t
{
    std::mt19937 g_;

public:
    using result_type = typename std::mt19937::result_type;

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
    for(;;)
        if(auto key = g_())
            return key;
}

template<class _>
void
maskgen_t<_>::rekey()
{
    std::random_device rng;
    std::array<std::uint32_t, 32> e;
    for(auto& i : e)
        i = rng();
    std::seed_seq ss(e.begin(), e.end());
    g_.seed(ss);
}

using maskgen = maskgen_t<>;

//------------------------------------------------------------------------------

//using prepared_key_type = std::size_t;
using prepared_key_type = std::uint32_t;
//using prepared_key_type = std::uint64_t;

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

template<class T>
inline
typename std::enable_if<std::is_integral<T>::value, T>::type
rol(T t, unsigned n = 1)
{
    auto constexpr bits =
        static_cast<unsigned>(
            sizeof(T) * CHAR_BIT);
    n &= bits-1;
    return static_cast<T>((t << n) | (
        static_cast<typename std::make_unsigned<T>::type>(t) >> (bits - n)));
}

template <class T>
inline
typename std::enable_if<std::is_integral<T>::value, T>::type
ror(T t, unsigned n = 1)
{
    auto constexpr bits =
        static_cast<unsigned>(
            sizeof(T) * CHAR_BIT);
    n &= bits-1;
    return static_cast<T>((t << (bits - n)) | (
        static_cast<typename std::make_unsigned<T>::type>(t) >> n));
}

// 32-bit Uuoptimized
//
template<class = void>
void
mask_inplace_safe(
    boost::asio::mutable_buffer const& b,
        std::uint32_t& key)
{
    using boost::asio::buffer_cast;
    using boost::asio::buffer_size;
    auto n = buffer_size(b);
    auto p = buffer_cast<std::uint8_t*>(b);
    for(auto i = n / sizeof(key); i; --i)
    {
        *p ^=  key      ; ++p;
        *p ^= (key >> 8); ++p;
        *p ^= (key >>16); ++p;
        *p ^= (key >>24); ++p;
    }
    n %= sizeof(key);
    switch(n)
    {
    case 3: p[2] ^= (key >>16);
    case 2: p[1] ^= (key >> 8);
    case 1: p[0] ^=  key;
        key = ror(key, n*8);
    default:
        break;
    }
}

// 64-bit unoptimized
//
template<class = void>
void
mask_inplace_safe(
    boost::asio::mutable_buffer const& b,
        std::uint64_t& key)
{
    using boost::asio::buffer_cast;
    using boost::asio::buffer_size;
    auto n = buffer_size(b);
    auto p = buffer_cast<std::uint8_t*>(b);
    for(auto i = n / sizeof(key); i; --i)
    {
        *p ^=  key      ; ++p;
        *p ^= (key >> 8); ++p;
        *p ^= (key >>16); ++p;
        *p ^= (key >>24); ++p;
        *p ^= (key >>32); ++p;
        *p ^= (key >>40); ++p;
        *p ^= (key >>48); ++p;
        *p ^= (key >>56); ++p;
    }
    n %= sizeof(key);
    switch(n)
    {
    case 7: p[6] ^= (key >>16);
    case 6: p[5] ^= (key >> 8);
    case 5: p[4] ^=  key;
    case 4: p[3] ^= (key >>24);
    case 3: p[2] ^= (key >>16);
    case 2: p[1] ^= (key >> 8);
    case 1: p[0] ^=  key;
        key = ror(key, n*8);
    default:
        break;
    }
}

// 32-bit optimized
template<class = void>
void
mask_inplace_32(
    boost::asio::mutable_buffer const& b,
        std::uint32_t& key)
{
    using boost::asio::buffer_cast;
    using boost::asio::buffer_size;
    auto n = buffer_size(b);
    auto p = buffer_cast<std::uint8_t*>(b);
    auto m = reinterpret_cast<
        uintptr_t>(p) % sizeof(key);
    switch(m)
    {
    case 1: *p ^=  key      ; ++p; --n;
    case 2: *p ^= (key >> 8); ++p; --n;
    case 3: *p ^= (key >>16); ++p; --n;
        key = ror(key, m * 8);
    case 0:
        break;
    }
    for(auto i = n / sizeof(key); i; --i)
    {
        *reinterpret_cast<
            std::uint32_t*>(p) ^= key;
        p += sizeof(key);
    }
    n %= sizeof(key);
    switch(n)
    {
    case 3: p[2] ^= (key >>16);
    case 2: p[1] ^= (key >> 8);
    case 1: p[0] ^=  key;
        key = ror(key, n*8);
    default:
        break;
    }
}

// 64-bit optimized
//
template<class = void>
void
mask_inplace_64(
    boost::asio::mutable_buffer const& b,
        std::uint64_t& key)
{
    using boost::asio::buffer_cast;
    using boost::asio::buffer_size;
    auto n = buffer_size(b);
    auto p = buffer_cast<std::uint8_t*>(b);
    auto m = reinterpret_cast<
        uintptr_t>(p) % sizeof(key);
    switch(m)
    {
    case 1: *p ^=  key      ; ++p; --n;
    case 2: *p ^= (key >> 8); ++p; --n;
    case 3: *p ^= (key >>16); ++p; --n;
    case 4: *p ^= (key >>24); ++p; --n;
    case 5: *p ^= (key >>32); ++p; --n;
    case 6: *p ^= (key >>40); ++p; --n;
    case 7: *p ^= (key >>48); ++p; --n;
        key = ror(key, m * 8);
    case 0:
        break;
    }
    for(auto i = n / sizeof(key); i; --i)
    {
        *reinterpret_cast<
            std::uint64_t*>(p) ^= key;
        p += sizeof(key);
    }
    n %= sizeof(key);
    switch(n)
    {
    case 3: p[2] ^= (key >>16);
    case 2: p[1] ^= (key >> 8);
    case 1: p[0] ^=  key;
        key = ror(key, n*8);
    default:
        break;
    }
}

// 32-bit x86 optimized
//
template<class = void>
void
mask_inplace_x86(
    boost::asio::mutable_buffer const& b,
        std::uint32_t& key)
{
    using boost::asio::buffer_cast;
    using boost::asio::buffer_size;
    auto n = buffer_size(b);
    auto p = buffer_cast<std::uint8_t*>(b);
    for(auto i = n / sizeof(key); i; --i)
    {
        *reinterpret_cast<
            std::uint32_t*>(p) ^= key;
        p += sizeof(key);
    }
    n %= sizeof(key);
    switch(n)
    {
    case 3: p[2] ^= (key >>16);
    case 2: p[1] ^= (key >> 8);
    case 1: p[0] ^=  key;
        key = ror(key, n*8);
    default:
        break;
    }
}

// 64-bit amd64 optimized
//
template<class = void>
void
mask_inplace_amd(
    boost::asio::mutable_buffer const& b,
        std::uint64_t& key)
{
    using boost::asio::buffer_cast;
    using boost::asio::buffer_size;
    auto n = buffer_size(b);
    auto p = buffer_cast<std::uint8_t*>(b);
    for(auto i = n / sizeof(key); i; --i)
    {
        *reinterpret_cast<
            std::uint64_t*>(p) ^= key;
        p += sizeof(key);
    }
    n %= sizeof(key);
    switch(n)
    {
    case 7: p[6] ^= (key >>16);
    case 6: p[5] ^= (key >> 8);
    case 5: p[4] ^=  key;
    case 4: p[3] ^= (key >>24);
    case 3: p[2] ^= (key >>16);
    case 2: p[1] ^= (key >> 8);
    case 1: p[0] ^=  key;
        key = ror(key, n*8);
    default:
        break;
    }
}

inline
void
mask_inplace(
    boost::asio::mutable_buffer const& b,
        std::uint32_t& key)
{
    mask_inplace_safe(b, key);
    //mask_inplace_32(b, key);
    //mask_inplace_x86(b, key);
}

inline
void
mask_inplace(
    boost::asio::mutable_buffer const& b,
        std::uint64_t& key)
{
    mask_inplace_safe(b, key);
    //mask_inplace_64(b, key);
    //mask_inplace_amd(b, key);
}

// Apply mask in place
//
template<class MutableBuffers, class KeyType>
void
mask_inplace(
    MutableBuffers const& bs, KeyType& key)
{
    for(auto const& b : bs)
        mask_inplace(b, key);
}

} // detail
} // websocket
} // beast

#endif
