//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_WEBSOCKET_IMPL_RESPONSE_OP_HPP
#define BEAST_WEBSOCKET_IMPL_RESPONSE_OP_HPP

#include <beast/http/message_v1.hpp>
#include <beast/http/string_body.hpp>
#include <beast/http/write.hpp>
#include <beast/core/handler_alloc.hpp>
#include <memory>

namespace beast {
namespace websocket {

// Respond to an upgrade HTTP request
template<class NextLayer>
template<class Handler>
class stream<NextLayer>::response_op
{
    using alloc_type =
        handler_alloc<char, Handler>;

    struct data
    {
        stream<NextLayer>& ws;
        http::response_v1<http::string_body> resp;
        Handler h;
        error_code final_ec;
        bool cont;
        int state = 0;

        template<class DeducedHandler,
            class Body, class Headers>
        data(DeducedHandler&& h_, stream<NextLayer>& ws_,
            http::request_v1<Body, Headers> const& req,
                bool cont_)
            : ws(ws_)
            , resp(ws_.build_response(req))
            , h(std::forward<DeducedHandler>(h_))
            , cont(cont_)
        {
            // can't call stream::reset() here
            // otherwise accept_op will malfunction
            //
            if(resp.status != 101)
                final_ec = error::handshake_failed;
        }
    };

    std::shared_ptr<data> d_;

public:
    response_op(response_op&&) = default;
    response_op(response_op const&) = default;

    template<class DeducedHandler, class... Args>
    response_op(DeducedHandler&& h,
            stream<NextLayer>& ws, Args&&... args)
        : d_(std::allocate_shared<data>(alloc_type{h},
            std::forward<DeducedHandler>(h), ws,
                std::forward<Args>(args)...))
    {
        (*this)(error_code{}, false);
    }

    void operator()(
        error_code ec, bool again = true);

    friend
    void* asio_handler_allocate(
        std::size_t size, response_op* op)
    {
        return boost_asio_handler_alloc_helpers::
            allocate(size, op->d_->h);
    }

    friend
    void asio_handler_deallocate(
        void* p, std::size_t size, response_op* op)
    {
        return boost_asio_handler_alloc_helpers::
            deallocate(p, size, op->d_->h);
    }

    friend
    bool asio_handler_is_continuation(response_op* op)
    {
        return op->d_->cont;
    }

    template <class Function>
    friend
    void asio_handler_invoke(Function&& f, response_op* op)
    {
        return boost_asio_handler_invoke_helpers::
            invoke(f, op->d_->h);
    }
};

template<class NextLayer>
template<class Handler>
void 
stream<NextLayer>::response_op<Handler>::
operator()(error_code ec, bool again)
{
    auto& d = *d_;
    d.cont = d.cont || again;
    while(! ec && d.state != 99)
    {
        switch(d.state)
        {
        case 0:
            // send response
            d.state = 1;
            http::async_write(d.ws.next_layer(),
                d.resp, std::move(*this));
            return;

        // sent response
        case 1:
            d.state = 99;
            ec = d.final_ec;
            if(! ec)
                d.ws.open(detail::role_type::server);
            break;
        }
    }
    d.h(ec);
}

} // websocket
} // beast

#endif
