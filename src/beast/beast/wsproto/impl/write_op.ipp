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

#ifndef BEAST_WSPROTO_WRITE_OP_H_INCLUDED
#define BEAST_WSPROTO_WRITE_OP_H_INCLUDED

#include <beast/asio/consuming_buffers.h>
#include <beast/asio/prepare_buffers.h>
#include <beast/asio/handler_alloc.h>
#include <beast/wsproto/detail/frame.h>
#include <algorithm>
#include <cassert>
#include <memory>

namespace beast {
namespace wsproto {

// write a message
//
template<class Stream>
template<class Buffers, class Handler>
class socket<Stream>::write_op
{
    using alloc_type =
        handler_alloc<char, Handler>;

    struct data : op
    {
        socket<Stream>& ws;
        consuming_buffers<Buffers> cb;
        Handler h;
        std::size_t remain;
        bool cont;
        int state = 0;

        template<class DeducedHandler>
        data(DeducedHandler&& h_,
            socket<Stream>& ws_, Buffers const& bs)
            : ws(ws_)
            , cb(bs)
            , h(std::forward<DeducedHandler>(h_))
            , remain(boost::asio::buffer_size(cb))
            , cont(boost_asio_handler_cont_helpers::
                is_continuation(h))
        {
        }
    };

    std::shared_ptr<data> d_;

public:
    write_op(write_op&&) = default;
    write_op(write_op const&) = default;

    template<class DeducedHandler, class... Args>
    explicit
    write_op(DeducedHandler&& h,
            socket<Stream>& ws, Args&&... args)
        : d_(std::allocate_shared<data>(alloc_type{h},
            std::forward<DeducedHandler>(h), ws,
                std::forward<Args>(args)...))
    {
        (*this)(error_code{}, false);
    }

    void operator()(error_code ec, bool again = true);

    friend
    auto asio_handler_allocate(
        std::size_t size, write_op* op)
    {
        return boost_asio_handler_alloc_helpers::
            allocate(size, op->d_->h);
    }

    friend
    auto asio_handler_deallocate(
        void* p, std::size_t size, write_op* op)
    {
        return boost_asio_handler_alloc_helpers::
            deallocate(p, size, op->d_->h);
    }

    friend
    auto asio_handler_is_continuation(write_op* op)
    {
        return op->d_->cont;
    }

    template <class Function>
    friend
    auto asio_handler_invoke(Function&& f, write_op* op)
    {
        return boost_asio_handler_invoke_helpers::
            invoke(f, op->d_->h);
    }
};

template<class Stream>
template<class Buffers, class Handler>
void
socket<Stream>::
write_op<Buffers, Handler>::
operator()(error_code ec, bool again)
{
    auto& d = *d_;
    d.cont = d.cont || again;
    while(! ec && d.state != 99)
    {
        switch(d.state)
        {
        case 0:
        {
            auto const n = std::min(
                d.remain, d.ws.wr_frag_size_);
            d.remain -= n;
            auto const fin = d.remain <= 0;
            if(fin)
                d.state = 99;
            d.ws.async_write_frame(fin,
                prepare_buffers(n, d.cb), std::move(*this));
            d.cb.consume(n);
            return;
        }
        }
    }
    d.h(ec);
}

} // wsproto
} // beast

#endif
