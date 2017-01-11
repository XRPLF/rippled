//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_WEBSOCKET_IMPL_READ_IPP
#define BEAST_WEBSOCKET_IMPL_READ_IPP

#include <beast/websocket/teardown.hpp>
#include <beast/core/buffer_concepts.hpp>
#include <beast/core/handler_helpers.hpp>
#include <beast/core/handler_ptr.hpp>
#include <beast/core/prepare_buffers.hpp>
#include <beast/core/static_streambuf.hpp>
#include <beast/core/stream_concepts.hpp>
#include <beast/core/detail/clamp.hpp>
#include <boost/assert.hpp>
#include <boost/optional.hpp>
#include <memory>

namespace beast {
namespace websocket {

//------------------------------------------------------------------------------

// Reads a single message frame,
// processes any received control frames.
//
template<class NextLayer>
template<class DynamicBuffer, class Handler>
class stream<NextLayer>::read_frame_op
{
    using fb_type =
        detail::frame_streambuf;

    using fmb_type =
        typename fb_type::mutable_buffers_type;

    using dmb_type =
        typename DynamicBuffer::mutable_buffers_type;

    struct data : op
    {
        bool cont;
        stream<NextLayer>& ws;
        frame_info& fi;
        DynamicBuffer& db;
        fb_type fb;
        boost::optional<dmb_type> dmb;
        boost::optional<fmb_type> fmb;
        int state = 0;

        data(Handler& handler, stream<NextLayer>& ws_,
                frame_info& fi_, DynamicBuffer& sb_)
            : cont(beast_asio_helpers::
                is_continuation(handler))
            , ws(ws_)
            , fi(fi_)
            , db(sb_)
        {
        }
    };

    handler_ptr<data, Handler> d_;

public:
    read_frame_op(read_frame_op&&) = default;
    read_frame_op(read_frame_op const&) = default;

    template<class DeducedHandler, class... Args>
    read_frame_op(DeducedHandler&& h,
            stream<NextLayer>& ws, Args&&... args)
        : d_(make_handler_ptr<data, Handler>(
            std::forward<DeducedHandler>(h), ws,
                std::forward<Args>(args)...))
    {
        (*this)(error_code{}, 0, false);
    }

    void operator()()
    {
        (*this)(error_code{}, 0, true);
    }

    void operator()(error_code const& ec)
    {
        (*this)(ec, 0, true);
    }

    void operator()(error_code ec,
        std::size_t bytes_transferred);

    void operator()(error_code ec,
        std::size_t bytes_transferred, bool again);

    friend
    void* asio_handler_allocate(
        std::size_t size, read_frame_op* op)
    {
        return beast_asio_helpers::
            allocate(size, op->d_.handler());
    }

    friend
    void asio_handler_deallocate(
        void* p, std::size_t size, read_frame_op* op)
    {
        return beast_asio_helpers::
            deallocate(p, size, op->d_.handler());
    }

    friend
    bool asio_handler_is_continuation(read_frame_op* op)
    {
        return op->d_->cont;
    }

