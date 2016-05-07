//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_WEBSOCKET_DETAIL_MASK_HPP
#define BEAST_WEBSOCKET_DETAIL_MASK_HPP

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
template<class Generator>
class maskgen_t
{
    Generator g_;

public:
    using result_type =
        typename Generator::result_type;

    maskgen_t();

    result_type
    operator()() noexcept;

    void
    rekey();
};

template<class Generator>
maskgen_t<Generator>::maskgen_t()
{
    rekey();
}

template<class Generator>
auto
maskgen_t<Generator>::operator()() noexcept ->
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

using maskgen = maskgen_t<std::mt19937>;

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

// 32-bit Unoptimized
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
