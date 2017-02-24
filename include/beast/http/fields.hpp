//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_HTTP_FIELDS_HPP
#define BEAST_HTTP_FIELDS_HPP

#include <beast/http/basic_fields.hpp>
#include <memory>

namespace beast {
namespace http {

/// A typical HTTP header fields container
using fields =
    basic_fields<std::allocator<char>>;

} // http
} // beast

#endif
