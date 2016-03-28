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

namespace beast {
namespace wsproto {
namespace detail {

// read an entire message
//
template<class Stream, class Streambuf, class Handler>
class read_op
{
    using alloc_type =
        asio::handler_alloc<char, Handler>;

    struct data
    {
        socket<Stream>& ws;
        opcode::value& op;
        Streambuf& sb;
        Handler h;
        msg_info mi;
        int state = 0;

        template<class DeducedHandler>
        data(DeducedHandler&& h_,
            socket<Stream>& ws_, opcode::value& op_,
                Streambuf& sb_)
            : ws(ws_)
            , op(op_)
            , sb(sb_)
            , h(std::forward<DeducedHandler>(h_))
        {
        }
    };

    std::shared_ptr<data> d_;

public:
    read_op(read_op&&) = default;
    read_op(read_op const&) = default;

    template<class DeducedHandler, class... Args>
    explicit
    read_op(DeducedHandler&& h, Args&&... args)
        : d_(std::allocate_shared<data>(alloc_type{h},
            std::forward<DeducedHandler>(h),
                std::forward<Args>(args)...))
    {
        (*this)(error_code{}, 0);
    }

    void operator()()
    {
        (*this)(error_code{}, 0);
    }

    void operator()(error_code ec)
    {
        (*this)(ec, 0);
    }

    void operator()(error_code ec,
        std::size_t bytes_transferred);

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
        return op->d_->state >= 1 ||
            boost_asio_handler_cont_helpers::
                is_continuation(op->d_->h);
    }

    template <class Function>
    friend
    auto asio_handler_invoke(Function&& f, read_op* op)
    {
        return boost_asio_handler_invoke_helpers::
            invoke(f, op->d_->h);
    }
};

template<class Stream, class Streambuf, class Handler>
void
read_op<Stream, Streambuf,
    Handler>::operator()(error_code ec,
        std::size_t bytes_transferred)
{
    using namespace boost::asio;
    auto& d = *d_;
    while(! ec && d.state != 99)
    {
        switch(d.state)
        {
        case 0:
        case 1:
            // read payload
            d.state = 2;
            d.ws.async_read_some(
                d.mi, d.sb, std::move(*this));
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

} // detail
} // wsproto
} // beast

#endif
