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

#ifndef BEAST_UTILITY_HASH_PAIR_H_INCLUDED
#define BEAST_UTILITY_HASH_PAIR_H_INCLUDED

#include <functional>
#include <utility>

#include <boost/functional/hash.hpp>
#include <boost/utility/base_from_member.hpp>

namespace std {

/** Specialization of std::hash for any std::pair type. */
template <class First, class Second>
struct hash <std::pair <First, Second>>
    : private boost::base_from_member <std::hash <First>, 0>
    , private boost::base_from_member <std::hash <Second>, 1>
{
private:
    using first_hash = boost::base_from_member <std::hash <First>, 0>;
    using second_hash = boost::base_from_member <std::hash <Second>, 1>;

public:
    hash ()
    {
    }

    hash (std::hash <First> const& first_hash_,
          std::hash <Second> const& second_hash_)
          : first_hash (first_hash_)
          , second_hash (second_hash_)
    {
    }

    std::size_t operator() (std::pair <First, Second> const& value)
    {
        std::size_t result (first_hash::member (value.first));
        boost::hash_combine (result, second_hash::member (value.second));
        return result;
    }

    std::size_t operator() (std::pair <First, Second> const& value) const
    {
        std::size_t result (first_hash::member (value.first));
        boost::hash_combine (result, second_hash::member (value.second));
        return result;
    }
};

}

#endif
