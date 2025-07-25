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

#ifndef BEAST_CONTAINER_AGED_CONTAINER_UTILITY_H_INCLUDED
#define BEAST_CONTAINER_AGED_CONTAINER_UTILITY_H_INCLUDED

#include <xrpl/beast/container/aged_container.h>

#include <chrono>
#include <type_traits>

namespace beast {

/** Expire aged container items past the specified age. */
template <class AgedContainer, class Rep, class Period>
typename std::enable_if<is_aged_container<AgedContainer>::value, std::size_t>::
    type
    expire(AgedContainer& c, std::chrono::duration<Rep, Period> const& age)
{
    std::size_t n(0);
    auto const expired(c.clock().now() - age);
    for (auto iter(c.chronological.cbegin());
         iter != c.chronological.cend() && iter.when() <= expired;)
    {
        iter = c.erase(iter);
        ++n;
    }
    return n;
}

}  // namespace beast

#endif
