//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_HTTP_DETAIL_WRITE_PREPARATION_HPP
#define BEAST_HTTP_DETAIL_WRITE_PREPARATION_HPP

#include <beast/http/error.hpp>
#include <beast/streambuf.hpp>
#include <beast/write_streambuf.hpp>

namespace beast {
namespace http {
namespace detail {

template<class T>
class has_content_length_value
{
    template<class U, class R = typename std::is_convertible<
        decltype(std::declval<U>().content_length()),
            std::size_t>>
    static R check(int);
    template <class>
    static std::false_type check(...);
    using type = decltype(check<T>(0));
public:
    // `true` if `T` meets the requirements.
    static bool const value = type::value;
};

// Determines if the writer can provide the content length
template<class T>
using has_content_length =
    std::integral_constant<bool,
        has_content_length_value<T>::value>;

template<bool isRequest, class Body, class Headers>
struct write_preparation
{
    using headers_type =
        basic_headers<std::allocator<char>>;

    message<isRequest, Body, Headers> const& msg;
    typename Body::writer w;
    streambuf sb;
    bool chunked;
    bool close;

    explicit
    write_preparation(
            message<isRequest, Body, Headers> const& msg_)
        : msg(msg_)
        , w(msg)
    {
    }

    void
    init(error_code& ec)
    {
        w.init(ec);
        if(ec)
            return;
        // VFALCO TODO This implementation requires making a
        //             copy of the headers, we can do better.
        // VFALCO Should we be using handler_alloc?
        headers_type h(msg.headers.begin(), msg.headers.end());
        set_content_length(h, has_content_length<
            typename Body::writer>{});

        // VFALCO TODO Keep-Alive

        if(close)
        {
            if(msg.version >= 11)
                h.insert("Connection", "close");
        }
        else
        {
            if(msg.version < 11)
                h.insert("Connection", "keep-alive");
        }

        msg.write_firstline(sb);
        write_fields(sb, h);
        beast::write(sb, "\r\n");
    }

private:
    void
    set_content_length(headers_type& h,
        std::true_type)
    {
        close = false;
        chunked = false;
        h.insert("Content-Length", w.content_length());
    }

    void
    set_content_length(headers_type& h,
        std::false_type)
    {
        if(msg.version >= 11)
        {
            close = false;
            chunked = true;
            h.insert("Transfer-Encoding", "chunked");
        }
        else
        {
            close = true;
            chunked = false;
        }
    }
};

} // detail
} // http
} // beast

#endif
