//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_WEBSOCKET_IMPL_CLOSE_IPP
#define BEAST_WEBSOCKET_IMPL_CLOSE_IPP

#include <beast/core/handler_ptr.hpp>
#include <beast/core/static_buffer.hpp>
#include <beast/core/type_traits.hpp>
#include <beast/core/detail/config.hpp>
#include <boost/asio/handler_alloc_hook.hpp>
#include <boost/asio/handler_continuation_hook.hpp>
#include <boost/asio/handler_invoke_hook.hpp>
#include <boost/throw_exception.hpp>
#include <memory>

namespace beast {
namespace websocket {

//------------------------------------------------------------------------------

// send the close message and wait for the response
//
template<class NextLayer>
template<class Handler>
class stream<NextLayer>::close_op
{
    struct data : op
    {
        stream<NextLayer>& ws;
        close_reason cr;
        detail::frame_streambuf fb;
        int state = 0;

        data(Handler&, stream<NextLayer>& ws_,
                close_reason const& cr_)
            : ws(ws_)
            , cr(cr_)
        {
            ws.template write_close<
                static_buffer>(fb, cr);
        }
    };

    handler_ptr<data, Handler> d_;

public:
    close_op(close_op&&) = default;
    close_op(close_op const&) = default;

    template<class DeducedHandler, class... Args>
    close_op(DeducedHandler&& h,
            stream<NextLayer>& ws, Args&&... args)
        : d_(std::forward<DeducedHandler>(h),
            ws, std::forward<Args>(args)...)
    {
    }

    void operator()()
    {
        (*this)({});
    }

    void
    operator()(error_code ec,
        std::size_t bytes_transferred = 0);

    friend
    void* asio_handler_allocate(
        std::size_t size, close_op* op)
    {
        using boost::asio::asio_handler_allocate;
        return asio_handler_allocate(
            size, std::addressof(op->d_.handler()));
    }

    friend
    void asio_handler_deallocate(
        void* p, std::size_t size, close_op* op)
    {
        using boost::asio::asio_handler_deallocate;
        asio_handler_deallocate(
            p, size, std::addressof(op->d_.handler()));
    }

    friend
    bool asio_handler_is_continuation(close_op* op)
    {
        using boost::asio::asio_handler_is_continuation;
        return asio_handler_is_continuation(
            std::addressof(op->d_.handler()));
    }

    template<class Function>
    friend
    void asio_handler_invoke(Function&& f, close_op* op)
    {
        using boost::asio::asio_handler_invoke;
        asio_handler_invoke(
            f, std::addressof(op->d_.handler()));
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
    {
        BOOST_ASSERT(d.ws.wr_block_ == &d);
        d.ws.failed_ = true;
        goto upcall;
    }
    switch(d.state)
    {
    case 0:
        if(d.ws.wr_block_)
        {
            // suspend
            d.state = 1;
            d.ws.close_op_.emplace(std::move(*this));
            return;
        }
        d.ws.wr_block_ = &d;
        if(d.ws.failed_ || d.ws.wr_close_)
        {
            // call handler
            d.ws.get_io_service().post(
                bind_handler(std::move(*this),
                    boost::asio::error::operation_aborted));
            return;
        }

    do_write:
        // send close frame
        BOOST_ASSERT(d.ws.wr_block_ == &d);
        d.state = 3;
        d.ws.wr_close_ = true;
        boost::asio::async_write(d.ws.stream_,
            d.fb.data(), std::move(*this));
        return;

    case 1:
        BOOST_ASSERT(! d.ws.wr_block_);
        d.ws.wr_block_ = &d;
        d.state = 2;
        // The current context is safe but might not be
        // the same as the one for this operation (since
        // we are being called from a write operation).
        // Call post to make sure we are invoked the same
        // way as the final handler for this operation.
        d.ws.get_io_service().post(
            bind_handler(std::move(*this), ec));
        return;

    case 2:
        BOOST_ASSERT(d.ws.wr_block_ == &d);
        if(d.ws.failed_ || d.ws.wr_close_)
        {
            // call handler
            ec = boost::asio::error::operation_aborted;
            goto upcall;
        }
        goto do_write;

    case 3:
        break;
    }
upcall:
    BOOST_ASSERT(d.ws.wr_block_ == &d);
    d.ws.wr_block_ = nullptr;
    d.ws.rd_op_.maybe_invoke() ||
        d.ws.ping_op_.maybe_invoke() ||
        d.ws.wr_op_.maybe_invoke();
    d_.invoke(ec);
}

template<class NextLayer>
template<class CloseHandler>
async_return_type<
    CloseHandler, void(error_code)>
stream<NextLayer>::
async_close(close_reason const& cr, CloseHandler&& handler)
{
    static_assert(is_async_stream<next_layer_type>::value,
        "AsyncStream requirements not met");
    async_completion<CloseHandler,
        void(error_code)> init{handler};
    close_op<handler_type<
        CloseHandler, void(error_code)>>{
            init.completion_handler, *this, cr}({});
    return init.result.get();
}

template<class NextLayer>
void
stream<NextLayer>::
close(close_reason const& cr)
{
    static_assert(is_sync_stream<next_layer_type>::value,
        "SyncStream requirements not met");
    error_code ec;
    close(cr, ec);
    if(ec)
        BOOST_THROW_EXCEPTION(system_error{ec});
}

template<class NextLayer>
void
stream<NextLayer>::
close(close_reason const& cr, error_code& ec)
{
    static_assert(is_sync_stream<next_layer_type>::value,
        "SyncStream requirements not met");
    BOOST_ASSERT(! wr_close_);
    if(wr_close_)
    {
        ec = boost::asio::error::operation_aborted;
        return;
    }
    wr_close_ = true;
    detail::frame_streambuf fb;
    write_close<static_buffer>(fb, cr);
    boost::asio::write(stream_, fb.data(), ec);
    failed_ = !!ec;
}

//------------------------------------------------------------------------------

} // websocket
} // beast

#endif
