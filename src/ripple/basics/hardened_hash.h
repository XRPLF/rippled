//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2014 Ripple Labs Inc.

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

#ifndef RIPPLE_BASICS_HARDENED_HASH_H_INCLUDED
#define RIPPLE_BASICS_HARDENED_HASH_H_INCLUDED

#include <ripple/beast/hash/hash_append.h>
#include <ripple/beast/hash/xxhasher.h>

#include <cstdint>
#include <functional>
#include <mutex>
#include <utility>
#include <random>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>

namespace ripple {

namespace detail {

using seed_pair = std::pair<std::uint64_t, std::uint64_t>;

template <bool = true>
seed_pair
make_seed_pair() noexcept
{
    struct state_t
    {
        std::mutex mutex;
        std::random_device rng;
        std::mt19937_64 gen;
        std::uniform_int_distribution <std::uint64_t> dist;

        state_t() : gen(rng()) {}
        // state_t(state_t const&) = delete;
        // state_t& operator=(state_t const&) = delete;
    };
    static state_t state;
    std::lock_guard lock(state.mutex);
    return {state.dist(state.gen), state.dist(state.gen)};
}

}

template <class HashAlgorithm, bool ProcessSeeded>
class basic_hardened_hash;

/**
 * Seed functor once per process
*/
template <class HashAlgorithm>
class basic_hardened_hash <HashAlgorithm, true>
{
private:
    static
    detail::seed_pair const&
    init_seed_pair()
    {
        static detail::seed_pair const p = detail::make_seed_pair<>();
        return p;
    }

public:
    explicit basic_hardened_hash() = default;

    using result_type = typename HashAlgorithm::result_type;

    template <class T>
    result_type
    operator()(T const& t) const noexcept
    {
        auto const [seed0, seed1] = init_seed_pair();
        HashAlgorithm h(seed0, seed1);
        hash_append(h, t);
        return static_cast<result_type>(h);
    }
};

/**
 * Seed functor once per construction
*/
template <class HashAlgorithm>
class basic_hardened_hash<HashAlgorithm, false>
{
private:
    detail::seed_pair m_seeds;

public:
    using result_type = typename HashAlgorithm::result_type;

    basic_hardened_hash()
        : m_seeds (detail::make_seed_pair<>())
    {}

    template <class T>
    result_type
    operator()(T const& t) const noexcept
    {
        HashAlgorithm h(m_seeds.first, m_seeds.second);
        hash_append(h, t);
        return static_cast<result_type>(h);
    }
};

//------------------------------------------------------------------------------

/** A std compatible hash adapter that resists adversarial inputs.
    For this to work, T must implement in its own namespace:

    @code

    template <class Hasher>
    void
    hash_append (Hasher& h, T const& t) noexcept
    {
        // hash_append each base and member that should
        //  participate in forming the hash
        using beast::hash_append;
        hash_append (h, static_cast<T::base1 const&>(t));
        hash_append (h, static_cast<T::base2 const&>(t));
        // ...
        hash_append (h, t.member1);
        hash_append (h, t.member2);
        // ...
    }

    @endcode

    Do not use any version of Murmur or CityHash for the Hasher
    template parameter (the hashing algorithm).  For details
    see https://131002.net/siphash/#at
*/
template <class HashAlgorithm = beast::xxhasher>
    using hardened_hash = basic_hardened_hash<HashAlgorithm, false>;

} // ripple

#endif
