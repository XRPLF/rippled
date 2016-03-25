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

#ifndef BEAST_WSPROTO_HTTP_H_INCLUDED
#define BEAST_WSPROTO_HTTP_H_INCLUDED

#include <beast/asio/streambuf.h>
#include <beast/http/headers.h>
#include <beast/http/method.h>
#include <boost/asio/buffer.hpp>
#include <array>
#include <cstdint>
#include <map>

namespace beast {
namespace wsproto {

namespace detail {

template <class Streambuf>
void
sb_write(Streambuf& sb, std::string const& s)
{
    using namespace boost::asio;
    sb.commit(buffer_copy(
        sb.prepare(s.size()), buffer(s)));
}

template <class Streambuf, std::size_t N>
void
sb_write(Streambuf& sb, char const(&s)[N])
{
    using namespace boost::asio;
    sb.commit(buffer_copy(
        sb.prepare(N), buffer(s)));
}

} // detail

template<class = void>
char const*
http_reason(int status)
{
    switch(status)
    {
    case 100: return "Continue";
    case 101: return "Switching Protocols";
    case 200: return "OK";
    case 201: return "Created";
    case 202: return "Accepted";
    case 203: return "Non-Authoritative Information";
    case 204: return "No Content";
    case 205: return "Reset Content";
    case 206: return "Partial Content";
    case 300: return "Multiple Choices";
    case 301: return "Moved Permanently";
    case 302: return "Found";
    case 303: return "See Other";
    case 304: return "Not Modified";
    case 305: return "Use Proxy";
    case 307: return "Temporary Redirect";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 402: return "Payment Required";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 406: return "Not Acceptable";
    case 407: return "Proxy Authentication Required";
    case 408: return "Request Time-out";
    case 409: return "Conflict";
    case 410: return "Gone";
    case 411: return "Length Required";
    case 412: return "Precondition Failed";
    case 413: return "Request Entity Too Large";
    case 414: return "Request-URI Too Large";
    case 415: return "Unsupported Media Type";
    case 416: return "Requested range not satisfiable";
    case 417: return "Expectation Failed";
    case 500: return "Internal Server Error";
    case 501: return "Not Implemented";
    case 502: return "Bad Gateway";
    case 503: return "Service Unavailable";
    case 504: return "Gateway Time-out";
    case 505: return "HTTP Version not supported";
    }
    return "?";
}

struct empty_body
{
};

struct http_headers
{
    //bool upgrade;
    //bool keep_alive;
    std::string version;
    beast::http::headers fields;
};

template<class Body>
struct http_message : http_headers, Body
{
};

struct string_body
{
    std::string body;

    template<class Streambuf>
    void
    write(Streambuf& sb) const
    {
        using namespace boost::asio;
        sb.commit(buffer_copy(sb.prepare(
            body.size()), buffer(body)));
    }

    void
    prepare(http_headers& h)
    {
        h.fields.append("Content-Length",
            std::to_string(body.size()));
        h.fields.append("Content-Type", "text");
    }
};

struct streambuf_body
{
    beast::asio::streambuf body;
};

template<class Body>
struct http_request : http_message<Body>
{
    std::string url;
    beast::http::method_t method;
};

template<class Body>
struct http_response : http_message<Body>
{
    int status;
    std::string reason;
};

/** Prepare a HTTP message.
    
    This fuses a content body with the headers,
    setting fields as appropriate and returning
    an immutable object.
*/
template<class Body, class String, class Headers, class... Args
    /*
    ,class = std::enable_if_t<std::is_same<
        std::decay_t<Headers>, http_headers>::value>
    */
>
http_response<Body>
prepare_response(int status,
    String&& reason, Headers&& h, Args&&... args)
{
    http_response<Body> m;
    m.status = status;
    m.reason = std::forward<String>(reason);
    static_cast<http_headers&>(m) =
        std::forward<Headers>(h);
    static_cast<std::decay_t<Body>&>(m) =
        Body{std::forward<Args>(args)...};
    m.prepare(m);
    return m;
}

template<class Streambuf>
void
write(Streambuf& sb, http_headers const& m)
{
    write(sb, m.fields);
}

template<class Streambuf, class Body>
void
write(Streambuf& sb, http_message<Body> const& m)
{
    write(sb, static_cast<http_headers const&>(m));
    static_cast<Body const&>(m).write(sb);
}

template<class Streambuf, class Body>
void
write(Streambuf& sb, http_request<Body> const& m)
{
    using namespace detail;
    //sb_write(sb, to_string(m.method()));
    sb_write(sb, " ");
    sb_write(sb, m.url);
    sb_write(sb, " HTTP/");
    sb_write(sb, m.version);
}

template<class Streambuf, class Body>
void
write(Streambuf& sb, http_response<Body> const& m)
{
    detail::sb_write(sb, "HTTP/");
    detail::sb_write(sb, m.version);
    detail::sb_write(sb, " ");
    detail::sb_write(sb, std::to_string(m.status));
    detail::sb_write(sb, " ");
    detail::sb_write(sb, m.reason);
    detail::sb_write(sb, "\r\n");
    write(sb, static_cast<http_headers const&>(m));
    detail::sb_write(sb, "\r\n");
    static_cast<http_message<Body> const&>(m).write(sb);
}

} // wsproto
} // beast

#endif
