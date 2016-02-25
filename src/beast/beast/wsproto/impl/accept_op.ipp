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

#include <beast/wsproto/impl/response_op.ipp>
#include <beast/asio/handler_alloc.h>
#include <beast/asio/prepare_buffers.h>
#include <beast/http/parser.h>
#include <beast/http/read.h>
#include <cassert>
#include <memory>
#include <type_traits>

namespace beast {
namespace wsproto {

// read and respond to an upgrade request
//
template<class Stream>
template<class Handler>
class socket<Stream>::accept_op
{
    using alloc_type =
        handler_alloc<char, Handler>;

    struct data
    {
        socket<Stream>& ws;
        http::request<http::empty_body> req;
        Handler h;
        bool cont;
        int state = 0;

        template<class DeducedHandler, class Buffers>
        data(DeducedHandler&& h_, socket<Stream>& ws_,
                Buffers const& buffers)
            : ws(ws_)
            , h(std::forward<DeducedHandler>(h_))
            , cont(boost_asio_handler_cont_helpers::
                is_continuation(h))
        {
            using boost::asio::buffer_copy;
            using boost::asio::buffer_size;
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
            socket<Stream>& ws, Args&&... args)
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
        return op->d_->cont;
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
template<class Handler>
void 
socket<Stream>::accept_op<Handler>::
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
            http::async_read(d.ws.next_layer_,
                d.ws.stream_.buffer(), d.req,
                    std::move(*this));
            return;

        // got message
        case 1:
            // respond to request
            response_op<Handler>{
                std::move(d.h), d.ws, d.req, true};
            return;
        }
    }
    d.h(ec);
}

} // wsproto
} // beast

#endif
