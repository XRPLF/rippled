//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_HTTP_TYPE_CHECK_HPP
#define BEAST_HTTP_TYPE_CHECK_HPP

#include <beast/http/error.hpp>
#include <beast/http/message.hpp>
#include <beast/http/resume_context.hpp>
#include <beast/type_check.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/logic/tribool.hpp>
#include <boost/system/error_code.hpp>
#include <functional>
#include <type_traits>

namespace beast {
namespace http {

#if GENERATING_DOCS
namespace detail {
#else
namespace concept {
#endif

struct Reader
{
    template<bool isRequest, class Body, class Headers>
    Reader(message<isRequest, Body, Headers>&) noexcept;
    void write(void const*, std::size_t, error_code&) noexcept;
};

} // concept

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
