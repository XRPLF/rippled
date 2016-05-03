//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_HTTP_ERROR_HPP
#define BEAST_HTTP_ERROR_HPP

#include <boost/system/error_code.hpp>
#include <boost/system/system_error.hpp>

namespace beast {
namespace http {

using error_code = boost::system::error_code;

} // http
} // beast

#endif
