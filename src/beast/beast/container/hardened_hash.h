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

#include "../utility/is_call_possible.h"

#include "../utility/noexcept.h"
#include <functional>
#include <mutex>
#include <random>
#include "../cxx14/utility.h" // <utility>

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

}

/** A std compatible hash adapter that resists adversarial inputs.
    For this to work, one of the following must exist:
    
    * A member function of `T` called `hash_combine` with
      this signature:

        @code
        
        void hash_combine (std::size_t&) const noexcept;

        @endcode

    * A free function called `hash_combine`, found via argument
      dependent lookup, callable with this signature:

        @code

        void hash_combine (std::size_t, T const& t) noexcept;

        @endcode
*/
template <class T>
class hardened_hash
    : public detail::hardened_hash_base <std::size_t>
{
public:
    typedef T argument_type;
    using detail::hardened_hash_base <std::size_t>::result_type;

private:
    BEAST_DEFINE_IS_CALL_POSSIBLE(has_hash_combine,hash_combine);

    typedef detail::hardened_hash_base <std::size_t> base;

    // Called when hash_combine is a member function
    result_type
    operator() (argument_type const& key, std::true_type) const noexcept
    {
        result_type result (base::seed());
        key.hash_combine (result);
        return result;
    }

    result_type
    operator() (argument_type const& key, std::false_type) const noexcept
    {
        result_type result (base::seed());
        hash_combine (result, key);
        return result;
    }

public:
    hardened_hash() = default;

    result_type
    operator() (argument_type const& key) const noexcept
    {
        return operator() (key, std::integral_constant <bool,
            has_hash_combine <T,void(result_type&)>::value>());
    }
};

}

#endif
