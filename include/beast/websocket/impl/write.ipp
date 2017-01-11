//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_WEBSOCKET_IMPL_WRITE_IPP
#define BEAST_WEBSOCKET_IMPL_WRITE_IPP

#include <beast/core/bind_handler.hpp>
#include <beast/core/buffer_cat.hpp>
#include <beast/core/buffer_concepts.hpp>
#include <beast/core/consuming_buffers.hpp>
#include <beast/core/handler_helpers.hpp>
#include <beast/core/handler_ptr.hpp>
#include <beast/core/prepare_buffers.hpp>
#include <beast/core/static_streambuf.hpp>
#include <beast/core/stream_concepts.hpp>
#include <beast/core/detail/clamp.hpp>
#include <beast/websocket/detail/frame.hpp>
#include <boost/assert.hpp>
#include <algorithm>
#include <memory>

namespace beast {
namespace websocket {

/*
    template<class ConstBufferSequence>
    void
    write_frame(bool fin, ConstBufferSequence const& buffer)

    Depending on the settings of autofragment role, and compression,
    different algorithms are used.

    1.  autofragment: false
        compression:  false

        In the server role, this will send a single frame in one
        system call, by concatenating the frame header and the payload.

        In the client role, this will send a single frame in one system
        call, using the write buffer to calculate masked data.

    2.  autofragment: true
        compression:  false

        In the server role, this will send one or more frames in one
        system call per sent frame. Each frame is sent by concatenating
        the frame header and payload. The size of each sent frame will
        not exceed the write buffer size option.

        In the client role, this will send one or more frames in one
        system call per sent frame, using the write buffer to calculate
        masked data. The size of each sent frame will not exceed the
        write buffer size option.

    3.  autofragment: false
        compression:  true

        In the server role, this will...

*/
/*
    if(compress)
        compress buffers into write_buffer
        if(write_buffer_avail == write_buffer_size || fin`)
            if(mask)
                apply mask to write buffer
            write frame header, write_buffer as one frame
    else if(auto-fragment)
        if(fin || write_buffer_avail + buffers size == write_buffer_size)
            if(mask)
                append buffers to write buffer
                apply mask to write buffer
                write frame header, write buffer as one frame

            else:
                write frame header, write buffer, and buffers as one frame
        else:
            append buffers to write buffer
    else if(mask)
        copy buffers to write_buffer
        apply mask to write_buffer
        write frame header and possibly full write_buffer in a single call
        loop:
            copy buffers to write_buffer
            apply mask to write_buffer
            write write_buffer in a single call
    else
            write frame header, buffers as one frame
*/

//------------------------------------------------------------------------------

template<class NextLayer>
template<class Buffers, class Handler>
class stream<NextLayer>::write_frame_op
{
    struct data : op
    {
        Handler& handler;
        bool cont;
        stream<NextLayer>& ws;
        consuming_buffers<Buffers> cb;
        detail::frame_header fh;
        detail::fh_streambuf fh_buf;
        detail::prepared_key_type key;
        void* tmp;
        std::size_t tmp_size;
        std::uint64_t remain;
        int state = 0;

        data(Handler& handler_, stream<NextLayer>& ws_,
                bool fin, Buffers const& bs)
            : handler(handler_)
            , cont(beast_asio_helpers::
                is_continuation(handler))
            , ws(ws_)
            , cb(bs)
        {
            using beast::detail::clamp;
            fh.op = ws.wr_.cont ?
                opcode::cont : ws.wr_opcode_;
            ws.wr_.cont = ! fin;
            fh.fin = fin;
            fh.rsv1 = false;
            fh.rsv2 = false;
            fh.rsv3 = false;
            fh.len = boost::asio::buffer_size(cb);
            fh.mask = ws.role_ == detail::role_type::client;
            if(fh.mask)
            {
                fh.key = ws.maskgen_();
                detail::prepare_key(key, fh.key);
                tmp_size = clamp(fh.len, ws.wr_buf_size_);
                tmp = beast_asio_helpers::
                    allocate(tmp_size, handler);
                remain = fh.len;
            }
            else
            {
                tmp = nullptr;
            }
            detail::write<static_streambuf>(fh_buf, fh);
        }

        ~data()
        {
            if(tmp)
                beast_asio_helpers::
                    deallocate(tmp, tmp_size, handler);
        }
    };

    handler_ptr<data, Handler> d_;

public:
    write_frame_op(write_frame_op&&) = default;
    write_frame_op(write_frame_op const&) = default;

