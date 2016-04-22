//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_HTTP_DETAIL_WRITES_HPP
#define BEAST_HTTP_DETAIL_WRITES_HPP

#include <beast/type_check.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/utility/string_ref.hpp>
#include <string>
#include <type_traits>
#include <utility>

namespace beast {
namespace http {
namespace detail {

template<class Streambuf, class T,
    class = typename std::enable_if<
        is_Streambuf<Streambuf>::value>::type>
void
write(Streambuf& streambuf, T&& t)
{
    using boost::asio::buffer;
    using boost::asio::buffer_copy;
    auto const& s =
        boost::lexical_cast<std::string>(
            std::forward<T>(t));
    streambuf.commit(buffer_copy(
        streambuf.prepare(s.size()), buffer(s)));
}

template<class Streambuf, std::size_t N,
    class = typename std::enable_if< (N>0) &&
        is_Streambuf<Streambuf>::value>::type>
void
write(Streambuf& streambuf, char const(&s)[N])
{
    using boost::asio::buffer;
    using boost::asio::buffer_copy;
    streambuf.commit(buffer_copy(
        streambuf.prepare(N), buffer(s, N-1)));
}

template<class Streambuf,
    class = typename std::enable_if<
        is_Streambuf<Streambuf>::value>::type>
void
write(Streambuf& streambuf, boost::string_ref const& s)
{
    using boost::asio::buffer;
    using boost::asio::buffer_copy;
    streambuf.commit(buffer_copy(
        streambuf.prepare(s.size()),
            buffer(s.data(), s.size())));
}

} // detail
} // http
} // beast

#endif
