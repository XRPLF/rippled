//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_WEBSOCKET_IMPL_READ_FRAME_OP_HPP
#define BEAST_WEBSOCKET_IMPL_READ_FRAME_OP_HPP

#include <beast/websocket/teardown.hpp>
#include <beast/core/handler_alloc.hpp>
#include <beast/core/prepare_buffers.hpp>
#include <beast/core/static_streambuf.hpp>
#include <boost/optional.hpp>
#include <cassert>
#include <memory>

namespace beast {
namespace websocket {

// Reads a single message frame,
// processes any received control frames.
//
template<class NextLayer>
template<class DynamicBuffer, class Handler>
class stream<NextLayer>::read_frame_op
{
    using alloc_type =
        handler_alloc<char, Handler>;

    using fb_type =
        detail::frame_streambuf;

    using fmb_type =
        typename fb_type::mutable_buffers_type;

    using dmb_type =
        typename DynamicBuffer::mutable_buffers_type;

    struct data : op
    {
        stream<NextLayer>& ws;
        frame_info& fi;
        DynamicBuffer& db;
        Handler h;
        fb_type fb;
        boost::optional<dmb_type> dmb;
        boost::optional<fmb_type> fmb;
        bool cont;
        int state = 0;

        template<class DeducedHandler>
        data(DeducedHandler&& h_, stream<NextLayer>& ws_,
                frame_info& fi_, DynamicBuffer& sb_)
            : ws(ws_)
            , fi(fi_)
            , db(sb_)
            , h(std::forward<DeducedHandler>(h_))
            , cont(boost_asio_handler_cont_helpers::
                is_continuation(h))
        {
        }
    };

    std::shared_ptr<data> d_;

public:
    read_frame_op(read_frame_op&&) = default;
    read_frame_op(read_frame_op const&) = default;

    template<class DeducedHandler, class... Args>
    read_frame_op(DeducedHandler&& h,
            stream<NextLayer>& ws, Args&&... args)
        : d_(std::allocate_shared<data>(alloc_type{h},
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
        return boost_asio_handler_alloc_helpers::
            allocate(size, op->d_->h);
    }

    friend
    void asio_handler_deallocate(
        void* p, std::size_t size, read_frame_op* op)
    {
        return boost_asio_handler_alloc_helpers::
            deallocate(p, size, op->d_->h);
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
        return boost_asio_handler_invoke_helpers::
            invoke(f, op->d_->h);
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
                d.dmb = d.db.prepare(
                    detail::clamp(d.ws.rd_need_));
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
                auto const n = detail::read_fh1(
                    d.ws.rd_fh_, d.fb, d.ws.role_, code);
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
                detail::read_fh2(d.ws.rd_fh_,
                    d.fb, d.ws.role_, code);
                if(code == close_code::none)
                    d.ws.prepare_fh(code);
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
                        assert(d.ws.wr_block_ != &d);
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
                assert(d.ws.rd_fh_.op == opcode::close);
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
                    // call handler;
                    ec = error::closed;
                    goto upcall;
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
                assert(! d.ws.wr_block_);
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
                d.state = do_close + 1;
                d.ws.wr_close_ = true;
                assert(! d.ws.wr_block_);
                d.ws.wr_block_ = &d;
                boost::asio::async_write(d.ws.stream_,
                    d.fb.data(), std::move(*this));
                return;

            case do_close + 1:
                d.state = do_close + 2;
                websocket_helpers::call_async_teardown(
                    d.ws.next_layer(), std::move(*this));
                return;

            case do_close + 2:
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
                assert(! d.ws.wr_block_);
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
    d.h(ec);
}

} // websocket
} // beast

#endif
