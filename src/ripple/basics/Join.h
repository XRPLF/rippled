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

#ifndef RIPPLED_RIPPLE_BASICS_JOIN_H
#define RIPPLED_RIPPLE_BASICS_JOIN_H

namespace ripple {

namespace detail {

inline size_t strSize (char const* s)
{
    return strlen (s);
}

inline size_t strSize (std::string const& s)
{
    return s.size();
}

} // detail

/** A string join in O(n) time, where n is the total number of characters
    joined. */
template <typename Iterator>
std::string join (Iterator first, Iterator last)
{
    std::string result;
    if (first != last)
    {
        size_t size = 0;
        for (auto i = first; i != last; ++i)
            size += detail::strSize (*i);

        result.reserve (size);
        for (auto i = first; i != last; ++i)
            result += *i;
    }
    return result;
}

/** A string join with a separator in O(n), where n is the total number of
 * characters joined. */
template <typename Iterator, typename Separator>
std::string join (Iterator first, Iterator last, Separator sep)
{
    std::string result;
    if (first != last)
    {
        auto i = first;
        auto sepSize = detail::strSize (sep);
        auto size = detail::strSize (*i++);

        while (i != last)
            size += (detail::strSize (*i++) + sepSize);

        result.reserve (size);
        i = first;
        result += (*i++);
        while (i != last)
        {
            result += sep;
            result += *i++;
        }
    }

    return result;
}

template <typename Container>
std::string join (Container const& c)
{
    using std::begin;
    using std::end;
    return join (begin (c), end (c));
}

template <typename Container, typename Separator>
std::string join (Container const& c, Separator sep)
{
    using std::begin;
    using std::end;
    return join (begin (c), end (c), sep);
}

} // ripple

#endif
