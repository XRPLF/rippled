//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_WEBSOCKET_IMPL_WRITE_IPP
#define BEAST_WEBSOCKET_IMPL_WRITE_IPP

#include <beast/core/bind_handler.hpp>
#include <beast/core/buffer_cat.hpp>
#include <beast/core/buffer_prefix.hpp>
#include <beast/core/consuming_buffers.hpp>
#include <beast/core/handler_ptr.hpp>
#include <beast/core/static_buffer.hpp>
#include <beast/core/type_traits.hpp>
#include <beast/core/detail/clamp.hpp>
#include <beast/core/detail/config.hpp>
#include <beast/websocket/detail/frame.hpp>
#include <boost/asio/handler_alloc_hook.hpp>
#include <boost/asio/handler_continuation_hook.hpp>
#include <boost/asio/handler_invoke_hook.hpp>
#include <boost/assert.hpp>
#include <boost/config.hpp>
#include <boost/throw_exception.hpp>
#include <algorithm>
#include <memory>

namespace beast {
namespace websocket {

template<class NextLayer>
template<class Buffers, class Handler>
class stream<NextLayer>::write_frame_op
{
    struct data : op
    {
        bool cont;
        stream<NextLayer>& ws;
        consuming_buffers<Buffers> cb;
        bool fin;
        detail::frame_header fh;
        detail::fh_streambuf fh_buf;
        detail::prepared_key key;
        std::uint64_t remain;
        int step = 0;
        int entry_state;

        data(Handler& handler, stream<NextLayer>& ws_,
                bool fin_, Buffers const& bs)
            : ws(ws_)
            , cb(bs)
            , fin(fin_)
        {
            using boost::asio::asio_handler_is_continuation;
            cont = asio_handler_is_continuation(std::addressof(handler));
        }
    };

    handler_ptr<data, Handler> d_;

public:
    write_frame_op(write_frame_op&&) = default;
    write_frame_op(write_frame_op const&) = default;

    template<class DeducedHandler, class... Args>
    write_frame_op(DeducedHandler&& h,
            stream<NextLayer>& ws, Args&&... args)
        : d_(std::forward<DeducedHandler>(h),
            ws, std::forward<Args>(args)...)
    {
    }

    void operator()()
    {
        (*this)({}, 0, true);
    }

    void operator()(error_code ec,
        std::size_t bytes_transferred,
            bool again = true);

    friend
    void* asio_handler_allocate(
        std::size_t size, write_frame_op* op)
    {
        using boost::asio::asio_handler_allocate;
        return asio_handler_allocate(
            size, std::addressof(op->d_.handler()));
    }

    friend
    void asio_handler_deallocate(
        void* p, std::size_t size, write_frame_op* op)
    {
        using boost::asio::asio_handler_deallocate;
        asio_handler_deallocate(
            p, size, std::addressof(op->d_.handler()));
    }

    friend
    bool asio_handler_is_continuation(write_frame_op* op)
    {
        return op->d_->cont;
    }

