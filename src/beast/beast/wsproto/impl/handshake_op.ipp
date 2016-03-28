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

#ifndef BEAST_WSPROTO_HANDSHAKE_OP_H_INCLUDED
#define BEAST_WSPROTO_HANDSHAKE_OP_H_INCLUDED

#include <beast/asio/handler_alloc.h>
#include <beast/http/message.h>
#include <beast/http/parser.h>
#include <cassert>
#include <memory>

namespace beast {
namespace wsproto {

// send the upgrade request and process the response
//
template<class Stream>
template<class Handler>
class socket<Stream>::handshake_op
{
    using alloc_type =
        asio::handler_alloc<char, Handler>;

    // VFALCO TODO use beast streambuf
    using streambuf_type =
        boost::asio::basic_streambuf<alloc_type>;

    struct data
    {
        socket<Stream>& ws;
        Handler h;
        std::string host;
        std::string resource;
        streambuf_type sb;
        int state = 0;

        template<class DeducedHandler>
        data(DeducedHandler&& h_, socket<Stream>& ws_,
                std::string const& host_, std::string const& resource_)
            : ws(ws_)
            , h(std::forward<DeducedHandler>(h_))
            , host(host_)
            , resource(resource_)
            , sb(std::numeric_limits<std::size_t>::max(), h)
        {
        }
    };

    std::shared_ptr<data> d_;

public:
    handshake_op(handshake_op&&) = default;
    handshake_op(handshake_op const&) = default;

    template<class DeducedHandler, class... Args>
    explicit
    handshake_op(DeducedHandler&& h, Args&&... args)
        : d_(std::allocate_shared<data>(alloc_type{h},
            std::forward<DeducedHandler>(h),
                std::forward<Args>(args)...))
    {
        (*this)(error_code{}, 0);
    }

    void operator()(error_code ec,
        std::size_t bytes_transferred);

    friend
    auto asio_handler_allocate(
        std::size_t size, handshake_op* op)
    {
        return boost_asio_handler_alloc_helpers::
            allocate(size, op->d_->h);
    }

    friend
    auto asio_handler_deallocate(
        void* p, std::size_t size, handshake_op* op)
    {
        return boost_asio_handler_alloc_helpers::
            deallocate(p, size, op->d_->h);
    }

    friend
    auto asio_handler_is_continuation(handshake_op* op)
    {
        return op->d_->state >= 1 ||
            boost_asio_handler_cont_helpers::
                is_continuation(op->d_->h);
    }

    template <class Function>
    friend
    auto asio_handler_invoke(Function&& f, handshake_op* op)
    {
        return boost_asio_handler_invoke_helpers::
            invoke(f, op->d_->h);
    }
};

template<class Stream>
template<class Handler>
void
socket<Stream>::handshake_op<
    Handler>::operator()(error_code ec,
        std::size_t bytes_transferred)
{
    using namespace boost::asio;
    auto& d = *d_;
    while(! ec && d.state != 99)
    {
        switch(d.state)
        {
        case 0:
        {
            auto const m(d.ws.make_upgrade(
                d.host, d.resource));
            beast::http::write(d.sb, m);
            // send http upgrade
            d.state = 1;
            boost::asio::async_write(d.ws.stream_,
                d.sb.data(), std::move(*this));
            return;
        }

        // sent http upgrade
        case 1:
            assert(bytes_transferred == d.sb.size());
            d.sb.consume(d.sb.size());
            // read http response
            d.state = 2;
            boost::asio::async_read_until(d.ws.stream_,
                d.sb, "\r\n\r\n", std::move(*this));
            return;

        // got http response
        case 2:
        {
            http::body b;
            http::message m;
            http::parser p(m, b, false);
            auto result = p.write(d.sb.data());
            d.sb.consume(result.second);
            // VFALCO This is a massive hack to deal with the
            // parser expecting "eof" on a Connection: Close response
            if(! result.first && ! p.complete())
                result.first = p.write_eof();
            if(! p.complete() || result.first)
                ec = error::response_malformed;
            if(! ec)
                d.ws.do_response(m, ec);
            if(! ec)
                d.ws.role_ = role_type::client;
            // VFALCO What about what's left in d.sb.data()?
            // call handler
            d.state = 99;
            break;
        }
        }
    }
    d.h(ec);
}

} // wsproto
} // beast

#endif
