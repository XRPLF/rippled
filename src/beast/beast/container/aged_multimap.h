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

#ifndef BEAST_CONTAINER_AGED_MULTIMAP_H_INCLUDED
#define BEAST_CONTAINER_AGED_MULTIMAP_H_INCLUDED

#include <beast/container/detail/aged_ordered_container.h>

#include <chrono>
#include <functional>
#include <memory>

namespace beast {

template <
    class Key,
    class T,
    class Clock = std::chrono::steady_clock,
    class Compare = std::less <Key>,
    class Allocator = std::allocator <std::pair <Key const, T>>
>
using aged_multimap = detail::aged_ordered_container <
    true, true, Key, T, Clock, Compare, Allocator>;

}

#endif
