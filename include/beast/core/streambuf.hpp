//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_STREAMBUF_HPP
#define BEAST_STREAMBUF_HPP

#include <beast/core/basic_streambuf.hpp>

namespace beast {

/** A @b `Streambuf` that uses multiple buffers internally.

    The implementation uses a sequence of one or more character arrays
    of varying sizes. Additional character array objects are appended to
    the sequence to accommodate changes in the size of the character
    sequence.

    @note Meets the requirements of @b `Streambuf`.
*/
using streambuf = basic_streambuf<std::allocator<char>>;

} // beast

#endif
