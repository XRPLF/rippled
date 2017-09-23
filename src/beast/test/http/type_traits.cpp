//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

// Test that header file is self-contained.
#include <beast/http/type_traits.hpp>

#include <beast/http/empty_body.hpp>
#include <string>

namespace beast {
namespace http {

BOOST_STATIC_ASSERT(! is_body_reader<int>::value);

BOOST_STATIC_ASSERT(is_body_reader<empty_body>::value);

BOOST_STATIC_ASSERT(! is_body_writer<std::string>::value);

namespace {

struct not_fields {};

} // (anonymous)

BOOST_STATIC_ASSERT(! is_fields<not_fields>::value);

} // http
} // beast
