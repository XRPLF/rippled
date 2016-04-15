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
namespace detail {

// read an entire message
//
template<class Stream, class Streambuf, class Handler, class Data>
class op_imp
{
    using alloc_type =
        handler_alloc<char, Handler>;

    std::shared_ptr<Data> d_;

public:
    op_imp(op_imp&&) = default;
    op_imp(op_imp const&) = default;

    template<class DeducedHandler, class... Args>
    op_imp(DeducedHandler&& h,
            socket<Stream>& ws, Args&&... args)
        : d_(std::allocate_shared<Data>(alloc_type{h},
            std::forward<DeducedHandler>(h), ws,
                std::forward<Args>(args)...))
    {
        (*this)(error_code{}, 0);
    }

    void operator()()
    {
        (*this)(error_code{}, 0);
    }

    void operator()(error_code const& ec)
    {
        (*this)(ec, 0);
    }

    void operator()(error_code const& ec,
        std::size_t bytes_transferred)
    {
        (*d_)(*this, ec, bytes_transferred);
    }

    friend
    auto asio_handler_allocate(
        std::size_t size, op_imp* op)
    {
        return boost_asio_handler_alloc_helpers::
            allocate(size, op->d_->h);
    }

    friend
    auto asio_handler_deallocate(
        void* p, std::size_t size, op_imp* op)
    {
        return boost_asio_handler_alloc_helpers::
            deallocate(p, size, op->d_->h);
    }

    friend
    auto asio_handler_is_continuation(op_imp* op)
    {
        return op->d_->state >= 1 ||
            boost_asio_handler_cont_helpers::
                is_continuation(op->d_->h);
    }

    template <class Function>
    friend
    auto asio_handler_invoke(Function&& f, op_imp* op)
    {
        return boost_asio_handler_invoke_helpers::
            invoke(f, op->d_->h);
    }
};

template<class Stream, class Streambuf, class Handler>
struct read_op_data
{
    socket<Stream>& ws;
    opcode& op;
    Streambuf& sb;
    Handler h;
    msg_info mi;
    int state = 0;

    template<class DeducedHandler>
    read_op_data(DeducedHandler&& h_,
         socket<Stream>& ws_, opcode& op_,
         Streambuf& sb_)
            : ws(ws_)
            , op(op_)
            , sb(sb_)
            , h(std::forward<DeducedHandler>(h_))
    {
    }

    template<class Op>
    void operator()(Op&& op, error_code const& ec,
            std::size_t bytes_transferred)
    {
        auto& d = *this; //TBD
        while(! ec && d.state != 99)
        {
            switch(d.state)
            {
                case 0:
                case 1:
                    // read payload
                    d.state = 2;
                    d.ws.async_read_some(
                        d.mi, d.sb, std::move(op));
                    return;

                    // got payload
                case 2:
                    d.op = d.mi.op;
                    if(! d.mi.fin)
                    {
                        d.state = 1;
                        break;
                    }
                    // call handler
                    d.state = 99;
                    break;
            }
        }
        d.h(ec);
    }
};

template<class Stream, class Streambuf, class Handler>
using read_op = op_imp<Stream, Streambuf, Handler, read_op_data<Stream, Streambuf, Handler>>;

} // detail
} // wsproto
} // beast

#endif
