//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_STREAMBUF_HPP
#define BEAST_STREAMBUF_HPP

#include <beast/basic_streambuf.hpp>

namespace beast {

using streambuf = basic_streambuf<std::allocator<char>>;

} // beast

#endif
