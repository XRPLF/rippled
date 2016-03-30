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

#ifndef BEAST_WSPROTO_ACCEPT_OP_H_INCLUDED
#define BEAST_WSPROTO_ACCEPT_OP_H_INCLUDED

#include <beast/asio/handler_alloc.h>
#include <beast/asio/prepare_buffers.h>
#include <beast/asio/read_until.h>
#include <beast/http/parser.h>
#include <beast/http/read.h>
#include <boost/optional.hpp>
#include <cassert>
#include <memory>
#include <type_traits>

namespace beast {
namespace wsproto {

// read and respond to an upgrade request
//
// VFALCO Split this into two operations,
//        one that uses a buffer the other a message.
//
template<class Stream>
template<class Body, class Allocator, class Handler>
class socket<Stream>::accept_op
{
    using alloc_type =
        handler_alloc<char, Handler>;

    using streambuf_type =
        basic_streambuf<alloc_type>;

    struct data
    {
        socket<Stream>& ws;
        deprecated_http::body body;
        http::parsed_request<Body, Allocator> m;
        Handler h;
        error_code ec;
        streambuf_type wb;
        boost::optional<http::request_parser<Body, Allocator>> p;
        int state = 0;

        template<class DeducedHandler, class Buffers>
        data(DeducedHandler&& h_, socket<Stream>& ws_,
                Buffers const& buffers)
            : ws(ws_)
            , h(std::forward<DeducedHandler>(h_))
            , wb(1024, alloc_type{h})
        {
            ws.stream_.buffer().commit(
                boost::asio::buffer_copy(
                    ws.stream_.buffer().prepare(
                        boost::asio::buffer_size(buffers)),
                            buffers));
            p.emplace();
        }

        template<class DeducedHandler, class DeducedMessage>
        data(DeducedHandler&& h_, socket<Stream>& ws_,
                DeducedMessage&& m_, void const*)
            : ws(ws_)
            , m(std::forward<DeducedMessage>(m_))
            , h(std::forward<DeducedHandler>(h_))
            , wb(1024, alloc_type{h})
        {
        }
    };

    std::shared_ptr<data> d_;

public:
    accept_op(accept_op&&) = default;
    accept_op(accept_op const&) = default;

    template<class DeducedHandler, class... Args,
        class = std::enable_if_t<
            ! std::is_same<std::decay_t<
                DeducedHandler>, accept_op>::value>>
    explicit
    accept_op(DeducedHandler&& h, Args&&... args)
        : d_(std::allocate_shared<data>(alloc_type{h},
            std::forward<DeducedHandler>(h),
                std::forward<Args>(args)...))
    {
        (*this)(error_code{}, 0);
    }

    void operator()(error_code ec)
    {
        (*this)(error_code{}, 0);
    }

    void operator()(error_code ec,
        std::size_t bytes_transferred);

    friend
    auto asio_handler_allocate(
        std::size_t size, accept_op* op)
    {
        return boost_asio_handler_alloc_helpers::
            allocate(size, op->d_->h);
    }

    friend
    auto asio_handler_deallocate(
        void* p, std::size_t size, accept_op* op)
    {
        return boost_asio_handler_alloc_helpers::
            deallocate(p, size, op->d_->h);
    }

    friend
    auto asio_handler_is_continuation(accept_op* op)
    {
        return op->d_->state >= 2 ||
            boost_asio_handler_cont_helpers::
                is_continuation(op->d_->h);
    }

    template <class Function>
    friend
    auto asio_handler_invoke(Function&& f, accept_op* op)
    {
        return boost_asio_handler_invoke_helpers::
            invoke(f, op->d_->h);
    }
};

template<class Stream>
template<class Body, class Allocator, class Handler>
void 
socket<Stream>::accept_op<Body, Allocator, Handler>::
operator()(error_code ec, std::size_t bytes_transferred)
{
    auto& d = *d_;
    while(! ec && d.state != 999)
    {
        switch(d.state)
        {
        case 0:
            if(! d.p)
            {
                d.state = 1;
                break;
            }
            // read message
            d.state = 3;
            http::async_read(d.ws.next_layer_,
                d.ws.stream_.buffer(), *d.p,
                    std::move(*this));
            boost::asio::async_read_until(
                d.ws.next_layer_, d.ws.stream_.buffer(),
                    "\r\n\r\n", std::move(*this));
            return;

        // got message
        case 1:
        case 2:
            ec = d.ws.do_accept(d.m);
            if(ec)
            {
                // send error response
                d.ec = ec;
                d.state = 4;
                d.ws.write_error(d.wb, ec);
                boost::asio::async_write(d.ws.stream_,
                    d.wb.data(), std::move(*this));
                return;
            }
            d.ws.write_response(d.wb, d.m);
            // send response
            d.state = 999;
            d.ws.role_ = role_type::server;
            boost::asio::async_write(d.ws.stream_,
                d.wb.data(), std::move(*this));
            return;

        // parser done
        case 3:
            d.state = 2;
            d.m = d.p->release();
            break;

        // sent error response
        case 4:
            // call handler
            ec = d.ec;
            d.state = 999;
            break;
        }
    }
    d.h(ec);
}

} // wsproto
} // beast

#endif