    template<class DeducedHandler, class... Args>
    write_frame_op(DeducedHandler&& h,
            stream<NextLayer>& ws, Args&&... args)
        : d_(make_handler_ptr<data, Handler>(
            std::forward<DeducedHandler>(h),
                ws, std::forward<Args>(args)...))
    {
        (*this)(error_code{}, false);
    }

    void operator()()
    {
        (*this)(error_code{});
    }

    void operator()(error_code ec, std::size_t);

    void operator()(error_code ec, bool again = true);

    friend
    void* asio_handler_allocate(
        std::size_t size, write_frame_op* op)
    {
        return beast_asio_helpers::
            allocate(size, op->d_.handler());
    }

    friend
    void asio_handler_deallocate(
        void* p, std::size_t size, write_frame_op* op)
    {
        return beast_asio_helpers::
            deallocate(p, size, op->d_.handler());
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
        return beast_asio_helpers::
            invoke(f, op->d_.handler());
    }
};

template<class NextLayer>
template<class Buffers, class Handler>
void
stream<NextLayer>::
write_frame_op<Buffers, Handler>::
operator()(error_code ec, std::size_t)
{
    auto& d = *d_;
    if(ec)
        d.ws.failed_ = true;
    (*this)(ec);
}

template<class NextLayer>
template<class Buffers, class Handler>
void
stream<NextLayer>::
write_frame_op<Buffers, Handler>::
operator()(error_code ec, bool again)
{
    using beast::detail::clamp;
    using boost::asio::buffer_copy;
    using boost::asio::mutable_buffers_1;
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
                d.state = 3;
                d.ws.wr_op_.template emplace<
                    write_frame_op>(std::move(*this));
                return;
            }
            if(d.ws.failed_ || d.ws.wr_close_)
            {
                // call handler
                d.state = 99;
                d.ws.get_io_service().post(
                    bind_handler(std::move(*this),
                        boost::asio::error::operation_aborted));
                return;
            }
            // fall through

        case 1:
        {
            if(! d.fh.mask)
            {
                // send header and entire payload
                d.state = 99;
                BOOST_ASSERT(! d.ws.wr_block_);
                d.ws.wr_block_ = &d;
                boost::asio::async_write(d.ws.stream_,
                    buffer_cat(d.fh_buf.data(), d.cb),
                        std::move(*this));
                return;
            }
            auto const n = clamp(d.remain, d.tmp_size);
            mutable_buffers_1 mb{d.tmp, n};
            buffer_copy(mb, d.cb);
            d.cb.consume(n);
            d.remain -= n;
            detail::mask_inplace(mb, d.key);
            // send header and payload
            d.state = d.remain > 0 ? 2 : 99;
            BOOST_ASSERT(! d.ws.wr_block_);
            d.ws.wr_block_ = &d;
            boost::asio::async_write(d.ws.stream_,
                buffer_cat(d.fh_buf.data(),
                    mb), std::move(*this));
            return;
        }

        // sent masked payload
        case 2:
        {
            auto const n = clamp(d.remain, d.tmp_size);
            mutable_buffers_1 mb{d.tmp,
                static_cast<std::size_t>(n)};
            buffer_copy(mb, d.cb);
            d.cb.consume(n);
            d.remain -= n;
            detail::mask_inplace(mb, d.key);
            // send payload
            if(d.remain == 0)
                d.state = 99;
            BOOST_ASSERT(d.ws.wr_block_ == &d);
            boost::asio::async_write(
                d.ws.stream_, mb, std::move(*this));
            return;
        }

        case 3:
            d.state = 4;
            d.ws.get_io_service().post(bind_handler(
                std::move(*this), ec));
            return;