    template<class Function>
    friend
    void asio_handler_invoke(Function&& f, write_frame_op* op)
    {
        using boost::asio::asio_handler_invoke;
        asio_handler_invoke(
            f, std::addressof(op->d_.handler()));
    }
};

template<class NextLayer>
template<class Buffers, class Handler>
void
stream<NextLayer>::
write_frame_op<Buffers, Handler>::
operator()(error_code ec,
    std::size_t bytes_transferred, bool again)
{
    using beast::detail::clamp;
    using boost::asio::buffer;
    using boost::asio::buffer_copy;
    using boost::asio::buffer_size;
    enum
    {
        do_init = 0,
        do_nomask_nofrag = 20,
        do_nomask_frag = 30,
        do_mask_nofrag = 40,
        do_mask_frag = 50,
        do_deflate = 60,
        do_maybe_suspend = 80,
        do_upcall = 99
    };
    auto& d = *d_;
    d.cont = d.cont || again;
    if(ec)
    {
        BOOST_ASSERT(d.ws.wr_block_ == &d);
        d.ws.failed_ = true;
        goto upcall;
    }
loop:
    switch(d.step)
    {
    case do_init:
        if(! d.ws.wr_.cont)
        {
            d.ws.wr_begin();
            d.fh.rsv1 = d.ws.wr_.compress;
        }
        else
        {
            d.fh.rsv1 = false;
        }
        d.fh.rsv2 = false;
        d.fh.rsv3 = false;
        d.fh.op = d.ws.wr_.cont ?
            detail::opcode::cont : d.ws.wr_opcode_;
        d.fh.mask =
            d.ws.role_ == role_type::client;

        // entry_state determines which algorithm
        // we will use to send. If we suspend, we
        // will transition to entry_state + 1 on
        // the resume.
        if(d.ws.wr_.compress)
        {
            d.entry_state = do_deflate;
        }
        else if(! d.fh.mask)
        {
            if(! d.ws.wr_.autofrag)
            {
                d.entry_state = do_nomask_nofrag;
            }
            else
            {
                BOOST_ASSERT(d.ws.wr_.buf_size != 0);
                d.remain = buffer_size(d.cb);
                if(d.remain > d.ws.wr_.buf_size)
                    d.entry_state = do_nomask_frag;
                else
                    d.entry_state = do_nomask_nofrag;
            }
        }
        else
        {
            if(! d.ws.wr_.autofrag)
            {
                d.entry_state = do_mask_nofrag;
            }
            else
            {
                BOOST_ASSERT(d.ws.wr_.buf_size != 0);
                d.remain = buffer_size(d.cb);
                if(d.remain > d.ws.wr_.buf_size)
                    d.entry_state = do_mask_frag;
                else
                    d.entry_state = do_mask_nofrag;
            }
        }
        d.step = do_maybe_suspend;
        goto loop;

    //----------------------------------------------------------------------

    case do_nomask_nofrag:
        BOOST_ASSERT(d.ws.wr_block_ == &d);
        d.fh.fin = d.fin;
        d.fh.len = buffer_size(d.cb);
        detail::write<static_buffer>(
            d.fh_buf, d.fh);
        d.ws.wr_.cont = ! d.fin;
        // Send frame
        d.step = do_upcall;
        return boost::asio::async_write(d.ws.stream_,
            buffer_cat(d.fh_buf.data(), d.cb),
                std::move(*this));

    //----------------------------------------------------------------------

    go_nomask_frag:
    case do_nomask_frag:
    {
        BOOST_ASSERT(d.ws.wr_block_ == &d);
        auto const n = clamp(
            d.remain, d.ws.wr_.buf_size);
        d.remain -= n;
        d.fh.len = n;
        d.fh.fin = d.fin ? d.remain == 0 : false;
        detail::write<static_buffer>(
            d.fh_buf, d.fh);
        d.ws.wr_.cont = ! d.fin;
        // Send frame
        d.step = d.remain == 0 ?
            do_upcall : do_nomask_frag + 1;
        return boost::asio::async_write(
            d.ws.stream_, buffer_cat(
                d.fh_buf.data(), buffer_prefix(
                    n, d.cb)), std::move(*this));
    }

    case do_nomask_frag + 1:
        BOOST_ASSERT(d.ws.wr_block_ == &d);
        d.ws.wr_block_ = nullptr;
        d.cb.consume(
            bytes_transferred - d.fh_buf.size());
        d.fh_buf.consume(d.fh_buf.size());
        d.fh.op = detail::opcode::cont;
        // Allow outgoing control frames to
        // be sent in between message frames
        if( d.ws.close_op_.maybe_invoke() ||
            d.ws.rd_op_.maybe_invoke() ||
            d.ws.ping_op_.maybe_invoke())
        {
            d.step = do_maybe_suspend;
            return d.ws.get_io_service().post(
                std::move(*this));
        }
        d.ws.wr_block_ = &d;
        goto go_nomask_frag;

    //----------------------------------------------------------------------

    case do_mask_nofrag:
    {
        BOOST_ASSERT(d.ws.wr_block_ == &d);
        d.remain = buffer_size(d.cb);
        d.fh.fin = d.fin;
        d.fh.len = d.remain;
        d.fh.key = d.ws.maskgen_();
        detail::prepare_key(d.key, d.fh.key);
        detail::write<static_buffer>(
            d.fh_buf, d.fh);
        auto const n =
            clamp(d.remain, d.ws.wr_.buf_size);
        auto const b =
            buffer(d.ws.wr_.buf.get(), n);
        buffer_copy(b, d.cb);
        detail::mask_inplace(b, d.key);
        d.remain -= n;
        d.ws.wr_.cont = ! d.fin;
        // Send frame header and partial payload
        d.step = d.remain == 0 ?
            do_upcall : do_mask_nofrag + 1;
        return boost::asio::async_write(
            d.ws.stream_, buffer_cat(d.fh_buf.data(),
                b), std::move(*this));
    }

    case do_mask_nofrag + 1:
    {
        d.cb.consume(d.ws.wr_.buf_size);
        auto const n =
            clamp(d.remain, d.ws.wr_.buf_size);
        auto const b =
            buffer(d.ws.wr_.buf.get(), n);
        buffer_copy(b, d.cb);
        detail::mask_inplace(b, d.key);
        d.remain -= n;
        // Send partial payload
        if(d.remain == 0)
            d.step = do_upcall;
        return boost::asio::async_write(
            d.ws.stream_, b, std::move(*this));
    }

    //----------------------------------------------------------------------

    go_mask_frag:
    case do_mask_frag:
    {
        BOOST_ASSERT(d.ws.wr_block_ == &d);
        auto const n = clamp(
            d.remain, d.ws.wr_.buf_size);
        d.remain -= n;
        d.fh.len = n;
        d.fh.key = d.ws.maskgen_();
        d.fh.fin = d.fin ? d.remain == 0 : false;
        detail::prepare_key(d.key, d.fh.key);
        auto const b = buffer(
            d.ws.wr_.buf.get(), n);
        buffer_copy(b, d.cb);
        detail::mask_inplace(b, d.key);
        detail::write<static_buffer>(
            d.fh_buf, d.fh);
        d.ws.wr_.cont = ! d.fin;
        // Send frame
        d.step = d.remain == 0 ?
            do_upcall : do_mask_frag + 1;
        return boost::asio::async_write(
            d.ws.stream_, buffer_cat(
                d.fh_buf.data(), b),
                    std::move(*this));
    }

    case do_mask_frag + 1:
        BOOST_ASSERT(d.ws.wr_block_ == &d);
        d.ws.wr_block_ = nullptr;
        d.cb.consume(
            bytes_transferred - d.fh_buf.size());
        d.fh_buf.consume(d.fh_buf.size());
        d.fh.op = detail::opcode::cont;
        // Allow outgoing control frames to
        // be sent in between message frames:
        if( d.ws.close_op_.maybe_invoke() ||
            d.ws.rd_op_.maybe_invoke() ||
            d.ws.ping_op_.maybe_invoke())
        {
            d.step = do_maybe_suspend;
            d.ws.get_io_service().post(
                std::move(*this));
            return;
        }
        d.ws.wr_block_ = &d;
        goto go_mask_frag;

    //----------------------------------------------------------------------

    go_deflate:
    case do_deflate:
    {
        BOOST_ASSERT(d.ws.wr_block_ == &d);
        auto b = buffer(d.ws.wr_.buf.get(),
            d.ws.wr_.buf_size);
        auto const more = detail::deflate(
            d.ws.pmd_->zo, b, d.cb, d.fin, ec);
        d.ws.failed_ = !!ec;
        if(d.ws.failed_)
            goto upcall;
        auto const n = buffer_size(b);
        if(n == 0)
        {
            // The input was consumed, but there
            // is no output due to compression
            // latency.
            BOOST_ASSERT(! d.fin);
            BOOST_ASSERT(buffer_size(d.cb) == 0);

            // We can skip the dispatch if the
            // asynchronous initiation function is
            // not on call stack but its hard to
            // figure out so be safe and dispatch.
            d.step = do_upcall;
            d.ws.get_io_service().post(std::move(*this));
            return;
        }
        if(d.fh.mask)
        {
            d.fh.key = d.ws.maskgen_();
            detail::prepared_key key;
            detail::prepare_key(key, d.fh.key);
            detail::mask_inplace(b, key);
        }
        d.fh.fin = ! more;
        d.fh.len = n;
        detail::fh_streambuf fh_buf;
        detail::write<static_buffer>(fh_buf, d.fh);
        d.ws.wr_.cont = ! d.fin;
        // Send frame
        d.step = more ?
            do_deflate + 1 : do_deflate + 2;
        boost::asio::async_write(d.ws.stream_,
            buffer_cat(fh_buf.data(), b),
                std::move(*this));
        return;
    }

    case do_deflate + 1:
        BOOST_ASSERT(d.ws.wr_block_ == &d);
        d.ws.wr_block_ = nullptr;
        d.fh.op = detail::opcode::cont;
        d.fh.rsv1 = false;
        // Allow outgoing control frames to
        // be sent in between message frames:
        if( d.ws.close_op_.maybe_invoke() ||
            d.ws.rd_op_.maybe_invoke() ||
            d.ws.ping_op_.maybe_invoke())
        {
            d.step = do_maybe_suspend;
            d.ws.get_io_service().post(
                std::move(*this));
            return;
        }
        d.ws.wr_block_ = &d;
        goto go_deflate;

    case do_deflate + 2:
        BOOST_ASSERT(d.ws.wr_block_ == &d);
        if(d.fh.fin && (
            (d.ws.role_ == role_type::client &&
                d.ws.pmd_config_.client_no_context_takeover) ||
            (d.ws.role_ == role_type::server &&
                d.ws.pmd_config_.server_no_context_takeover)))
            d.ws.pmd_->zo.reset();
        goto upcall;

    //----------------------------------------------------------------------

    case do_maybe_suspend:
        if(d.ws.wr_block_)
        {
            // suspend
            BOOST_ASSERT(d.ws.wr_block_ != &d);
            d.step = do_maybe_suspend + 1;
            d.ws.wr_op_.emplace(std::move(*this));
            return;
        }
        d.ws.wr_block_ = &d;
        if(d.ws.failed_ || d.ws.wr_close_)
        {
            // call handler
            return d.ws.get_io_service().post(
                bind_handler(std::move(*this),
                    boost::asio::error::operation_aborted, 0));
        }
        d.step = d.entry_state;
        goto loop;

    case do_maybe_suspend + 1:
        BOOST_ASSERT(! d.ws.wr_block_);
        d.ws.wr_block_ = &d;
        d.step = do_maybe_suspend + 2;
        // The current context is safe but might not be
        // the same as the one for this operation (since
        // we are being called from a write operation).
        // Call post to make sure we are invoked the same
        // way as the final handler for this operation.
        d.ws.get_io_service().post(bind_handler(
            std::move(*this), ec, 0));
        return;

    case do_maybe_suspend + 2:
        BOOST_ASSERT(d.ws.wr_block_ == &d);
        if(d.ws.failed_ || d.ws.wr_close_)
        {
            // call handler
            ec = boost::asio::error::operation_aborted;
            goto upcall;
        }
        d.step = d.entry_state;
        goto loop;

    //----------------------------------------------------------------------

    case do_upcall:
        goto upcall;
    }
upcall:
    if(d.ws.wr_block_ == &d)
        d.ws.wr_block_ = nullptr;
    d.ws.close_op_.maybe_invoke() ||
        d.ws.rd_op_.maybe_invoke() ||
        d.ws.ping_op_.maybe_invoke();
    d_.invoke(ec);
}

//------------------------------------------------------------------------------

template<class NextLayer>
template<class Buffers, class Handler>
class stream<NextLayer>::write_op
{
    struct data : op
    {
        int step = 0;
        stream<NextLayer>& ws;
        consuming_buffers<Buffers> cb;
        std::size_t remain;

