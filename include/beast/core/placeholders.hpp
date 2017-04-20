//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_PLACEHOLDERS_HPP
#define BEAST_PLACEHOLDERS_HPP

#include <beast/config.hpp>
#include <functional>

namespace beast {
namespace asio {

namespace placeholders {
// asio placeholders that work with std::bind
namespace {
static auto const error (std::placeholders::_1);
static auto const bytes_transferred (std::placeholders::_2);
static auto const iterator (std::placeholders::_2);
static auto const signal_number (std::placeholders::_2);
}
} // placeholders

} // asio
} // beast

#endif
