//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

// Test that header file is self-contained.
#include <beast/http/vector_body.hpp>

namespace beast {
namespace http {

BOOST_STATIC_ASSERT(is_body<vector_body<char>>::value);
BOOST_STATIC_ASSERT(is_body_reader<vector_body<char>>::value);
BOOST_STATIC_ASSERT(is_body_writer<vector_body<char>>::value);

} // http
} // beast
