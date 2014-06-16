//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#ifndef RIPPLE_SECONDS_CLOCK_H_INCLUDED
#define RIPPLE_SECONDS_CLOCK_H_INCLUDED

#include <beast/chrono/abstract_clock.h>
#include <beast/chrono/basic_seconds_clock.h>

#include <chrono>

namespace ripple {

using days = std::chrono::duration
    <int, std::ratio_multiply<std::chrono::hours::period, std::ratio<24>>>;

using weeks = std::chrono::duration
    <int, std::ratio_multiply<days::period, std::ratio<7>>>;

/** Returns an abstract_clock optimized for counting seconds. */
inline beast::abstract_clock <std::chrono::seconds>& get_seconds_clock ()
{
    typedef beast::basic_seconds_clock <std::chrono::steady_clock> clock_type;
    return beast::get_abstract_clock <clock_type, std::chrono::seconds> ();
}

}

#endif
