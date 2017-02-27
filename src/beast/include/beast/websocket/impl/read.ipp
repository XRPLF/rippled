//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
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
#include <limits>
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
        std::uint64_t remain;
        detail::frame_header fh;
        detail::prepared_key key;
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
        : d_(std::forward<DeducedHandler>(h),
            ws, std::forward<Args>(args)...)
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
operator()(error_code ec,
    std::size_t bytes_transferred, bool again)
{
    using beast::detail::clamp;
    using boost::asio::buffer;
    enum
    {
        do_start = 0,
        do_read_payload = 1,
        do_inflate_payload = 30,
        do_frame_done = 4,
        do_read_fh = 5,
        do_control_payload = 8,
        do_control = 9,
        do_pong_resume = 10,
        do_pong = 12,
        do_close_resume = 14,
        do_close = 16,
        do_teardown = 17,
        do_fail = 19,

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
                d.state = do_read_fh;
                break;

            //------------------------------------------------------------------

            case do_read_payload:
                if(d.fh.len == 0)
                {
                    d.state = do_frame_done;
                    break;
                }
                // Enforce message size limit
                if(d.ws.rd_msg_max_ && d.fh.len >
                    d.ws.rd_msg_max_ - d.ws.rd_.size)
                {
                    code = close_code::too_big;
                    d.state = do_fail;
                    break;
                }
                d.ws.rd_.size += d.fh.len;
                d.remain = d.fh.len;
                if(d.fh.mask)
                    detail::prepare_key(d.key, d.fh.key);
                // fall through

            case do_read_payload + 1:
                d.state = do_read_payload + 2;
                d.dmb = d.db.prepare(clamp(d.remain));
                // Read frame payload data
                d.ws.stream_.async_read_some(
                    *d.dmb, std::move(*this));
                return;

            case do_read_payload + 2:
            {
                d.remain -= bytes_transferred;
                auto const pb = prepare_buffers(
                    bytes_transferred, *d.dmb);
                if(d.fh.mask)
                    detail::mask_inplace(pb, d.key);
                if(d.ws.rd_.op == opcode::text)
                {
                    if(! d.ws.rd_.utf8.write(pb) ||
                        (d.remain == 0 && d.fh.fin &&
                            ! d.ws.rd_.utf8.finish()))
                    {
                        // invalid utf8
                        code = close_code::bad_payload;
                        d.state = do_fail;
                        break;
                    }
                }
                d.db.commit(bytes_transferred);
                if(d.remain > 0)
                {
                    d.state = do_read_payload + 1;
                    break;
                }
                d.state = do_frame_done;
                break;
            }

            //------------------------------------------------------------------

            case do_inflate_payload:
                d.remain = d.fh.len;
                if(d.fh.len == 0)
                {
                    // inflate even if fh.len == 0, otherwise we
                    // never emit the end-of-stream deflate block.
                    bytes_transferred = 0;
                    d.state = do_inflate_payload + 2;
                    break;
                }
                if(d.fh.mask)
                    detail::prepare_key(d.key, d.fh.key);
                // fall through

            case do_inflate_payload + 1:
            {
                d.state = do_inflate_payload + 2;
                // Read compressed frame payload data
                d.ws.stream_.async_read_some(
                    buffer(d.ws.rd_.buf.get(), clamp(
                        d.remain, d.ws.rd_.buf_size)),
                            std::move(*this));
                return;
            }

            case do_inflate_payload + 2:
            {
                d.remain -= bytes_transferred;
                auto const in = buffer(
                    d.ws.rd_.buf.get(), bytes_transferred);
                if(d.fh.mask)
                    detail::mask_inplace(in, d.key);
                auto const prev = d.db.size();
                detail::inflate(d.ws.pmd_->zi, d.db, in, ec);
                d.ws.failed_ = ec != 0;
                if(d.ws.failed_)
                    break;
                if(d.remain == 0 && d.fh.fin)
                {
                    static std::uint8_t constexpr
                        empty_block[4] = {
                            0x00, 0x00, 0xff, 0xff };
                    detail::inflate(d.ws.pmd_->zi, d.db,
                        buffer(&empty_block[0], 4), ec);
                    d.ws.failed_ = ec != 0;
                    if(d.ws.failed_)
                        break;
                }
                if(d.ws.rd_.op == opcode::text)
                {
                    consuming_buffers<typename
                        DynamicBuffer::const_buffers_type
                            > cb{d.db.data()};
                    cb.consume(prev);
                    if(! d.ws.rd_.utf8.write(cb) ||
                        (d.remain == 0 && d.fh.fin &&
                            ! d.ws.rd_.utf8.finish()))
                    {
                        // invalid utf8
                        code = close_code::bad_payload;
                        d.state = do_fail;
                        break;
                    }
                }
                if(d.remain > 0)
                {
                    d.state = do_inflate_payload + 1;
                    break;
                }
                if(d.fh.fin && (
                    (d.ws.role_ == detail::role_type::client &&
                        d.ws.pmd_config_.server_no_context_takeover) ||
                    (d.ws.role_ == detail::role_type::server &&
                        d.ws.pmd_config_.client_no_context_takeover)))
                    d.ws.pmd_->zi.reset();
                d.state = do_frame_done;
                break;
            }

            //------------------------------------------------------------------

            case do_frame_done:
                // call handler
                d.fi.op = d.ws.rd_.op;
                d.fi.fin = d.fh.fin;
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
                auto const n = d.ws.read_fh1(
                    d.fh, d.fb, code);
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
                d.ws.read_fh2(d.fh, d.fb, code);
                if(code != close_code::none)
                {
                    // protocol error
                    d.state = do_fail;
                    break;
                }
                if(detail::is_control(d.fh.op))
                {
                    if(d.fh.len > 0)
                    {
                        // read control payload
                        d.state = do_control_payload;
                        d.fmb = d.fb.prepare(static_cast<
                            std::size_t>(d.fh.len));
                        boost::asio::async_read(d.ws.stream_,
                            *d.fmb, std::move(*this));
                        return;
                    }
                    d.state = do_control;
                    break;
                }
                if(d.fh.op == opcode::text ||
                        d.fh.op == opcode::binary)
                    d.ws.rd_begin();
                if(d.fh.len == 0 && ! d.fh.fin)
                {
                    // Empty message frame
                    d.state = do_frame_done;
                    break;
                }
                if(! d.ws.pmd_ || ! d.ws.pmd_->rd_set)
                    d.state = do_read_payload;
                else
                    d.state = do_inflate_payload;
                break;

            //------------------------------------------------------------------

            case do_control_payload:
                if(d.fh.mask)
                {
                    detail::prepare_key(d.key, d.fh.key);
                    detail::mask_inplace(*d.fmb, d.key);
                }
                d.fb.commit(bytes_transferred);
                d.state = do_control; // VFALCO fall through?
                break;

            //------------------------------------------------------------------

            case do_control:
                if(d.fh.op == opcode::ping)
                {
                    ping_data payload;
                    detail::read(payload, d.fb.data());
                    d.fb.reset();
                    if(d.ws.ping_cb_)
                        d.ws.ping_cb_(false, payload);
                    if(d.ws.wr_close_)
                    {
                        // ignore ping when closing
                        d.state = do_read_fh;
                        break;
                    }
                    d.ws.template write_ping<static_streambuf>(
                        d.fb, opcode::pong, payload);
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
                else if(d.fh.op == opcode::pong)
                {
                    code = close_code::none;
                    ping_data payload;
                    detail::read(payload, d.fb.data());
                    if(d.ws.ping_cb_)
                        d.ws.ping_cb_(true, payload);
                    d.fb.reset();
                    d.state = do_read_fh;
                    break;
                }
                BOOST_ASSERT(d.fh.op == opcode::close);
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
                BOOST_ASSERT(! d.ws.wr_block_);
                d.ws.wr_block_ = &d;
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
                // [[fallthrough]]

            //------------------------------------------------------------------

            case do_pong:
                if(d.ws.wr_close_)
                {
                    // ignore ping when closing
                    if(d.ws.wr_block_)
                    {
                        BOOST_ASSERT(d.ws.wr_block_ == &d);
                        d.ws.wr_block_ = nullptr;
                    }
                    d.fb.reset();
                    d.state = do_read_fh;
                    break;
                }
                // send pong
                if(! d.ws.wr_block_)
                    d.ws.wr_block_ = &d;
                else
                    BOOST_ASSERT(d.ws.wr_block_ == &d);
                d.state = do_pong + 1;
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
                BOOST_ASSERT(! d.ws.wr_block_);
                d.ws.wr_block_ = &d;
                d.state = do_close_resume + 1;
                // The current context is safe but might not be
                // the same as the one for this operation (since
                // we are being called from a write operation).
                // Call post to make sure we are invoked the same
                // way as the final handler for this operation.
                d.ws.get_io_service().post(bind_handler(
                    std::move(*this), ec, bytes_transferred));
                return;

            case do_close_resume + 1:
                BOOST_ASSERT(d.ws.wr_block_ == &d);
                if(d.ws.failed_)
                {
                    // call handler
                    ec = boost::asio::error::operation_aborted;
                    goto upcall;
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
                if(! d.ws.wr_block_)
                    d.ws.wr_block_ = &d;
                else
                    BOOST_ASSERT(d.ws.wr_block_ == &d);
                d.state = do_teardown;
                d.ws.wr_close_ = true;
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
    d.ws.ping_op_.maybe_invoke() ||
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
        ReadHandler, void(error_code)> completion{handler};
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
    using boost::asio::buffer;
    using boost::asio::buffer_cast;
    using boost::asio::buffer_size;
    close_code::value code{};
    for(;;)
    {
        // Read frame header
        detail::frame_header fh;
        detail::frame_streambuf fb;
        {
            fb.commit(boost::asio::read(
                stream_, fb.prepare(2), ec));
            failed_ = ec != 0;
            if(failed_)
                return;
            {
                auto const n = read_fh1(fh, fb, code);
                if(code != close_code::none)
                    goto do_close;
                if(n > 0)
                {
                    fb.commit(boost::asio::read(
                        stream_, fb.prepare(n), ec));
                    failed_ = ec != 0;
                    if(failed_)
                        return;
                }
            }
            read_fh2(fh, fb, code);

            failed_ = ec != 0;
            if(failed_)
                return;
            if(code != close_code::none)
                goto do_close;
        }
        if(detail::is_control(fh.op))
        {
            // Read control frame payload
            if(fh.len > 0)
            {
                auto const mb = fb.prepare(
                    static_cast<std::size_t>(fh.len));
                fb.commit(boost::asio::read(stream_, mb, ec));
                failed_ = ec != 0;
                if(failed_)
                    return;
                if(fh.mask)
                {
                    detail::prepared_key key;
                    detail::prepare_key(key, fh.key);
                    detail::mask_inplace(mb, key);
                }
                fb.commit(static_cast<std::size_t>(fh.len));
            }
            // Process control frame
            if(fh.op == opcode::ping)
            {
                ping_data payload;
                detail::read(payload, fb.data());
                fb.reset();
                if(ping_cb_)
                    ping_cb_(false, payload);
                write_ping<static_streambuf>(
                    fb, opcode::pong, payload);
                boost::asio::write(stream_, fb.data(), ec);
                failed_ = ec != 0;
                if(failed_)
                    return;
                continue;
            }
            else if(fh.op == opcode::pong)
            {
                ping_data payload;
                detail::read(payload, fb.data());
                if(ping_cb_)
                    ping_cb_(true, payload);
                continue;
            }
            BOOST_ASSERT(fh.op == opcode::close);
            {
                detail::read(cr_, fb.data(), code);
                if(code != close_code::none)
                    goto do_close;
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
                goto do_close;
            }
        }
        if(fh.op != opcode::cont)
            rd_begin();
        if(fh.len == 0 && ! fh.fin)
        {
            // empty frame
            continue;
        }
        auto remain = fh.len;
        detail::prepared_key key;
        if(fh.mask)
            detail::prepare_key(key, fh.key);
        if(! pmd_ || ! pmd_->rd_set)
        {
            // Enforce message size limit
            if(rd_msg_max_ && fh.len >
                rd_msg_max_ - rd_.size)
            {
                code = close_code::too_big;
                goto do_close;
            }
            rd_.size += fh.len;
            // Read message frame payload
            while(remain > 0)
            {
                auto b =
                    dynabuf.prepare(clamp(remain));
                auto const bytes_transferred =
                    stream_.read_some(b, ec);
                failed_ = ec != 0;
                if(failed_)
                    return;
                BOOST_ASSERT(bytes_transferred > 0);
                remain -= bytes_transferred;
                auto const pb = prepare_buffers(
                    bytes_transferred, b);
                if(fh.mask)
                    detail::mask_inplace(pb, key);
                if(rd_.op == opcode::text)
                {
                    if(! rd_.utf8.write(pb) ||
                        (remain == 0 && fh.fin &&
                            ! rd_.utf8.finish()))
                    {
                        code = close_code::bad_payload;
                        goto do_close;
                    }
                }
                dynabuf.commit(bytes_transferred);
            }
        }
        else
        {
            // Read compressed message frame payload:
            // inflate even if fh.len == 0, otherwise we
            // never emit the end-of-stream deflate block.
            for(;;)
            {
                auto const bytes_transferred =
                    stream_.read_some(buffer(rd_.buf.get(),
                        clamp(remain, rd_.buf_size)), ec);
                failed_ = ec != 0;
                if(failed_)
                    return;
                remain -= bytes_transferred;
                auto const in = buffer(
                    rd_.buf.get(), bytes_transferred);
                if(fh.mask)
                    detail::mask_inplace(in, key);
                auto const prev = dynabuf.size();
                detail::inflate(pmd_->zi, dynabuf, in, ec);
                failed_ = ec != 0;
                if(failed_)
                    return;
                if(remain == 0 && fh.fin)
                {
                    static std::uint8_t constexpr
                        empty_block[4] = {
                            0x00, 0x00, 0xff, 0xff };
                    detail::inflate(pmd_->zi, dynabuf,
                        buffer(&empty_block[0], 4), ec);
                    failed_ = ec != 0;
                    if(failed_)
                        return;
                }
                if(rd_.op == opcode::text)
                {
                    consuming_buffers<typename
                        DynamicBuffer::const_buffers_type
                            > cb{dynabuf.data()};
                    cb.consume(prev);
                    if(! rd_.utf8.write(cb) || (
                        remain == 0 && fh.fin &&
                            ! rd_.utf8.finish()))
                    {
                        code = close_code::bad_payload;
                        goto do_close;
                    }
                }
                if(remain == 0)
                    break;
            }
            if(fh.fin && (
                (role_ == detail::role_type::client &&
                    pmd_config_.server_no_context_takeover) ||
                (role_ == detail::role_type::server &&
                    pmd_config_.client_no_context_takeover)))
                pmd_->zi.reset();
        }
        fi.op = rd_.op;
        fi.fin = fh.fin;
        return;
    }
do_close:
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
        : d_(std::forward<DeducedHandler>(h),
            ws, std::forward<Args>(args)...)
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
            > completion{handler};
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
