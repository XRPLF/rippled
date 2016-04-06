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

#ifndef BEAST_HTTP_TYPE_CHECK_H_INCLUDED
#define BEAST_HTTP_TYPE_CHECK_H_INCLUDED

#include <boost/asio/buffer.hpp>
#include <boost/system/error_code.hpp>
#include <functional>
#include <beast/cxx17/type_traits.h> // <type_traits>

namespace beast {
namespace http {

/// Evaluates to std::true_type if `T` models Body
template<class T>
struct is_Body : std::true_type
{
};

/// Evaluates to std::true_type if `T` models HTTPMessage
template<class T>
struct is_HTTPMessage : std::false_type
{
};

/// Evaluates to std::true_type if `HTTPMessage` is a request
template<class HTTPMessage>
struct is_HTTPRequest : std::true_type
{
};

/** Trait: `C` models HTTPParser
*/
template<class C, class = void>
struct is_HTTPParser : std::false_type {};
template <class C>
struct is_HTTPParser<C, std::void_t<
    decltype(std::declval<C>().write(std::declval<boost::asio::const_buffer>(),
        std::declval<boost::system::error_code&>()), std::true_type{}),
    std::is_same<decltype(
        std::declval<C>().complete()), bool>
>>:std::true_type{};

} // http
} // beast

#endif
