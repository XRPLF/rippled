//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_WEBSOCKET_IMPL_ACCEPT_OP_HPP
#define BEAST_WEBSOCKET_IMPL_ACCEPT_OP_HPP

#include <beast/websocket/impl/response_op.ipp>
#include <beast/http/message_v1.hpp>
#include <beast/http/parser_v1.hpp>
#include <beast/http/read.hpp>
#include <beast/core/handler_alloc.hpp>
#include <beast/core/prepare_buffers.hpp>
#include <cassert>
#include <memory>
#include <type_traits>

namespace beast {
namespace websocket {

// read and respond to an upgrade request
//
template<class NextLayer>
template<class Handler>
class stream<NextLayer>::accept_op
{
    using alloc_type =
        handler_alloc<char, Handler>;

    struct data
    {
        stream<NextLayer>& ws;
        http::request_v1<http::string_body> req;
        Handler h;
        bool cont;
        int state = 0;

        template<class DeducedHandler, class Buffers>
        data(DeducedHandler&& h_, stream<NextLayer>& ws_,
                Buffers const& buffers)
            : ws(ws_)
            , h(std::forward<DeducedHandler>(h_))
            , cont(boost_asio_handler_cont_helpers::
                is_continuation(h))
        {
            using boost::asio::buffer_copy;
            using boost::asio::buffer_size;
            ws.reset();
            ws.stream_.buffer().commit(buffer_copy(
                ws.stream_.buffer().prepare(
                    buffer_size(buffers)), buffers));
        }
    };

    std::shared_ptr<data> d_;

public:
    accept_op(accept_op&&) = default;
    accept_op(accept_op const&) = default;

    template<class DeducedHandler, class... Args>
    accept_op(DeducedHandler&& h,
            stream<NextLayer>& ws, Args&&... args)
        : d_(std::allocate_shared<data>(alloc_type{h},
            std::forward<DeducedHandler>(h), ws,
                std::forward<Args>(args)...))
    {
        (*this)(error_code{}, 0, false);
    }

    void operator()(error_code const& ec)
    {
        (*this)(ec, 0);
    }

    void operator()(error_code const& ec,
        std::size_t bytes_transferred, bool again = true);

    friend
    void* asio_handler_allocate(
        std::size_t size, accept_op* op)
    {
        return boost_asio_handler_alloc_helpers::
            allocate(size, op->d_->h);
    }

    friend
    void asio_handler_deallocate(
        void* p, std::size_t size, accept_op* op)
    {
        return boost_asio_handler_alloc_helpers::
            deallocate(p, size, op->d_->h);
    }

    friend
    bool asio_handler_is_continuation(accept_op* op)
    {
        return op->d_->cont;
    }

    template <class Function>
    friend
    void asio_handler_invoke(Function&& f, accept_op* op)
    {
        return boost_asio_handler_invoke_helpers::
            invoke(f, op->d_->h);
    }
};

template<class NextLayer>
template<class Handler>
void 
stream<NextLayer>::accept_op<Handler>::
operator()(error_code const& ec,
    std::size_t bytes_transferred, bool again)
{
    auto& d = *d_;
    d.cont = d.cont || again;
    while(! ec && d.state != 99)
    {
        switch(d.state)
        {
        case 0:
            // read message
            d.state = 1;
            http::async_read(d.ws.next_layer(),
                d.ws.stream_.buffer(), d.req,
                    std::move(*this));
            return;

        // got message
        case 1:
            // respond to request
#if 1
            // VFALCO I have no idea why passing std::move(*this) crashes
            d.state = 99;
            d.ws.async_accept(d.req, *this);
#else
            response_op<Handler>{
                std::move(d.h), d.ws, d.req, true};
#endif
            return;
        }
    }
    d.h(ec);
}

} // websocket
} // beast

#endif
