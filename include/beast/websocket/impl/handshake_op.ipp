//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_WEBSOCKET_IMPL_HANDSHAKE_OP_HPP
#define BEAST_WEBSOCKET_IMPL_HANDSHAKE_OP_HPP

#include <beast/handler_alloc.hpp>
#include <beast/http/empty_body.hpp>
#include <beast/http/message.hpp>
#include <beast/http/read.hpp>
#include <beast/http/write.hpp>
#include <cassert>
#include <memory>

namespace beast {
namespace websocket {

// send the upgrade request and process the response
//
template<class NextLayer>
template<class Handler>
class stream<NextLayer>::handshake_op
{
    using alloc_type =
        handler_alloc<char, Handler>;

    struct data
    {
        stream<NextLayer>& ws;
        Handler h;
        std::string key;
        http::request<http::empty_body> req;
        http::response<http::string_body> resp;
        bool cont;
        int state = 0;

        template<class DeducedHandler>
        data(DeducedHandler&& h_, stream<NextLayer>& ws_,
            boost::string_ref const& host,
                boost::string_ref const& resource)
            : ws(ws_)
            , h(std::forward<DeducedHandler>(h_))
            , req(ws.build_request(host, resource, key))
            , cont(boost_asio_handler_cont_helpers::
                is_continuation(h))
        {
        }
    };

    std::shared_ptr<data> d_;

public:
    handshake_op(handshake_op&&) = default;
    handshake_op(handshake_op const&) = default;

    template<class DeducedHandler, class... Args>
    handshake_op(DeducedHandler&& h,
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

    void operator()(error_code ec,
        std::size_t bytes_transferred, bool again = true);

    friend
    void* asio_handler_allocate(
        std::size_t size, handshake_op* op)
    {
        return boost_asio_handler_alloc_helpers::
            allocate(size, op->d_->h);
    }

    friend
    void asio_handler_deallocate(
        void* p, std::size_t size, handshake_op* op)
    {
        return boost_asio_handler_alloc_helpers::
            deallocate(p, size, op->d_->h);
    }

    friend
    bool asio_handler_is_continuation(handshake_op* op)
    {
        return op->d_->cont;
    }

    template <class Function>
    friend
    void asio_handler_invoke(Function&& f, handshake_op* op)
    {
        return boost_asio_handler_invoke_helpers::
            invoke(f, op->d_->h);
    }
};

template<class NextLayer>
template<class Handler>
void
stream<NextLayer>::handshake_op<
    Handler>::operator()(error_code ec,
        std::size_t bytes_transferred, bool again)
{
    auto& d = *d_;
    d.cont = d.cont || again;
    while(! ec && d.state != 99)
    {
        switch(d.state)
        {
        case 0:
        {
            // send http upgrade
            d.state = 1;
            // VFALCO Do we need the ability to move
            //        a message on the async_write?
            http::async_write(d.ws.stream_,
                d.req, std::move(*this));
            return;
        }

        // sent upgrade
        case 1:
            // read http response
            d.state = 2;
            http::async_read(d.ws.next_layer(),
                d.ws.stream_.buffer(), d.resp,
                    std::move(*this));
            return;

        // got response
        case 2:
        {
            d.ws.do_response(d.resp, d.key, ec);
            // call handler
            d.state = 99;
            break;
        }
        }
    }
    d.h(ec);
}

} // websocket
} // beast

#endif
