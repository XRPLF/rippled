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
#include <beast/http/empty_body.h>
#include <beast/http/message.h>
#include <beast/http/read.h>
#include <beast/http/write.h>
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
        handler_alloc<char, Handler>;

    struct data
    {
        socket<Stream>& ws;
        Handler h;
        std::string key;
        http::request<http::empty_body> req;
        http::response<http::string_body> resp;
        bool cont;
        int state = 0;

        template<class DeducedHandler>
        data(DeducedHandler&& h_, socket<Stream>& ws_,
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

    void operator()(error_code ec,
        std::size_t bytes_transferred, bool again = true);

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
        return op->d_->cont;
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
            http::async_read(d.ws.next_layer_,
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

} // wsproto
} // beast

#endif