        case 4:
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
    d_.invoke(ec);
}

template<class NextLayer>
template<class ConstBufferSequence, class WriteHandler>
typename async_completion<
    WriteHandler, void(error_code)>::result_type
stream<NextLayer>::
async_write_frame(bool fin,
    ConstBufferSequence const& bs, WriteHandler&& handler)
{
    static_assert(is_AsyncStream<next_layer_type>::value,
        "AsyncStream requirements not met");
    static_assert(beast::is_ConstBufferSequence<
        ConstBufferSequence>::value,
            "ConstBufferSequence requirements not met");
    beast::async_completion<
        WriteHandler, void(error_code)
            > completion(handler);
    write_frame_op<ConstBufferSequence, decltype(
        completion.handler)>{completion.handler,
            *this, fin, bs};
    return completion.result.get();
}

template<class NextLayer>
template<class ConstBufferSequence>
void
stream<NextLayer>::
write_frame(bool fin, ConstBufferSequence const& buffers)
{
    static_assert(is_SyncStream<next_layer_type>::value,
        "SyncStream requirements not met");
    static_assert(beast::is_ConstBufferSequence<
        ConstBufferSequence>::value,
            "ConstBufferSequence requirements not met");
    error_code ec;
    write_frame(fin, buffers, ec);
    if(ec)
        throw system_error{ec};
}

template<class NextLayer>
template<class ConstBufferSequence>
void
stream<NextLayer>::
write_frame(bool fin,
    ConstBufferSequence const& buffers, error_code& ec)
{
    static_assert(is_SyncStream<next_layer_type>::value,
        "SyncStream requirements not met");
    static_assert(beast::is_ConstBufferSequence<
        ConstBufferSequence>::value,
            "ConstBufferSequence requirements not met");
    using beast::detail::clamp;
    using boost::asio::buffer;
    using boost::asio::buffer_copy;
    using boost::asio::buffer_size;
    bool const compress = false;
    if(! wr_.cont)
        wr_prepare(compress);
    detail::frame_header fh;
    fh.op = wr_.cont ? opcode::cont : wr_opcode_;
    fh.rsv1 = false;
    fh.rsv2 = false;
    fh.rsv3 = false;
    fh.mask = role_ == detail::role_type::client;
    wr_.cont = ! fin;
    auto remain = buffer_size(buffers);
    if(compress)
    {
        // TODO
    }
    else if(! fh.mask && ! wr_.autofrag)
    {
        fh.fin = fin;
        fh.len = remain;
        detail::fh_streambuf fh_buf;
        detail::write<static_streambuf>(fh_buf, fh);
        boost::asio::write(stream_,
            buffer_cat(fh_buf.data(), buffers), ec);
        failed_ = ec != 0;
        if(failed_)
            return;
        return;
    }
    else if(! fh.mask && wr_.autofrag)
    {
        BOOST_ASSERT(wr_.size != 0);
        consuming_buffers<
            ConstBufferSequence> cb(buffers);
        for(;;)
        {
            auto const n = clamp(remain, wr_.size);
            fh.len = n;
            remain -= n;
            fh.fin = fin ? remain == 0 : false;
            detail::fh_streambuf fh_buf;
            detail::write<static_streambuf>(fh_buf, fh);
            boost::asio::write(stream_,
                buffer_cat(fh_buf.data(),
                    prepare_buffers(n, cb)), ec);
            failed_ = ec != 0;
            if(failed_)
                return;
            if(remain == 0)
                break;
            fh.op = opcode::cont;
            cb.consume(n);
        }
        return;
    }
    else if(fh.mask && ! wr_.autofrag)
    {
        fh.key = maskgen_();
        detail::prepared_key_type key;
        detail::prepare_key(key, fh.key);
        fh.fin = fin;
        fh.len = remain;
        detail::fh_streambuf fh_buf;
        detail::write<static_streambuf>(fh_buf, fh);
        consuming_buffers<
            ConstBufferSequence> cb(buffers);
        {
            auto const n = clamp(remain, wr_.size);
            auto const mb = buffer(wr_.buf.get(), n);
            buffer_copy(mb, cb);
            cb.consume(n);
            remain -= n;
            detail::mask_inplace(mb, key);
            boost::asio::write(stream_,
                buffer_cat(fh_buf.data(), mb), ec);
            failed_ = ec != 0;
            if(failed_)
                return;
        }
        while(remain > 0)
        {
            auto const n = clamp(remain, wr_.size);
            auto const mb = buffer(wr_.buf.get(), n);
            buffer_copy(mb, cb);
            cb.consume(n);
            remain -= n;
            detail::mask_inplace(mb, key);
            boost::asio::write(stream_, mb, ec);
            failed_ = ec != 0;
            if(failed_)
                return;
        }
        return;
    }
    else if(fh.mask && wr_.autofrag)
    {
        BOOST_ASSERT(wr_.size != 0);
        consuming_buffers<
            ConstBufferSequence> cb(buffers);
        for(;;)
        {
            fh.key = maskgen_();
            detail::prepared_key_type key;
            detail::prepare_key(key, fh.key);
            auto const n = clamp(remain, wr_.size);
            auto const mb = buffer(wr_.buf.get(), n);
            buffer_copy(mb, cb);
            detail::mask_inplace(mb, key);
            fh.len = n;
            remain -= n;
            fh.fin = fin ? remain == 0 : false;
            detail::fh_streambuf fh_buf;
            detail::write<static_streambuf>(fh_buf, fh);
            boost::asio::write(stream_,
                buffer_cat(fh_buf.data(), mb), ec);
            failed_ = ec != 0;
            if(failed_)
                return;
            if(remain == 0)
                break;
            fh.op = opcode::cont;
            cb.consume(n);
        }
        return;
    }
}

//------------------------------------------------------------------------------

template<class NextLayer>
template<class Buffers, class Handler>
class stream<NextLayer>::write_op
{
    struct data : op
    {
        bool cont;
        stream<NextLayer>& ws;
        consuming_buffers<Buffers> cb;
        std::size_t remain;
        int state = 0;

