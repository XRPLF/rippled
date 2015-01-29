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

#ifndef RIPPLE_BASICS_TOSTRING_H_INCLUDED
#define RIPPLE_BASICS_TOSTRING_H_INCLUDED

#include <string>
#include <type_traits>

namespace ripple {

/** to_string() generalizes std::to_string to handle bools, chars, and strings.

    It's also possible to provide implementation of to_string for a class
    which needs a string implementation.
 */

template <class T>
typename std::enable_if<std::is_arithmetic<T>::value,
                        std::string>::type
to_string(T t)
{
    return std::to_string(t);
}

inline std::string to_string(bool b)
{
    return b ? "true" : "false";
}

inline std::string to_string(char c)
{
    return std::string(1, c);
}

inline std::string to_string(std::string s)
{
    return s;
}

inline std::string to_string(char const* s)
{
    return s;
}

} // ripple

#endif
