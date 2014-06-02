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

#ifndef BEAST_CONTAINER_HARDENED_HASH_H_INCLUDED
#define BEAST_CONTAINER_HARDENED_HASH_H_INCLUDED

#include <beast/container/hash_append.h>

#include <beast/utility/noexcept.h>
#include <cstdint>
#include <functional>
#include <mutex>
#include <random>
#include <beast/cxx14/type_traits.h> // <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <beast/cxx14/utility.h> // <utility>

// When set to 1, makes the seed per-process instead
// of per default-constructed instance of hardened_hash
//
#ifndef BEAST_NO_HARDENED_HASH_INSTANCE_SEED
# ifdef __GLIBCXX__
#  define BEAST_NO_HARDENED_HASH_INSTANCE_SEED 1
# else
#  define BEAST_NO_HARDENED_HASH_INSTANCE_SEED 0
# endif
#endif

namespace beast {
namespace detail {

template <class Result>
class hardened_hash_base
{
public:
    typedef Result result_type;

private:
    static
    result_type
    next_seed() noexcept
    {
        static std::mutex mutex;
        static std::random_device rng;
        static std::mt19937_64 gen (rng());
        std::lock_guard <std::mutex> lock (mutex);
        std::uniform_int_distribution <result_type> dist;
        result_type value;
        for(;;)
        {
            value = dist (gen);
            // VFALCO Do we care if 0 is picked?
            if (value != 0)
                break;
        }
        return value;
    }

#if BEAST_NO_HARDENED_HASH_INSTANCE_SEED
protected:
    hardened_hash_base() noexcept = default;

    hardened_hash_base(result_type) noexcept
    {
    }

    result_type
    seed() const noexcept
    {
        static result_type const value (next_seed());
        return value;
    }

#else
protected:
    hardened_hash_base() noexcept
        : m_seed (next_seed())
    {
    }

    hardened_hash_base(result_type seed) noexcept
        : m_seed (seed)
    {
    }

    result_type
    seed() const noexcept
    {
        return m_seed;
    }

private:
    // VFALCO Should seed be per process or per hash function?
    result_type m_seed;

#endif
};

//------------------------------------------------------------------------------

} // detail

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
*/
template <class T, class Hasher = detail::spooky_wrapper>
class hardened_hash
    : public detail::hardened_hash_base <std::size_t>
{
    typedef detail::hardened_hash_base <std::size_t> base;
public:
    typedef T argument_type;
    using detail::hardened_hash_base <std::size_t>::result_type;

public:
    hardened_hash() = default;
    explicit hardened_hash(result_type seed)
        : base (seed)
    {
    }

    result_type
    operator() (argument_type const& key) const noexcept
    {
        Hasher h {base::seed()};
        hash_append (h, key);
        return static_cast<result_type> (h);
    }
};

} // beast

#endif
