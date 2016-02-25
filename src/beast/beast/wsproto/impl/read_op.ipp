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

#ifndef BEAST_WSPROTO_READ_OP_H_INCLUDED
#define BEAST_WSPROTO_READ_OP_H_INCLUDED

#include <beast/asio/handler_alloc.h>
#include <memory>

namespace beast {
namespace wsproto {

// read an entire message
//
template<class Stream>
template<class Streambuf, class Handler>
class socket<Stream>::read_op
{
    using alloc_type =
        handler_alloc<char, Handler>;

    struct data
    {
        socket<Stream>& ws;
        opcode& op;
        Streambuf& sb;
        Handler h;
        frame_info fi;
        bool cont;
        int state = 0;

        template<class DeducedHandler>
        data(DeducedHandler&& h_,
            socket<Stream>& ws_, opcode& op_,
                Streambuf& sb_)
            : ws(ws_)
            , op(op_)
            , sb(sb_)
            , h(std::forward<DeducedHandler>(h_))
            , cont(boost_asio_handler_cont_helpers::
                is_continuation(h))
        {
        }
    };

    std::shared_ptr<data> d_;

public:
    read_op(read_op&&) = default;
    read_op(read_op const&) = default;

    template<class DeducedHandler, class... Args>
    read_op(DeducedHandler&& h,
            socket<Stream>& ws, Args&&... args)
        : d_(std::allocate_shared<data>(alloc_type{h},
            std::forward<DeducedHandler>(h), ws,
                std::forward<Args>(args)...))
    {
        (*this)(error_code{}, false);
    }

    void operator()(
        error_code const& ec, bool again = true);

    friend
    auto asio_handler_allocate(
        std::size_t size, read_op* op)
    {
        return boost_asio_handler_alloc_helpers::
            allocate(size, op->d_->h);
    }

    friend
    auto asio_handler_deallocate(
        void* p, std::size_t size, read_op* op)
    {
        return boost_asio_handler_alloc_helpers::
            deallocate(p, size, op->d_->h);
    }

    friend
    auto asio_handler_is_continuation(read_op* op)
    {
        return op->d_->cont;
    }

    template <class Function>
    friend
    auto asio_handler_invoke(Function&& f, read_op* op)
    {
        return boost_asio_handler_invoke_helpers::
            invoke(f, op->d_->h);
    }
};

template<class Stream>
template<class Streambuf, class Handler>
void
socket<Stream>::read_op<Streambuf, Handler>::
operator()(error_code const& ec, bool again)
{
    auto& d = *d_;
    d.cont = d.cont || again;
    while(! ec && d.state != 99)
    {
        switch(d.state)
        {
        case 0:
            // read payload
            d.state = 1;
            d.ws.async_read_frame(
                d.fi, d.sb, std::move(*this));
            return;

        // got payload
        case 1:
            d.op = d.fi.op;
            d.state = d.fi.fin ? 99 : 0;
            break;
        }
    }
    d.h(ec);
}

} // wsproto
} // beast

#endif
