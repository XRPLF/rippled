//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_WEBSOCKET_IMPL_CLOSE_OP_HPP
#define BEAST_WEBSOCKET_IMPL_CLOSE_OP_HPP

#include <beast/core/handler_alloc.hpp>
#include <beast/core/static_streambuf.hpp>
#include <memory>

namespace beast {
namespace websocket {

// send the close message and wait for the response
//
template<class NextLayer>
template<class Handler>
class stream<NextLayer>::close_op
{
    using alloc_type = handler_alloc<char, Handler>;

    using fb_type = detail::frame_streambuf;

    struct data : op
    {
        stream<NextLayer>& ws;
        close_reason cr;
        Handler h;
        fb_type fb;
        bool cont;
        int state = 0;

        template<class DeducedHandler>
        data(DeducedHandler&& h_, stream<NextLayer>& ws_,
                close_reason const& cr_)
            : ws(ws_)
            , cr(cr_)
            , h(std::forward<DeducedHandler>(h_))
            , cont(boost_asio_handler_cont_helpers::
                is_continuation(h))
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
    close_op(DeducedHandler&& h,
            stream<NextLayer>& ws, Args&&... args)
        : d_(std::allocate_shared<data>(alloc_type{h},
            std::forward<DeducedHandler>(h), ws,
                std::forward<Args>(args)...))
    {
        (*this)(error_code{}, false);
    }

    void operator()()
    {
        (*this)(error_code{});
    }

    void
    operator()(error_code ec, std::size_t);

    void
    operator()(error_code ec, bool again = true);

    friend
    void* asio_handler_allocate(
        std::size_t size, close_op* op)
    {
        return boost_asio_handler_alloc_helpers::
            allocate(size, op->d_->h);
    }

    friend
    void asio_handler_deallocate(
        void* p, std::size_t size, close_op* op)
    {
        return boost_asio_handler_alloc_helpers::
            deallocate(p, size, op->d_->h);
    }

    friend
    bool asio_handler_is_continuation(close_op* op)
    {
        return op->d_->cont;
    }

    template<class Function>
    friend
    void asio_handler_invoke(Function&& f, close_op* op)
    {
        return boost_asio_handler_invoke_helpers::
            invoke(f, op->d_->h);
    }
};

template<class NextLayer>
template<class Handler>
void 
stream<NextLayer>::close_op<Handler>::
operator()(error_code ec, std::size_t)
{
    auto& d = *d_;
    if(ec)
        d.ws.failed_ = true;
    (*this)(ec);
}

template<class NextLayer>
template<class Handler>
void 
stream<NextLayer>::close_op<Handler>::
operator()(error_code ec, bool again)
{
    auto& d = *d_;
    d.cont = d.cont || again;
    if(ec)
        goto upcall;
    for(;;)
    {
        switch(d.state)
        {
        case 0:
            if(d.ws.wr_block_)
            {
                // suspend
                d.state = 2;
                d.ws.wr_op_.template emplace<
                    close_op>(std::move(*this));
                return;
            }
            if(d.ws.failed_ || d.ws.wr_close_)
            {
                // call handler
                d.ws.get_io_service().post(
                    bind_handler(std::move(*this),
                        boost::asio::error::operation_aborted));
                return;
            }
            // fall through

        case 1:
            // send close frame
            d.state = 99;
            d.ws.wr_close_ = true;
            assert(! d.ws.wr_block_);
            d.ws.wr_block_ = &d;
            boost::asio::async_write(d.ws.stream_,
                d.fb.data(), std::move(*this));
            return;

        case 2:
            d.state = 3;
            d.ws.get_io_service().post(
                bind_handler(std::move(*this), ec));
            return;

        case 3:
            if(d.ws.failed_ || d.ws.wr_close_)
            {
                // call handler
                ec = boost::asio::error::operation_aborted;
                goto upcall;
            }
            d.state = 1;
            break;

        case 99:
            goto upcall;
        }
    }
upcall:
    if(d.ws.wr_block_ == &d)
        d.ws.wr_block_ = nullptr;
    d.ws.rd_op_.maybe_invoke();
    d.h(ec);
}

} // websocket
} // beast

#endif