        data(Handler& handler, stream<NextLayer>& ws_,
                Buffers const& bs)
            : cont(beast_asio_helpers::
                is_continuation(handler))
            , ws(ws_)
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
        : d_(make_handler_ptr<data, Handler>(
            std::forward<DeducedHandler>(h), ws,
                std::forward<Args>(args)...))
    {
        (*this)(error_code{}, false);
    }

    void operator()(error_code ec, bool again = true);

    friend
    void* asio_handler_allocate(
        std::size_t size, write_op* op)
    {
        return beast_asio_helpers::
            allocate(size, op->d_.handler());
    }

    friend
    void asio_handler_deallocate(
        void* p, std::size_t size, write_op* op)
    {
        return beast_asio_helpers::
            deallocate(p, size, op->d_.handler());
    }

    friend
    bool asio_handler_is_continuation(write_op* op)
    {
        return op->d_->cont;
    }

    template<class Function>
    friend
    void asio_handler_invoke(Function&& f, write_op* op)
    {
        return beast_asio_helpers::
            invoke(f, op->d_.handler());
    }
};

template<class NextLayer>
template<class Buffers, class Handler>
void
stream<NextLayer>::
write_op<Buffers, Handler>::
operator()(error_code ec, bool again)
{
    auto& d = *d_;
    d.cont = d.cont || again;
    if(! ec)
    {
        switch(d.state)
        {
        case 0:
        {
            auto const n = d.remain;
            d.remain -= n;
            auto const fin = d.remain <= 0;
            if(fin)
                d.state = 99;
            auto const pb = prepare_buffers(n, d.cb);
            d.cb.consume(n);
            d.ws.async_write_frame(fin, pb, std::move(*this));
            return;
        }

        case 99:
            break;
        }
    }
    d_.invoke(ec);
}

template<class NextLayer>
template<class ConstBufferSequence, class WriteHandler>
typename async_completion<
    WriteHandler, void(error_code)>::result_type
stream<NextLayer>::
async_write(ConstBufferSequence const& bs, WriteHandler&& handler)
{
    static_assert(is_AsyncStream<next_layer_type>::value,
        "AsyncStream requirements not met");
    static_assert(beast::is_ConstBufferSequence<
        ConstBufferSequence>::value,
            "ConstBufferSequence requirements not met");
    beast::async_completion<
        WriteHandler, void(error_code)> completion(handler);
    write_op<ConstBufferSequence, decltype(completion.handler)>{
        completion.handler, *this, bs};
    return completion.result.get();
}

template<class NextLayer>
template<class ConstBufferSequence>
void
stream<NextLayer>::
write(ConstBufferSequence const& buffers)
{
    static_assert(is_SyncStream<next_layer_type>::value,
        "SyncStream requirements not met");
    static_assert(beast::is_ConstBufferSequence<
        ConstBufferSequence>::value,
            "ConstBufferSequence requirements not met");
    error_code ec;
    write(buffers, ec);
    if(ec)
        throw system_error{ec};
}

template<class NextLayer>
template<class ConstBufferSequence>
void
stream<NextLayer>::
write(ConstBufferSequence const& buffers, error_code& ec)
{
    static_assert(is_SyncStream<next_layer_type>::value,
        "SyncStream requirements not met");
    static_assert(beast::is_ConstBufferSequence<
        ConstBufferSequence>::value,
            "ConstBufferSequence requirements not met");
    write_frame(true, buffers, ec);
}

//------------------------------------------------------------------------------

} // websocket
} // beast

#endif