        data(Handler&, stream<NextLayer>& ws_,
                Buffers const& bs)
            : ws(ws_)
            , cb(bs)
            , remain(boost::asio::buffer_size(cb))
        {
        }
    };

    handler_ptr<data, Handler> d_;

public:
    write_op(write_op&&) = default;
    write_op(write_op const&) = default;

    template<class DeducedHandler, class... Args>
    explicit
    write_op(DeducedHandler&& h,
            stream<NextLayer>& ws, Args&&... args)
        : d_(std::forward<DeducedHandler>(h),
            ws, std::forward<Args>(args)...)
    {
    }

    void operator()(error_code ec);

    friend
    void* asio_handler_allocate(
        std::size_t size, write_op* op)
    {
        using boost::asio::asio_handler_allocate;
        return asio_handler_allocate(
            size, std::addressof(op->d_.handler()));
    }

    friend
    void asio_handler_deallocate(
        void* p, std::size_t size, write_op* op)
    {
        using boost::asio::asio_handler_deallocate;
        asio_handler_deallocate(
            p, size, std::addressof(op->d_.handler()));
    }

    friend
    bool asio_handler_is_continuation(write_op* op)
    {
        using boost::asio::asio_handler_is_continuation;
        return op->d_->step > 2 ||
            asio_handler_is_continuation(
                std::addressof(op->d_.handler()));
    }