    template<class Function>
    friend
    void asio_handler_invoke(Function&& f, read_frame_op* op)
    {
        return beast_asio_helpers::
            invoke(f, op->d_.handler());
    }
};

template<class NextLayer>
template<class DynamicBuffer, class Handler>
void
stream<NextLayer>::read_frame_op<DynamicBuffer, Handler>::
operator()(error_code ec, std::size_t bytes_transferred)
{
    auto& d = *d_;
    if(ec)
        d.ws.failed_ = true;
    (*this)(ec, bytes_transferred, true);
}

template<class NextLayer>
template<class DynamicBuffer, class Handler>
void
stream<NextLayer>::read_frame_op<DynamicBuffer, Handler>::
operator()(error_code ec,std::size_t bytes_transferred, bool again)
{
    using beast::detail::clamp;
    enum
    {
        do_start = 0,
        do_read_payload = 1,
        do_frame_done = 3,
        do_read_fh = 4,
        do_control_payload = 7,
        do_control = 8,
        do_pong_resume = 9,
        do_pong = 11,
        do_close_resume = 13,
        do_close = 15,
        do_teardown = 16,
        do_fail = 18,

        do_call_handler = 99
    };

    auto& d = *d_;
    if(! ec)
    {
        d.cont = d.cont || again;
        close_code::value code = close_code::none;
        do
        {
            switch(d.state)
            {
            case do_start:
                if(d.ws.failed_)
                {
                    d.state = do_call_handler;
                    d.ws.get_io_service().post(
                        bind_handler(std::move(*this),
                            boost::asio::error::operation_aborted, 0));
                    return;
                }
                d.state =  d.ws.rd_need_ > 0 ?
                    do_read_payload : do_read_fh;
                break;

            //------------------------------------------------------------------

            case do_read_payload:
                d.state = do_read_payload + 1;
                d.dmb = d.db.prepare(clamp(d.ws.rd_need_));
                // receive payload data
                d.ws.stream_.async_read_some(
                    *d.dmb, std::move(*this));
                return;

            case do_read_payload + 1:
            {
                d.ws.rd_need_ -= bytes_transferred;
                auto const pb = prepare_buffers(
                    bytes_transferred, *d.dmb);
                if(d.ws.rd_fh_.mask)
                    detail::mask_inplace(pb, d.ws.rd_key_);
                if(d.ws.rd_opcode_ == opcode::text)
                {
                    if(! d.ws.rd_utf8_check_.write(pb) ||
                        (d.ws.rd_need_ == 0 && d.ws.rd_fh_.fin &&
                            ! d.ws.rd_utf8_check_.finish()))
                    {
                        // invalid utf8
                        code = close_code::bad_payload;
                        d.state = do_fail;
                        break;
                    }
                }
                d.db.commit(bytes_transferred);
                if(d.ws.rd_need_ > 0)
                {
                    d.state = do_read_payload;
                    break;
                }
                // fall through
            }

            //------------------------------------------------------------------

            case do_frame_done:
                // call handler
                d.fi.op = d.ws.rd_opcode_;
                d.fi.fin = d.ws.rd_fh_.fin &&
                    d.ws.rd_need_ == 0;
                goto upcall;

            //------------------------------------------------------------------

            case do_read_fh:
                d.state = do_read_fh + 1;
                boost::asio::async_read(d.ws.stream_,
                    d.fb.prepare(2), std::move(*this));
                return;

            case do_read_fh + 1:
            {
                d.fb.commit(bytes_transferred);
                code = close_code::none;
                auto const n = d.ws.read_fh1(d.fb, code);
                if(code != close_code::none)
                {
                    // protocol error
                    d.state = do_fail;
                    break;
                }
                d.state = do_read_fh + 2;
                if(n == 0)
                {
                    bytes_transferred = 0;
                    break;
                }
                // read variable header
                boost::asio::async_read(d.ws.stream_,
                    d.fb.prepare(n), std::move(*this));
                return;
            }

            case do_read_fh + 2:
                d.fb.commit(bytes_transferred);
                code = close_code::none;
                d.ws.read_fh2(d.fb, code);
                if(code != close_code::none)
                {
                    // protocol error
                    d.state = do_fail;
                    break;
                }
                if(detail::is_control(d.ws.rd_fh_.op))
                {
                    if(d.ws.rd_fh_.len > 0)
                    {
                        // read control payload
                        d.state = do_control_payload;
                        d.fmb = d.fb.prepare(static_cast<
                            std::size_t>(d.ws.rd_fh_.len));
                        boost::asio::async_read(d.ws.stream_,
                            *d.fmb, std::move(*this));
                        return;
                    }
                    d.state = do_control;
                    break;
                }
                if(d.ws.rd_need_ > 0)
                {
                    d.state = do_read_payload;
                    break;
                }
                // empty frame
                d.state = do_frame_done;
                break;

            //------------------------------------------------------------------

            case do_control_payload:
                if(d.ws.rd_fh_.mask)
                    detail::mask_inplace(
                        *d.fmb, d.ws.rd_key_);
                d.fb.commit(bytes_transferred);
                d.state = do_control; // VFALCO fall through?
                break;

            //------------------------------------------------------------------

            case do_control:
                if(d.ws.rd_fh_.op == opcode::ping)
                {
                    ping_data data;
                    detail::read(data, d.fb.data());
                    d.fb.reset();
                    if(d.ws.wr_close_)
                    {
                        // ignore ping when closing
                        d.state = do_read_fh;
                        break;
                    }
                    d.ws.template write_ping<static_streambuf>(
                        d.fb, opcode::pong, data);
                    if(d.ws.wr_block_)
                    {
                        // suspend
                        d.state = do_pong_resume;
                        BOOST_ASSERT(d.ws.wr_block_ != &d);
                        d.ws.rd_op_.template emplace<
                            read_frame_op>(std::move(*this));
                        return;
                    }
                    d.state = do_pong;
                    break;
                }
                else if(d.ws.rd_fh_.op == opcode::pong)
                {
                    code = close_code::none;
                    ping_data payload;
                    detail::read(payload, d.fb.data());
                    if(d.ws.pong_cb_)
                        d.ws.pong_cb_(payload);
                    d.fb.reset();
                    d.state = do_read_fh;
                    break;
                }
                BOOST_ASSERT(d.ws.rd_fh_.op == opcode::close);
                {
                    detail::read(d.ws.cr_, d.fb.data(), code);
                    if(code != close_code::none)
                    {
                        // protocol error
                        d.state = do_fail;
                        break;
                    }
                    if(! d.ws.wr_close_)
                    {
                        auto cr = d.ws.cr_;
                        if(cr.code == close_code::none)
                            cr.code = close_code::normal;
                        cr.reason = "";
                        d.fb.reset();
                        d.ws.template write_close<
                            static_streambuf>(d.fb, cr);
                        if(d.ws.wr_block_)
                        {
                            // suspend
                            d.state = do_close_resume;
                            d.ws.rd_op_.template emplace<
                                read_frame_op>(std::move(*this));
                            return;
                        }
                        d.state = do_close;
                        break;
                    }
                    d.state = do_teardown;
                    break;
                }

            //------------------------------------------------------------------

            case do_pong_resume:
                d.state = do_pong_resume + 1;
                d.ws.get_io_service().post(bind_handler(
                    std::move(*this), ec, bytes_transferred));
                return;

            case do_pong_resume + 1:
                if(d.ws.failed_)
                {
                    // call handler
                    ec = boost::asio::error::operation_aborted;
                    goto upcall;
                }
                d.state = do_pong;
                break; // VFALCO fall through?

            //------------------------------------------------------------------

            case do_pong:
                if(d.ws.wr_close_)
                {
                    // ignore ping when closing
                    d.fb.reset();
                    d.state = do_read_fh;
                    break;
                }
                // send pong
                d.state = do_pong + 1;
                BOOST_ASSERT(! d.ws.wr_block_);
                d.ws.wr_block_ = &d;
                boost::asio::async_write(d.ws.stream_,
                    d.fb.data(), std::move(*this));
                return;

            case do_pong + 1:
                d.fb.reset();
                d.state = do_read_fh;
                d.ws.wr_block_ = nullptr;
                break;

            //------------------------------------------------------------------

            case do_close_resume:
                d.state = do_close_resume + 1;
                d.ws.get_io_service().post(bind_handler(
                    std::move(*this), ec, bytes_transferred));
                return;

            case do_close_resume + 1:
                if(d.ws.failed_)
                {
                    // call handler
                    d.state = do_call_handler;
                    ec = boost::asio::error::operation_aborted;
                    break;
                }
                if(d.ws.wr_close_)
                {
                    // call handler
                    ec = error::closed;
                    goto upcall;
                }
                d.state = do_close;
                break;

            //------------------------------------------------------------------

            case do_close:
                d.state = do_teardown;
                d.ws.wr_close_ = true;
                BOOST_ASSERT(! d.ws.wr_block_);
                d.ws.wr_block_ = &d;
                boost::asio::async_write(d.ws.stream_,
                    d.fb.data(), std::move(*this));
                return;

            //------------------------------------------------------------------

            case do_teardown:
                d.state = do_teardown + 1;
                websocket_helpers::call_async_teardown(
                    d.ws.next_layer(), std::move(*this));
                return;

            case do_teardown + 1:
                // call handler
                ec = error::closed;
                goto upcall;

            //------------------------------------------------------------------

            case do_fail:
                if(d.ws.wr_close_)
                {
                    d.state = do_fail + 4;
                    break;
                }
                d.fb.reset();
                d.ws.template write_close<
                    static_streambuf>(d.fb, code);
                if(d.ws.wr_block_)
                {
                    // suspend
                    d.state = do_fail + 2;
                    d.ws.rd_op_.template emplace<
                        read_frame_op>(std::move(*this));
                    return;
                }
                // fall through

            case do_fail + 1:
                d.ws.failed_ = true;
                // send close frame
                d.state = do_fail + 4;
                d.ws.wr_close_ = true;
                BOOST_ASSERT(! d.ws.wr_block_);
                d.ws.wr_block_ = &d;
                boost::asio::async_write(d.ws.stream_,
                    d.fb.data(), std::move(*this));
                return;

            case do_fail + 2:
                d.state = do_fail + 3;
                d.ws.get_io_service().post(bind_handler(
                    std::move(*this), ec, bytes_transferred));
                return;

            case do_fail + 3:
                if(d.ws.failed_)
                {
                    d.state = do_fail + 5;
                    break;
                }
                d.state = do_fail + 1;
                break;

            case do_fail + 4:
                d.state = do_fail + 5;
                websocket_helpers::call_async_teardown(
                    d.ws.next_layer(), std::move(*this));
                return;

            case do_fail + 5:
                // call handler
                ec = error::failed;
                goto upcall;

            //------------------------------------------------------------------

            case do_call_handler:
                goto upcall;
            }
        }
        while(! ec);
    }
upcall:
    if(d.ws.wr_block_ == &d)
        d.ws.wr_block_ = nullptr;
    d.ws.wr_op_.maybe_invoke();
    d_.invoke(ec);
}

template<class NextLayer>
template<class DynamicBuffer, class ReadHandler>
typename async_completion<
    ReadHandler, void(error_code)>::result_type
stream<NextLayer>::
async_read_frame(frame_info& fi,
    DynamicBuffer& dynabuf, ReadHandler&& handler)
{
    static_assert(is_AsyncStream<next_layer_type>::value,
        "AsyncStream requirements requirements not met");
    static_assert(beast::is_DynamicBuffer<DynamicBuffer>::value,
        "DynamicBuffer requirements not met");
    beast::async_completion<
        ReadHandler, void(error_code)> completion(handler);
    read_frame_op<DynamicBuffer, decltype(completion.handler)>{
        completion.handler, *this, fi, dynabuf};
    return completion.result.get();
}

template<class NextLayer>
template<class DynamicBuffer>
void
stream<NextLayer>::
read_frame(frame_info& fi, DynamicBuffer& dynabuf)
{
    static_assert(is_SyncStream<next_layer_type>::value,
        "SyncStream requirements not met");
    static_assert(beast::is_DynamicBuffer<DynamicBuffer>::value,
        "DynamicBuffer requirements not met");
    error_code ec;
    read_frame(fi, dynabuf, ec);
    if(ec)
        throw system_error{ec};
}

template<class NextLayer>
template<class DynamicBuffer>
void
stream<NextLayer>::
read_frame(frame_info& fi, DynamicBuffer& dynabuf, error_code& ec)
{
    static_assert(is_SyncStream<next_layer_type>::value,
        "SyncStream requirements not met");
    static_assert(beast::is_DynamicBuffer<DynamicBuffer>::value,
        "DynamicBuffer requirements not met");
    using beast::detail::clamp;
    close_code::value code{};
    for(;;)
    {
        if(rd_need_ == 0)
        {
            // read header
            detail::frame_streambuf fb;
            do_read_fh(fb, code, ec);
            failed_ = ec != 0;
            if(failed_)
                return;
            if(code != close_code::none)
                break;
            if(detail::is_control(rd_fh_.op))
            {
                // read control payload
                if(rd_fh_.len > 0)
                {
                    auto const mb = fb.prepare(
                        static_cast<std::size_t>(rd_fh_.len));
                    fb.commit(boost::asio::read(stream_, mb, ec));
                    failed_ = ec != 0;
                    if(failed_)
                        return;
                    if(rd_fh_.mask)
                        detail::mask_inplace(mb, rd_key_);
                    fb.commit(static_cast<std::size_t>(rd_fh_.len));
                }
                if(rd_fh_.op == opcode::ping)
                {
                    ping_data data;
                    detail::read(data, fb.data());
                    fb.reset();
                    write_ping<static_streambuf>(
                        fb, opcode::pong, data);
                    boost::asio::write(stream_, fb.data(), ec);
                    failed_ = ec != 0;
                    if(failed_)
                        return;
                    continue;
                }
                else if(rd_fh_.op == opcode::pong)
                {
                    ping_data payload;
                    detail::read(payload, fb.data());
                    if(pong_cb_)
                        pong_cb_(payload);
                    continue;
                }
                BOOST_ASSERT(rd_fh_.op == opcode::close);
                {
                    detail::read(cr_, fb.data(), code);
                    if(code != close_code::none)
                        break;
                    if(! wr_close_)
                    {
                        auto cr = cr_;
                        if(cr.code == close_code::none)
                            cr.code = close_code::normal;
                        cr.reason = "";
                        fb.reset();
                        wr_close_ = true;
                        write_close<static_streambuf>(fb, cr);
                        boost::asio::write(stream_, fb.data(), ec);
                        failed_ = ec != 0;
                        if(failed_)
                            return;
                    }
                    break;
                }
            }
            if(rd_need_ == 0 && ! rd_fh_.fin)
            {
                // empty frame
                continue;
            }
        }
        // read payload
        auto smb = dynabuf.prepare(clamp(rd_need_));
        auto const bytes_transferred =
            stream_.read_some(smb, ec);
        failed_ = ec != 0;
        if(failed_)
            return;
        rd_need_ -= bytes_transferred;
        auto const pb = prepare_buffers(
            bytes_transferred, smb);
        if(rd_fh_.mask)
            detail::mask_inplace(pb, rd_key_);
        if(rd_opcode_ == opcode::text)
        {
            if(! rd_utf8_check_.write(pb) ||
                (rd_need_ == 0 && rd_fh_.fin &&
                    ! rd_utf8_check_.finish()))
            {
                code = close_code::bad_payload;
                break;
            }
        }
        dynabuf.commit(bytes_transferred);
        fi.op = rd_opcode_;
        fi.fin = rd_fh_.fin && rd_need_ == 0;
        return;
    }
    if(code != close_code::none)
    {
        // Fail the connection (per rfc6455)
        if(! wr_close_)
        {
            wr_close_ = true;
            detail::frame_streambuf fb;
            write_close<static_streambuf>(fb, code);
            boost::asio::write(stream_, fb.data(), ec);
            failed_ = ec != 0;
            if(failed_)
                return;
        }
        websocket_helpers::call_teardown(next_layer(), ec);
        failed_ = ec != 0;
        if(failed_)
            return;
        ec = error::failed;
        failed_ = true;
        return;
    }
    if(! ec)
        websocket_helpers::call_teardown(next_layer(), ec);
    if(! ec)
        ec = error::closed;
    failed_ = ec != 0;
}

//------------------------------------------------------------------------------

// read an entire message
//
template<class NextLayer>
template<class DynamicBuffer, class Handler>
class stream<NextLayer>::read_op
{
    struct data
    {
        bool cont;
        stream<NextLayer>& ws;
        opcode& op;
        DynamicBuffer& db;
        frame_info fi;
        int state = 0;

