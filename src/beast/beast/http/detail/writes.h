//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef BEAST_HTTP_WRITES_H_INCLUDED
#define BEAST_HTTP_WRITES_H_INCLUDED

#include <beast/asio/type_check.h>
#include <boost/lexical_cast.hpp>
#include <boost/utility/string_ref.hpp>
#include <string>
#include <type_traits>
#include <utility>

namespace beast {
namespace http {
namespace detail {

template<class Streambuf, class T,
    class = std::enable_if_t<is_Streambuf<Streambuf>::value>>
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
    class = std::enable_if_t<is_Streambuf<Streambuf>::value>>
void
write(Streambuf& streambuf, char const(&s)[N])
{
    using boost::asio::buffer;
    using boost::asio::buffer_copy;
    streambuf.commit(buffer_copy(
        streambuf.prepare(N), buffer(s, N)));
}

template<class Streambuf,
    class = std::enable_if_t<is_Streambuf<Streambuf>::value>>
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
