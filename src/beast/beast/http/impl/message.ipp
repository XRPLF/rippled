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

#ifndef BEAST_HTTP_MESSAGE_IPP_INCLUDED
#define BEAST_HTTP_MESSAGE_IPP_INCLUDED

#include <beast/http/detail/writes.h>
#include <boost/asio/buffer.hpp>

namespace beast {
namespace http {

template<bool isReq, class Body, class Allocator>
template<class... Args>
message<isReq, Body, Allocator>::
        message(request_params params, Args&&... args)
    : body(std::forward<Args>(args)...)
{
    static_assert(isReq, "message is not a request");
    this->method = params.method;
    this->url = params.url;
    version = params.version;
    Body::prepare(*this);
}
         
template<bool isReq, class Body, class Allocator>
template<class... Args>
message<isReq, Body, Allocator>::
        message(response_params params, Args&&... args)
    : body(std::forward<Args>(args)...)
{
    static_assert(! isReq, "message is not a response");
    this->status = params.status;
    this->reason = params.reason;
    version = params.version;
    Body::prepare(*this);
}

template<bool isReq, class Body, class Allocator>
template<class... Args>
message<isReq, Body, Allocator>::
        message(Args&&... args)
    : body(std::forward<Args>(args)...)
{
}

template<bool isReq, class Body, class Allocator>
template<class Streambuf, class>
void
message<isReq, Body, Allocator>::
write(Streambuf& streambuf) const
{
    using boost::asio::buffer_copy;
    using boost::asio::buffer_size;
    typename Body::writer w(*this);
    auto const& data = w.data();
    streambuf.commit(buffer_copy(
        streambuf.prepare(
            buffer_size(data)), data));
}

template<bool isReq, class Body, class Allocator>
template<class Streambuf>
void
message<isReq, Body, Allocator>::
write_headers(Streambuf& streambuf,
    std::true_type) const
{
    detail::write(streambuf, to_string(this->method));
    detail::write(streambuf, " ");
    detail::write(streambuf, this->url);
    switch(version)
    {
    case 10:
        detail::write(streambuf, " HTTP/1.0\r\n");
        break;
    case 11:
        detail::write(streambuf, " HTTP/1.1\r\n");
        break;
    default:
        detail::write(streambuf, " HTTP/");
        detail::write(streambuf, version / 10);
        detail::write(streambuf, ".");
        detail::write(streambuf, version % 10);
        detail::write(streambuf, "\r\n");
        break;
    }
    fields.write(streambuf);
    detail::write(streambuf, "\r\n");
}

template<bool isReq, class Body, class Allocator>
template<class Streambuf>
void
message<isReq, Body, Allocator>::
write_headers(Streambuf& streambuf,
    std::false_type) const
{
    switch(version)
    {
    case 10:
        detail::write(streambuf, "HTTP/1.0 ");
        break;
    case 11:
        detail::write(streambuf, "HTTP/1.1 ");
        break;
    default:
        detail::write(streambuf, " HTTP/");
        detail::write(streambuf, version / 10);
        detail::write(streambuf, ".");
        detail::write(streambuf, version % 10);
        detail::write(streambuf, " ");
        break;
    }
    detail::write(streambuf, this->status);
    detail::write(streambuf, " ");
    detail::write(streambuf, this->reason);
    detail::write(streambuf, "\r\n");
    fields.write(streambuf);
    detail::write(streambuf, "\r\n");
}

} // http
} // beast

#include <beast/http/impl/message.ipp>

#endif
