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
message<isReq, Body, Allocator>::
message(request_params params)
{
    static_assert(isReq, "message is not a request");
    this->method = params.method;
    this->url = params.url;
    version = params.version;
}
         
template<bool isReq, class Body, class Allocator>
message<isReq, Body, Allocator>::
message(response_params params)
{
    static_assert(! isReq, "message is not a response");
    this->status = params.status;
    this->reason = params.reason;
    version = params.version;
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
    headers.write(streambuf);
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
    headers.write(streambuf);
    detail::write(streambuf, "\r\n");
}

//------------------------------------------------------------------------------

template<class Body, class Allocator,
    class Opt>
inline
void
prepare_one(prepared_request<Body, Allocator>&,
    Opt&&)
{
    // forward to Body if possible else static assert
}

template<class Body, class Allocator,
    class ReqBody, class ReqAllocator,
        class Opt>
inline
void
prepare_one(prepared_response<Body, Allocator>&,
    parsed_request<ReqBody, ReqAllocator> const&,
        Opt&&)
{
    // forward to Body if possible else static assert
}

namespace detail {

template<class Body, class Allocator,
    class ReqBody, class ReqAllocator>
inline
void
prepare_opts(prepared_response<Body, Allocator>&,
    parsed_request<ReqBody, ReqAllocator> const&)
{
}

template<class Body, class Allocator,
    class ReqBody, class ReqAllocator,
        class Opt, class... Opts>
inline
void
prepare_opts(prepared_response<Body, Allocator>& msg,
    parsed_request<ReqBody, ReqAllocator> const& req,
        Opt&& opt, Opts&&... opts)
{
    prepare_one(msg, req, std::forward<Opt>(opt));
    prepare_opts(msg, req, std::forward<Opts>(opts)...);
}

template<class Body, class Allocator>
inline
void
prepare_opts(prepared_request<Body, Allocator>&)
{
}

template<class Body, class Allocator,
    class Opt, class... Opts>
inline
void
prepare_opts(prepared_request<Body, Allocator>& msg,
    Opt&& opt, Opts&&... opts)
{
    prepare_one(msg, std::forward<Opt>(opt));
    prepare_opts(msg, std::forward<Opts>(opts)...);
}

} // detail

//------------------------------------------------------------------------------

namespace detail {

template<bool isRequest, class Body, class Allocator>
void
set_connection(prepared_message<
    isRequest, Body, Allocator>& msg,
        connection_value value)
{
    switch(value)
    {
    case connection_value::keep_alive:
    {
        if(msg.version < 11)
            msg.headers.replace("Connection", "Keep-Alive");
        else
            msg.headers.erase("Connection");
        msg.keep_alive = true;
        break;
    }
    case connection_value::close:
    {
        if(msg.version >= 11)
            msg.headers.replace("Connection", "Close");
        else
            msg.headers.erase("Connection");
        msg.keep_alive = false;
        break;
    }
    case connection_value::upgrade:
    {
        if(msg.version < 11)
            msg.version = 11;
        msg.headers.replace("Connection", "Upgrade");
        break;
    }
    }
}

} // detail

template<bool isRequest, class Body, class Allocator>
template<class... Opts>
prepared_message<isRequest, Body, Allocator>::
prepared_message(request<Body, Allocator>&& msg,
        Opts&&... opts)
    : message<isRequest, Body, Allocator>(std::move(msg))
{
    static_assert(isRequest, "");
    construct(std::forward<Opts>(opts)...);
}

template<bool isRequest, class Body, class Allocator>
template<class... Opts>
prepared_message<isRequest, Body, Allocator>::
prepared_message(request<Body, Allocator> const& msg,
        Opts&&... opts)
    : message<isRequest, Body, Allocator>(msg)
{
    static_assert(isRequest, "");
    construct(std::forward<Opts>(opts)...);
}

template<bool isRequest, class Body, class Allocator>
template<class ReqBody, class ReqAllocator, class... Opts>
prepared_message<isRequest, Body, Allocator>::
prepared_message(response<Body, Allocator>&& msg,
    parsed_request<ReqBody, ReqAllocator> const& req,
        Opts&&... opts)
    : message<isRequest, Body, Allocator>(std::move(msg))
{
    static_assert(! isRequest, "");
    construct(req, std::forward<Opts>(opts)...);
}

template<bool isRequest, class Body, class Allocator>
template<class ReqBody, class ReqAllocator, class... Opts>
prepared_message<isRequest, Body, Allocator>::
prepared_message(response<Body, Allocator> const& msg,
    parsed_request<ReqBody, ReqAllocator> const& req,
        Opts&&... opts)
    : message<isRequest, Body, Allocator>(msg)
{
    static_assert(! isRequest, "");
    construct(req, std::forward<Opts>(opts)...);
}

template<bool isRequest, class Body, class Allocator>
template<class... Opts>
void
prepared_message<isRequest, Body, Allocator>::
construct(Opts&&... opts)
{
    detail::set_connection(*this, this->version >= 11 ?
        connection_value::keep_alive : connection_value::close);
    detail::prepare_opts(*this, std::forward<Opts>(opts)...);
    Body::prepare(*this);
}

template<bool isRequest, class Body, class Allocator>
template<class ReqBody, class ReqAllocator,
    class... Opts>
void
prepared_message<isRequest, Body, Allocator>::
construct(parsed_request<ReqBody, ReqAllocator> const& req,
    Opts&&... opts)
{
    detail::set_connection(*this, req.keep_alive ?
        connection_value::keep_alive : connection_value::close);
    detail::prepare_opts(*this, req,
        std::forward<Opts>(opts)...);
    Body::prepare(*this, req);
}

//------------------------------------------------------------------------------

template<class Body, class Allocator>
inline
void
prepare_one(prepared_request<Body, Allocator>& msg,
    connection const& opt)
{
    detail::set_connection(msg, opt.value);
}

template<class Body, class Allocator,
    class ReqBody, class ReqAllocator>
inline
void
prepare_one(prepared_response<Body, Allocator>& msg,
    parsed_request<ReqBody, ReqAllocator> const& req,
        connection opt)
{
    if(opt.value == connection_value::keep_alive &&
            ! req.keep_alive)
        opt.value = connection_value::close;
    detail::set_connection(msg, opt.value);
}

} // http
} // beast

#include <beast/http/impl/message.ipp>

#endif
