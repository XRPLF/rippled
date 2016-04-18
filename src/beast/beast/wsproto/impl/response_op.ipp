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

#ifndef BEAST_WSPROTO_RESPONSE_OP_H_INCLUDED
#define BEAST_WSPROTO_RESPONSE_OP_H_INCLUDED

#include <beast/asio/handler_alloc.h>
#include <beast/http/string_body.h>
#include <beast/http/write.h>
#include <memory>

namespace beast {
namespace wsproto {

// Respond to an upgrade HTTP request
template<class Stream>
template<class Handler>
class socket<Stream>::response_op
{
    using alloc_type =
        handler_alloc<char, Handler>;

    struct data
    {
        socket<Stream>& ws;
        http::prepared_response<http::string_body> resp;
        Handler h;
        error_code final_ec;
        bool cont;
        int state = 0;

        template<class DeducedHandler,
            class Body, class Allocator>
        data(DeducedHandler&& h_, socket<Stream>& ws_,
            http::parsed_request<Body, Allocator> const& req,
                bool cont_)
            : ws(ws_)
            , resp(ws_.build_response(req))
            , h(std::forward<DeducedHandler>(h_))
            , cont(cont_)
        {
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
            socket<Stream>& ws, Args&&... args)
        : d_(std::allocate_shared<data>(alloc_type{h},
            std::forward<DeducedHandler>(h), ws,
                std::forward<Args>(args)...))
    {
        (*this)(error_code{}, 0);
    }

    void operator()(error_code const& ec)
    {
        (*this)(ec, 0);
    }

    void operator()(error_code ec,
        std::size_t bytes_transferred);

    friend
    auto asio_handler_allocate(
        std::size_t size, response_op* op)
    {
        return boost_asio_handler_alloc_helpers::
            allocate(size, op->d_->h);
    }

    friend
    auto asio_handler_deallocate(
        void* p, std::size_t size, response_op* op)
    {
        return boost_asio_handler_alloc_helpers::
            deallocate(p, size, op->d_->h);
    }

    friend
    auto asio_handler_is_continuation(response_op* op)
    {
        return op->d_->cont || op->d_->state >= 1 ||
            boost_asio_handler_cont_helpers::
                is_continuation(op->d_->h);
    }

    template <class Function>
    friend
    auto asio_handler_invoke(Function&& f, response_op* op)
    {
        return boost_asio_handler_invoke_helpers::
            invoke(f, op->d_->h);
    }
};

template<class Stream>
template<class Handler>
void 
socket<Stream>::response_op<Handler>::
operator()(error_code ec, std::size_t bytes_transferred)
{
    auto& d = *d_;
    while(! ec && d.state != 99)
    {
        switch(d.state)
        {
        case 0:
            // send response
            d.state = 1;
            http::async_write(d.ws.next_layer_,
                d.resp, std::move(*this));
            return;

        // sent response
        case 1:
            d.state = 99;
            ec = d.final_ec;
            if(! ec)
                d.ws.role_ = role_type::server;
            break;
        }
    }
    d.h(ec);
}

} // wsproto
} // beast

#endif
