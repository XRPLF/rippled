//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_HTTP_TYPE_CHECK_HPP
#define BEAST_HTTP_TYPE_CHECK_HPP

#include <type_traits>

namespace beast {
namespace http {

/// Evaluates to std::true_type if `T` models Body
template<class T>
struct is_Body : std::true_type
{
};

/// Evalulates to std::true_type if Body has a reader
template<class T>
struct is_ReadableBody : std::true_type
{
};

/// Evalulates to std::true_type if Body has a writer
template<class T>
struct is_WritableBody : std::true_type
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

} // http
} // beast

#endif
