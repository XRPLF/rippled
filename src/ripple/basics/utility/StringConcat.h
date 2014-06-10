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

#ifndef RIPPLE_STRINGCONCAT_H
#define RIPPLE_STRINGCONCAT_H

#include <ripple/basics/utility/ToString.h>

namespace ripple {

namespace detail {

// ConcatArg is used to represent arguments to stringConcat.

struct ConcatArg {
    ConcatArg(std::string const& s) : data_(s.data()), size_(s.size())
    {
    }

    ConcatArg(char const* s) : data_(s), size_(strlen(s))
    {
    }

    template <typename T>
    ConcatArg(T t) : string_(to_string(t)),
                     data_(string_.data()),
                     size_(string_.size())
    {
    }

    std::string string_;
    char const* data_;
    std::size_t size_;
};

} // namespace detail

/** Concatenate strings, numbers, bools and chars into one string in O(n) time.

    Usage:
      stringConcat({"hello ", 23, 'x', true});

    Returns:
      "hello 23xtrue"
 */
inline std::string stringConcat(std::vector<detail::ConcatArg> args)
{
    int capacity = 0;
    for (auto const& a: args)
        capacity += a.size_;

    std::string result;
    result.reserve(capacity);
    for (auto const& a: args)
        result.append(a.data_, a.data_ + a.size_);
    return result;
}

} // ripple

#endif