    template<class Function>
    friend
    void asio_handler_invoke(Function&& f, write_op* op)
    {
        using boost::asio::asio_handler_invoke;
        asio_handler_invoke(
            f, std::addressof(op->d_.handler()));
    }
};

template<class NextLayer>
template<class Buffers, class Handler>
void
stream<NextLayer>::
write_op<Buffers, Handler>::
operator()(error_code ec)
{
    auto& d = *d_;
    switch(d.step)
    {
    case 2:
        d.step = 3;
        BEAST_FALLTHROUGH;
    case 3:
    case 0:
    {
        auto const n = d.remain;
        d.remain -= n;
        auto const fin = d.remain <= 0;
        if(fin)
            d.step = d.step ? 4 : 1;
        else
            d.step = d.step ? 3 : 2;
        auto const pb = buffer_prefix(n, d.cb);
        d.cb.consume(n);
        return d.ws.async_write_frame(
            fin, pb, std::move(*this));
    }

    case 1:
    case 4:
        break;
    }
    d_.invoke(ec);
}

//------------------------------------------------------------------------------

template<class NextLayer>
template<class ConstBufferSequence>
void
stream<NextLayer>::
write_frame(bool fin, ConstBufferSequence const& buffers)
{
    static_assert(is_sync_stream<next_layer_type>::value,
        "SyncStream requirements not met");
    static_assert(beast::is_const_buffer_sequence<
        ConstBufferSequence>::value,
            "ConstBufferSequence requirements not met");
    error_code ec;
    write_frame(fin, buffers, ec);
    if(ec)
        BOOST_THROW_EXCEPTION(system_error{ec});
}

template<class NextLayer>
template<class ConstBufferSequence>
void
stream<NextLayer>::
write_frame(bool fin,
    ConstBufferSequence const& buffers, error_code& ec)
{
    static_assert(is_sync_stream<next_layer_type>::value,
        "SyncStream requirements not met");
    static_assert(beast::is_const_buffer_sequence<
        ConstBufferSequence>::value,
            "ConstBufferSequence requirements not met");
    using beast::detail::clamp;
    using boost::asio::buffer;
    using boost::asio::buffer_copy;
    using boost::asio::buffer_size;
    detail::frame_header fh;
    if(! wr_.cont)
    {
        wr_begin();
        fh.rsv1 = wr_.compress;
    }
    else
    {
        fh.rsv1 = false;
    }
    fh.rsv2 = false;
    fh.rsv3 = false;
    fh.op = wr_.cont ?
        detail::opcode::cont : wr_opcode_;
    fh.mask = role_ == role_type::client;
    auto remain = buffer_size(buffers);
    if(wr_.compress)
    {
        consuming_buffers<
            ConstBufferSequence> cb{buffers};
        for(;;)
        {
            auto b = buffer(
                wr_.buf.get(), wr_.buf_size);
            auto const more = detail::deflate(
                pmd_->zo, b, cb, fin, ec);
            failed_ = !!ec;
            if(failed_)
                return;
            auto const n = buffer_size(b);
            if(n == 0)
            {
                // The input was consumed, but there
                // is no output due to compression
                // latency.
                BOOST_ASSERT(! fin);
                BOOST_ASSERT(buffer_size(cb) == 0);
                fh.fin = false;
                break;
            }
            if(fh.mask)
            {
                fh.key = maskgen_();
                detail::prepared_key key;
                detail::prepare_key(key, fh.key);
                detail::mask_inplace(b, key);
            }
            fh.fin = ! more;
            fh.len = n;
            detail::fh_streambuf fh_buf;
            detail::write<static_buffer>(fh_buf, fh);
            wr_.cont = ! fin;
            boost::asio::write(stream_,
                buffer_cat(fh_buf.data(), b), ec);
            failed_ = !!ec;
            if(failed_)
                return;
            if(! more)
                break;
            fh.op = detail::opcode::cont;
            fh.rsv1 = false;
        }
        if(fh.fin && (
            (role_ == role_type::client &&
                pmd_config_.client_no_context_takeover) ||
            (role_ == role_type::server &&
                pmd_config_.server_no_context_takeover)))
            pmd_->zo.reset();
        return;
    }
    if(! fh.mask)
    {
        if(! wr_.autofrag)
        {
            // no mask, no autofrag
            fh.fin = fin;
            fh.len = remain;
            detail::fh_streambuf fh_buf;
            detail::write<static_buffer>(fh_buf, fh);
            wr_.cont = ! fin;
            boost::asio::write(stream_,
                buffer_cat(fh_buf.data(), buffers), ec);
            failed_ = !!ec;
            if(failed_)
                return;
        }
        else
        {
            // no mask, autofrag
            BOOST_ASSERT(wr_.buf_size != 0);
            consuming_buffers<
                ConstBufferSequence> cb{buffers};
            for(;;)
            {
                auto const n = clamp(remain, wr_.buf_size);
                remain -= n;
                fh.len = n;
                fh.fin = fin ? remain == 0 : false;
                detail::fh_streambuf fh_buf;
                detail::write<static_buffer>(fh_buf, fh);
                wr_.cont = ! fin;
                boost::asio::write(stream_,
                    buffer_cat(fh_buf.data(),
                        buffer_prefix(n, cb)), ec);
                failed_ = !!ec;
                if(failed_)
                    return;
                if(remain == 0)
                    break;
                fh.op = detail::opcode::cont;
                cb.consume(n);
            }
        }
        return;
    }
    if(! wr_.autofrag)
    {
        // mask, no autofrag
        fh.fin = fin;
        fh.len = remain;
        fh.key = maskgen_();
        detail::prepared_key key;
        detail::prepare_key(key, fh.key);
        detail::fh_streambuf fh_buf;
        detail::write<static_buffer>(fh_buf, fh);
        consuming_buffers<
            ConstBufferSequence> cb{buffers};
        {
            auto const n = clamp(remain, wr_.buf_size);
            auto const b = buffer(wr_.buf.get(), n);
            buffer_copy(b, cb);
            cb.consume(n);
            remain -= n;
            detail::mask_inplace(b, key);
            wr_.cont = ! fin;
            boost::asio::write(stream_,
                buffer_cat(fh_buf.data(), b), ec);
            failed_ = !!ec;
            if(failed_)
                return;
        }
        while(remain > 0)
        {
            auto const n = clamp(remain, wr_.buf_size);
            auto const b = buffer(wr_.buf.get(), n);
            buffer_copy(b, cb);
            cb.consume(n);
            remain -= n;
            detail::mask_inplace(b, key);
            boost::asio::write(stream_, b, ec);
            failed_ = !!ec;
            if(failed_)
                return;
        }
        return;
    }
    {
        // mask, autofrag
        BOOST_ASSERT(wr_.buf_size != 0);
        consuming_buffers<
            ConstBufferSequence> cb{buffers};
        for(;;)
        {
            fh.key = maskgen_();
            detail::prepared_key key;
            detail::prepare_key(key, fh.key);
            auto const n = clamp(remain, wr_.buf_size);
            auto const b = buffer(wr_.buf.get(), n);
            buffer_copy(b, cb);
            detail::mask_inplace(b, key);
            fh.len = n;
            remain -= n;
            fh.fin = fin ? remain == 0 : false;
            wr_.cont = ! fh.fin;
            detail::fh_streambuf fh_buf;
            detail::write<static_buffer>(fh_buf, fh);
            boost::asio::write(stream_,
                buffer_cat(fh_buf.data(), b), ec);
            failed_ = !!ec;
            if(failed_)
                return;
            if(remain == 0)
                break;
            fh.op = detail::opcode::cont;
            cb.consume(n);
        }
        return;
    }
}

template<class NextLayer>
template<class ConstBufferSequence, class WriteHandler>
async_return_type<
    WriteHandler, void(error_code)>
stream<NextLayer>::
async_write_frame(bool fin,
    ConstBufferSequence const& bs, WriteHandler&& handler)
{
    static_assert(is_async_stream<next_layer_type>::value,
        "AsyncStream requirements not met");
    static_assert(beast::is_const_buffer_sequence<
        ConstBufferSequence>::value,
            "ConstBufferSequence requirements not met");
    async_completion<WriteHandler,
        void(error_code)> init{handler};
    write_frame_op<ConstBufferSequence, handler_type<
        WriteHandler, void(error_code)>>{init.completion_handler,
            *this, fin, bs}({}, 0, false);
    return init.result.get();
}

//------------------------------------------------------------------------------

template<class NextLayer>
template<class ConstBufferSequence>
void
stream<NextLayer>::
write(ConstBufferSequence const& buffers)
{
    static_assert(is_sync_stream<next_layer_type>::value,
        "SyncStream requirements not met");
    static_assert(beast::is_const_buffer_sequence<
        ConstBufferSequence>::value,
            "ConstBufferSequence requirements not met");
    error_code ec;
    write(buffers, ec);
    if(ec)
        BOOST_THROW_EXCEPTION(system_error{ec});
}

template<class NextLayer>
template<class ConstBufferSequence>
void
stream<NextLayer>::
write(ConstBufferSequence const& buffers, error_code& ec)
{
    static_assert(is_sync_stream<next_layer_type>::value,
        "SyncStream requirements not met");
    static_assert(beast::is_const_buffer_sequence<
        ConstBufferSequence>::value,
            "ConstBufferSequence requirements not met");
    write_frame(true, buffers, ec);
}

template<class NextLayer>
template<class ConstBufferSequence, class WriteHandler>
async_return_type<
    WriteHandler, void(error_code)>
stream<NextLayer>::
async_write(
    ConstBufferSequence const& bs, WriteHandler&& handler)
{
    static_assert(is_async_stream<next_layer_type>::value,
        "AsyncStream requirements not met");
    static_assert(beast::is_const_buffer_sequence<
        ConstBufferSequence>::value,
            "ConstBufferSequence requirements not met");
    async_completion<WriteHandler,
        void(error_code)> init{handler};
    write_op<ConstBufferSequence, handler_type<
        WriteHandler, void(error_code)>>{
            init.completion_handler, *this, bs}(
                error_code{});
    return init.result.get();
}

} // websocket
} // beast

#endif
