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

#include <beast/asio/bind_handler.h>
#include <beast/asio/handler_alloc.h>
#include <beast/asio/append_buffers.h>
#include <beast/asio/static_streambuf.h>
#include <beast/wsproto/detail/frame.h>
#include <cassert>
#include <memory>

namespace beast {
namespace wsproto {

// write a frame
//
template<class Stream>
template<class Buffers, class Handler>
class socket<Stream>::write_op
{
    using alloc_type =
        asio::handler_alloc<char, Handler>;

    struct data
    {
        socket<Stream>& ws;
        Buffers bs;
        Handler h;
        detail::frame_header fh;
        asio::static_streambuf_n<14> fh_buf;
        asio::temp_buffer<Handler> tmp;
        int state = 0;

        template<class DeducedHandler, class DeducedBuffers>
        data(DeducedHandler&& h_, socket<Stream>& ws_,
            opcode::value op_, bool fin, DeducedBuffers&& bs_)
            : ws(ws_)
            , bs(std::forward<DeducedBuffers>(bs_))
            , h(std::forward<DeducedHandler>(h_))
            , tmp(h)
        {
            fh.op = op_;
            fh.fin = fin;
            fh.rsv1 = 0;
            fh.rsv2 = 0;
            fh.rsv3 = 0;
            fh.len = boost::asio::buffer_size(bs);
            if((fh.mask = (ws.role_ == role_type::client)))
                fh.key = ws.maskgen_();
            detail::write<
                asio::static_streambuf>(fh_buf, fh);
        }
    };

    std::shared_ptr<data> d_;

public:
    write_op(write_op&&) = default;
    write_op(write_op const&) = default;

    template<class DeducedHandler, class... Args>
    explicit
    write_op(DeducedHandler&& h, Args&&... args)
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

    void operator()(error_code ec,
        std::size_t bytes_transferred);

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
        return op->d_->state >= 4 ||
            boost_asio_handler_cont_helpers::
                is_continuation(op->d_->h);
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
socket<Stream>::write_op<
    Buffers, Handler>::operator()(error_code ec,
        std::size_t bytes_transferred)
{
    using namespace boost::asio;
    auto& d = *d_;
    while(! ec && d.state != 99)
    {
        switch(d.state)
        {
        case 0:
            if(d.ws.fail_)
            {
                // suspend
                d.state = 1;
                d.ws.rd_invoke_.template emplace<
                    write_op>(std::move(*this));
                return;
            }
            d.state = 3;
            break;

        // write during fail
        case 1:
            d.state = 2;
            d.ws.get_io_service().post(
                beast::asio::bind_handler(
                    std::move(*this), ec, 0));
            return;

        // cancel write
        case 2:
            // call handler
            d.state = 99;
            ec = boost::asio::error::operation_aborted;
            break;

        // send frame
        case 3:
            if(d.fh.mask)
            {
                detail::prepared_key_type key;
                detail::prepare_key(key, d.fh.key);
                d.tmp.alloc(d.fh.len);
                buffer_copy(d.tmp.buffers(), d.bs);
                detail::mask_inplace(d.tmp.buffers(), key);
                // send header and payload
                d.state = 4;
                d.ws.wr_active_ = true;
                boost::asio::async_write(d.ws.stream_,
                    beast::asio::append_buffers(
                        d.fh_buf.data(), d.tmp.buffers()),
                            std::move(*this));
                return;
            }
            // send header and payload
            d.state = 4;
            d.ws.wr_active_ = true;
            boost::asio::async_write(d.ws.stream_,
                beast::asio::append_buffers(
                    d.fh_buf.data(), d.bs), std::move(*this));
            return;

        // sent frame
        case 4:
            d.ws.wr_active_ = false;
            // call handler
            d.state = 99;
            break;

        }
    }
    d.ws.wr_invoke_.maybe_invoke();
    d.h(ec);
}

} // wsproto
} // beast

#endif
