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

#ifndef BEAST_WSPROTO_READ_MSG_OP_H_INCLUDED
#define BEAST_WSPROTO_READ_MSG_OP_H_INCLUDED

namespace beast {
namespace wsproto {
namespace detail {

// read an entire message
//
template<class Stream, class Streambuf, class Handler>
class read_msg_op
{
    struct data
    {
        socket<Stream>& ws;
        opcode::value& op;
        Streambuf& sb;
        Handler h;
        frame_header fh;
        int state = 0;

        template<class DeducedHandler>
        data(socket<Stream>& ws_, opcode::value& op_,
                Streambuf& sb_, DeducedHandler&& h_)
            : ws(ws_)
            , op(op_)
            , sb(sb_)
            , h(std::forward<DeducedHandler>(h_))
        {
        }
    };

    std::shared_ptr<data> d_;

public:
    read_msg_op(read_msg_op&&) = default;
    read_msg_op(read_msg_op const&) = default;

    template<class... Args>
    explicit
    read_msg_op(Args&&... args)
        : d_(std::make_shared<data>(
            std::forward<Args>(args)...))
    {
    }

    void operator()()
    {
        (*this)(error_code{}, 0);
    }

    void operator()(error_code ec)
    {
        auto& d = *d_;
        if(! ec)
        {
            // got frame header
            if(d.fh.op != opcode::cont)
                d.op = d.fh.op;
            if(d.fh.len > 0)
            {
                d_->state = 1;
                // read payload
                d.ws.async_read(
                    d.sb.prepare(d.fh.len),
                        std::move(*this));
                return;
            }
            d_->state = 2;
            (*this)(error_code{}, 0);
            return;
        }
        d.h(ec);
    }

    void operator()(error_code ec,
        std::size_t bytes_transferred)
    {
        using namespace boost::asio;
        auto& d = *d_;
        for(;! ec && d.state != 99;)
        {
            switch(d.state)
            {
            case 0:
                // read frame header
                d.ws.async_read(
                    d.fh, std::move(*this));
                return;

            // got payload
            case 1:
                d.state = 2;
                d.sb.commit(bytes_transferred);
                break;

            // check fin
            case 2:
                if(d.fh.fin)
                {
                    // call handler
                    d.state = 99;
                    break;
                }
                d.state = 0;
                break;
            }
        }
        d.h(ec);
    }

    friend
    auto asio_handler_allocate(
        std::size_t size, read_msg_op* op)
    {
        return boost_asio_handler_alloc_helpers::
            allocate(size, op->d_->h);
    }

    friend
    auto asio_handler_deallocate(
        void* p, std::size_t size, read_msg_op* op)
    {
        return boost_asio_handler_alloc_helpers::
            deallocate(p, size, op->d_->h);
    }

    friend
    auto asio_handler_is_continuation(read_msg_op* op)
    {
        return op->d_->state >= 1 ||
            boost_asio_handler_cont_helpers::
                is_continuation(op->d_->h);
    }

    template <class Function>
    friend
    auto asio_handler_invoke(Function&& f, read_msg_op* op)
    {
        return boost_asio_handler_invoke_helpers::
            invoke(f, op->d_->h);
    }
};

} // detail
} // wsproto
} // beast

#endif
