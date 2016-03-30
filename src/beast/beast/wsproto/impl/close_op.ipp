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

#ifndef BEAST_WSPROTO_CLOSE_OP_H_INCLUDED
#define BEAST_WSPROTO_CLOSE_OP_H_INCLUDED

#include <beast/asio/handler_alloc.h>
#include <beast/asio/static_streambuf.h>
#include <memory>

namespace beast {
namespace wsproto {

// send the close message and wait for the response
//
template<class Stream>
template<class Handler>
class socket<Stream>::close_op
{
    using alloc_type =
        handler_alloc<char, Handler>;
    using fb_type =
        detail::frame_streambuf;
    using fmb_type =
        typename fb_type::mutable_buffers_type;

    struct data : op
    {
        socket<Stream>& ws;
        close_reason cr;
        Handler h;
        fb_type fb;
        fmb_type fmb;
        temp_buffer<Handler> tmp;
        int state = 0;

        template<class DeducedHandler>
        data(DeducedHandler&& h_, socket<Stream>& ws_,
                close_reason const& cr_)
            : ws(ws_)
            , cr(cr_)
            , h(std::forward<DeducedHandler>(h_))
            , tmp(h_)
        {
            ws.template write_close<
                static_streambuf>(fb, cr);
        }
    };

    std::shared_ptr<data> d_;

public:
    close_op(close_op&&) = default;
    close_op(close_op const&) = default;

    template<class DeducedHandler, class... Args>
    explicit
    close_op(DeducedHandler&& h, Args&&... args)
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

    void
    operator()(error_code ec,
        std::size_t bytes_transferred);

    friend
    auto asio_handler_allocate(
        std::size_t size, close_op* op)
    {
        return boost_asio_handler_alloc_helpers::
            allocate(size, op->d_->h);
    }

    friend
    auto asio_handler_deallocate(
        void* p, std::size_t size, close_op* op)
    {
        return boost_asio_handler_alloc_helpers::
            deallocate(p, size, op->d_->h);
    }

    friend
    auto asio_handler_is_continuation(close_op* op)
    {
        return op->d_->state >= 20 ||
            boost_asio_handler_cont_helpers::
                is_continuation(op->d_->h);
    }

    template <class Function>
    friend
    auto asio_handler_invoke(Function&& f, close_op* op)
    {
        return boost_asio_handler_invoke_helpers::
            invoke(f, op->d_->h);
    }
};

template<class Stream>
template<class Handler>
void 
socket<Stream>::close_op<Handler>::operator()(
    error_code ec, std::size_t bytes_transferred)
{
    auto& d = *d_;
    while(! ec && d.state != 999)
    {
        switch(d.state)
        {
        case 0:
            if(d.ws.wr_block_)
            {
                // suspend
                d.state = 20;
                d.ws.rd_op_.template emplace<
                    close_op>(std::move(*this));
                return;
            }
            if(d.ws.error_)
            {
                // call handler
                d.state = 999;
                d.ws.get_io_service().post(
                    bind_handler(std::move(*this),
                        boost::asio::error::operation_aborted, 0));
                return;
            }
            d.state = 10;
            break;

        // resume
        case 20:
            if(d.ws.error_)
            {
                // call handler
                d.state = 999;
                ec = boost::asio::error::operation_aborted;
                break;
            }
            d.state = 30;
            break;

        case 10:
        case 30:
            // send close
            d.state = 999;
            assert(! d.ws.wr_close_);
            d.ws.wr_close_ = true;
            assert(! d.ws.wr_block_);
            d.ws.wr_block_ = &d;
            boost::asio::async_write(d.ws.stream_,
                d.fb.data(), std::move(*this));
            return;
        }
    }
    if(ec)
        d.ws.error_ = true;
    if(d.ws.wr_block_ == &d)
        d.ws.wr_block_ = nullptr;
    d.h(ec);
    d.ws.rd_op_.maybe_invoke();
}

} // wsproto
} // beast

#endif
