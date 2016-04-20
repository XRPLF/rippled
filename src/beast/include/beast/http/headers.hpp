//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_HTTP_HEADERS_HPP
#define BEAST_HTTP_HEADERS_HPP

#include <beast/http/basic_headers.hpp>
#include <memory>

namespace beast {
namespace http {

template<class Allocator>
using headers = basic_headers<Allocator>;

using http_headers =
    basic_headers<std::allocator<char>>;

} // http
} // beast

#endif
