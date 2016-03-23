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

#include <beast/wsproto/detail/handler_alloc.h>
#include <beast/asio/append_buffers.h>
#include <beast/asio/static_streambuf.h>

namespace beast {
namespace wsproto {

// write a frame
//
template<class Stream>
template<class Buffers, class Handler>
class socket<Stream>::write_op
{
    struct data
    {
        socket<Stream>& ws;
        Buffers bs;
        Handler h;
        frame_header fh;
        asio::static_streambuf_n<14> fh_buf;
        std::unique_ptr<std::uint8_t[]> p;
        int state = 0;

        template<class DeducedHandler, class DeducedBuffers>
        data(DeducedHandler&& h_, socket<Stream>& ws_,
            opcode::value op_, bool fin, DeducedBuffers&& bs_)
            : ws(ws_)
            , bs(std::forward<DeducedBuffers>(bs_))
            , h(std::forward<DeducedHandler>(h_))
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
        : d_(std::allocate_shared<data>(
            detail::handler_alloc<int, Handler>{h},
                std::forward<DeducedHandler>(h),
                    std::forward<Args>(args)...))
    {
    }

    void operator()()
    {
        using namespace boost::asio;
#if 0
        auto& d = *d_;
        if(d.state == 0 && d.ws.closing_)
        {
            // suspend
            d.state = 1;
            d.ws.rd_invoke_.template emplace<
                write_op>(std::move(*this));
            return;
        }
#endif
        (*this)(error_code{}, 0);
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
            // send frame
            case 0:
                if(d.fh.mask)
                {
                    detail::prepared_key_type key;
                    detail::prepare_key(key, d.fh.key);
                    d.p.reset(new std::uint8_t[d.fh.len]);
                    mutable_buffers_1 mb{d.p.get(), d.fh.len};
                    buffer_copy(mb, d.bs);
                    detail::mask_inplace(mb, key);
                    // send header and payload
                    d.state = 1;
                    d.ws.wr_active_ = true;
                    boost::asio::async_write(d.ws.stream_,
                        beast::asio::append_buffers(
                            d.fh_buf.data(), mb), std::move(*this));
                    return;
                }
                // send header and payload
                d.state = 2;
                d.ws.wr_active_ = true;
                boost::asio::async_write(d.ws.stream_,
                    beast::asio::append_buffers(
                        d.fh_buf.data(), d.bs), std::move(*this));
                return;

            // cancel (closing)
            case 1:
                // call handler
                ec = boost::asio::error::operation_aborted;
                break;

            // sent frame
            case 2:
                d.ws.wr_active_ = false;
                // call handler
                d.state = 99;
                break;

            }
        }
        d.ws.wr_invoke_.maybe_invoke();
        d.h(ec);
    }

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
        return op->d_->state >= 2 ||
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

} // wsproto
} // beast

#endif