        data(Handler& handler,
            stream<NextLayer>& ws_, opcode& op_,
                DynamicBuffer& sb_)
            : cont(beast_asio_helpers::
                is_continuation(handler))
            , ws(ws_)
            , op(op_)
            , db(sb_)
        {
        }
    };

    handler_ptr<data, Handler> d_;

public:
    read_op(read_op&&) = default;
    read_op(read_op const&) = default;

    template<class DeducedHandler, class... Args>
    read_op(DeducedHandler&& h,
            stream<NextLayer>& ws, Args&&... args)
        : d_(make_handler_ptr<data, Handler>(
            std::forward<DeducedHandler>(h), ws,
                std::forward<Args>(args)...))
    {
        (*this)(error_code{}, false);
    }

    void operator()(
        error_code const& ec, bool again = true);

    friend
    void* asio_handler_allocate(
        std::size_t size, read_op* op)
    {
        return beast_asio_helpers::
            allocate(size, op->d_.handler());
    }

    friend
    void asio_handler_deallocate(
        void* p, std::size_t size, read_op* op)
    {
        return beast_asio_helpers::
            deallocate(p, size, op->d_.handler());
    }

    friend
    bool asio_handler_is_continuation(read_op* op)
    {
        return op->d_->cont;
    }

    template<class Function>
    friend
    void asio_handler_invoke(Function&& f, read_op* op)
    {
        return beast_asio_helpers::
            invoke(f, op->d_.handler());
    }
};

template<class NextLayer>
template<class DynamicBuffer, class Handler>
void
stream<NextLayer>::read_op<DynamicBuffer, Handler>::
operator()(error_code const& ec, bool again)
{
    auto& d = *d_;
    d.cont = d.cont || again;
    while(! ec)
    {
        switch(d.state)
        {
        case 0:
            // read payload
            d.state = 1;
#if 0
            // VFALCO This causes dereference of null, because
            //        the handler is moved from the data block
            //        before asio_handler_deallocate is called.
            d.ws.async_read_frame(
                d.fi, d.db, std::move(*this));
#else
            d.ws.async_read_frame(d.fi, d.db, *this);
#endif
            return;

        // got payload
        case 1:
            d.op = d.fi.op;
            if(d.fi.fin)
                goto upcall;
            d.state = 0;
            break;
        }
    }
upcall:
    d_.invoke(ec);
}

template<class NextLayer>
template<class DynamicBuffer, class ReadHandler>
typename async_completion<
    ReadHandler, void(error_code)>::result_type
stream<NextLayer>::
async_read(opcode& op,
    DynamicBuffer& dynabuf, ReadHandler&& handler)
{
    static_assert(is_AsyncStream<next_layer_type>::value,
        "AsyncStream requirements requirements not met");
    static_assert(beast::is_DynamicBuffer<DynamicBuffer>::value,
        "DynamicBuffer requirements not met");
    beast::async_completion<
        ReadHandler, void(error_code)
            > completion(handler);
    read_op<DynamicBuffer, decltype(completion.handler)>{
        completion.handler, *this, op, dynabuf};
    return completion.result.get();
}

template<class NextLayer>
template<class DynamicBuffer>
void
stream<NextLayer>::
read(opcode& op, DynamicBuffer& dynabuf)
{
    static_assert(is_SyncStream<next_layer_type>::value,
        "SyncStream requirements not met");
    static_assert(beast::is_DynamicBuffer<DynamicBuffer>::value,
        "DynamicBuffer requirements not met");
    error_code ec;
    read(op, dynabuf, ec);
    if(ec)
        throw system_error{ec};
}

template<class NextLayer>
template<class DynamicBuffer>
void
stream<NextLayer>::
read(opcode& op, DynamicBuffer& dynabuf, error_code& ec)
{
    static_assert(is_SyncStream<next_layer_type>::value,
        "SyncStream requirements not met");
    static_assert(beast::is_DynamicBuffer<DynamicBuffer>::value,
        "DynamicBuffer requirements not met");
    frame_info fi;
    for(;;)
    {
        read_frame(fi, dynabuf, ec);
        if(ec)
            break;
        op = fi.op;
        if(fi.fin)
            break;
    }
}

//------------------------------------------------------------------------------

} // websocket
} // beast

#endif
