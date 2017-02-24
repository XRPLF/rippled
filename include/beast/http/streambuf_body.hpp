//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_HTTP_STREAMBUF_BODY_HPP
#define BEAST_HTTP_STREAMBUF_BODY_HPP

#include <beast/http/basic_dynabuf_body.hpp>
#include <beast/core/streambuf.hpp>

namespace beast {
namespace http {

/** A message body represented by a @ref streambuf

    Meets the requirements of @b `Body`.
*/
using streambuf_body = basic_dynabuf_body<streambuf>;

} // http
} // beast

#endif
