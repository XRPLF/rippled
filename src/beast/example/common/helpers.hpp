//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_EXAMPLE_COMMON_HELPERS_HPP
#define BEAST_EXAMPLE_COMMON_HELPERS_HPP

#include <boost/asio/io_service.hpp>
#include <boost/asio/signal_set.hpp>
#include <ostream>
#include <sstream>

/// Block until SIGINT or SIGTERM is received.
inline
void
sig_wait()
{
    boost::asio::io_service ios{1};
    boost::asio::signal_set signals(ios, SIGINT, SIGTERM);
    signals.async_wait([&](boost::system::error_code const&, int){});
    ios.run();
}

namespace detail {

inline
void
print_1(std::ostream&)
{
}

template<class T1, class... TN>
void
print_1(std::ostream& os, T1 const& t1, TN const&... tn)
{
    os << t1;
    print_1(os, tn...);
}

} // detail

// compose a string to std::cout or std::cerr atomically
//
template<class...Args>
void
print(std::ostream& os, Args const&... args)
{
    std::stringstream ss;
    detail::print_1(ss, args...);
    os << ss.str() << std::endl;
}

#endif
