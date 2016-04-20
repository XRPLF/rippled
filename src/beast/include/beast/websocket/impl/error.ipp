//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_WEBSOCKET_IMPL_ERROR_IPP_HPP
#define BEAST_WEBSOCKET_IMPL_ERROR_IPP_HPP

#include <beast/websocket/detail/error.hpp>

namespace beast {
namespace websocket {

inline
error_code
make_error_code(error e)
{
    return error_code(
        static_cast<int>(e), detail::get_error_category());
}

} // websocket
} // beast

#endif
