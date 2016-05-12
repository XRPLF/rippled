//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_WEBSOCKET_IMPL_WRITE_FRAME_OP_HPP
#define BEAST_WEBSOCKET_IMPL_WRITE_FRAME_OP_HPP

#include <beast/core/buffer_cat.hpp>
#include <beast/core/bind_handler.hpp>
#include <beast/core/consuming_buffers.hpp>
#include <beast/core/handler_alloc.hpp>
#include <beast/core/static_streambuf.hpp>
#include <beast/websocket/detail/frame.hpp>
#include <algorithm>
#include <cassert>
#include <memory>

namespace beast {
namespace websocket {

// write a frame
//
template<class NextLayer>
template<class Buffers, class Handler>
class stream<NextLayer>::write_frame_op
{
    using alloc_type =
        handler_alloc<char, Handler>;

    struct data : op
    {
        stream<NextLayer>& ws;
        consuming_buffers<Buffers> cb;
        Handler h;
        detail::frame_header fh;
        detail::fh_streambuf fh_buf;
        detail::prepared_key_type key;
        void* tmp;
        std::size_t tmp_size;
        std::uint64_t remain;
        bool cont;
        int state = 0;

        template<class DeducedHandler>
        data(DeducedHandler&& h_, stream<NextLayer>& ws_,
                bool fin, Buffers const& bs)
            : ws(ws_)
            , cb(bs)
            , h(std::forward<DeducedHandler>(h_))
            , cont(boost_asio_handler_cont_helpers::
                is_continuation(h))
        {
            fh.op = ws.wr_cont_ ?
                opcode::cont : ws.wr_opcode_;
            ws.wr_cont_ = ! fin;
            fh.fin = fin;
            fh.rsv1 = 0;
            fh.rsv2 = 0;
            fh.rsv3 = 0;
            fh.len = boost::asio::buffer_size(cb);
            fh.mask = ws.role_ == role_type::client;
            if(fh.mask)
            {
                fh.key = ws.maskgen_();
                detail::prepare_key(key, fh.key);
                tmp_size = detail::clamp(
                    fh.len, ws.wr_buf_size_);
                tmp = boost_asio_handler_alloc_helpers::
                    allocate(tmp_size, h);
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
                boost_asio_handler_alloc_helpers::
                    deallocate(tmp, tmp_size, h);
        }
    };

    std::shared_ptr<data> d_;

public:
    write_frame_op(write_frame_op&&) = default;
    write_frame_op(write_frame_op const&) = default;

    template<class DeducedHandler, class... Args>
    write_frame_op(DeducedHandler&& h,
            stream<NextLayer>& ws, Args&&... args)
        : d_(std::make_shared<data>(
            std::forward<DeducedHandler>(h), ws,
                std::forward<Args>(args)...))
    {
        (*this)(error_code{}, 0, false);
    }

    void operator()()
    {
        auto& d = *d_;
        d.cont = false;
        (*this)(error_code{}, 0, false);
    }

    void operator()(error_code ec,
        std::size_t bytes_transferred, bool again = true);

    friend
    void* asio_handler_allocate(
        std::size_t size, write_frame_op* op)
    {
        return boost_asio_handler_alloc_helpers::
            allocate(size, op->d_->h);
    }

    friend
    void asio_handler_deallocate(
        void* p, std::size_t size, write_frame_op* op)
    {
        return boost_asio_handler_alloc_helpers::
            deallocate(p, size, op->d_->h);
    }

    friend
    bool asio_handler_is_continuation(write_frame_op* op)
    {
        return op->d_->cont;
    }

    template <class Function>
    friend
    void asio_handler_invoke(Function&& f, write_frame_op* op)
    {
        return boost_asio_handler_invoke_helpers::
            invoke(f, op->d_->h);
    }
};

template<class NextLayer>
template<class Buffers, class Handler>
void
stream<NextLayer>::
write_frame_op<Buffers, Handler>::
operator()(
    error_code ec, std::size_t bytes_transferred, bool again)
{
    using boost::asio::buffer_copy;
    using boost::asio::mutable_buffers_1;
    auto& d = *d_;
    d.cont = d.cont || again;
    while(! ec && d.state != 99)
    {
        switch(d.state)
        {
        case 0:
            if(d.ws.wr_block_)
            {
                // suspend
                d.state = 1;
                d.ws.wr_op_.template emplace<
                    write_frame_op>(std::move(*this));
                return;
            }
            if(d.ws.error_)
            {
                // call handler
                d.state = 99;
                d.ws.get_io_service().post(
                    bind_handler(std::move(*this),
                        boost::asio::error::operation_aborted, 0));
                return;
            }
            assert(! d.ws.wr_close_);
            d.state = 2;
            break;

        // resume
        case 1:
            if(d.ws.error_)
            {
                // call handler
                d.state = 99;
                ec = boost::asio::error::operation_aborted;
                break;
            }
            d.state = 2;
            break;

        case 2:
        {
            if(! d.fh.mask)
            {
                // send header and payload
                d.state = 99;
                assert(! d.ws.wr_block_);
                d.ws.wr_block_ = &d;
                boost::asio::async_write(d.ws.stream_,
                    buffer_cat(d.fh_buf.data(), d.cb),
                        std::move(*this));
                return;
            }
            auto const n =
                detail::clamp(d.remain, d.tmp_size);
            mutable_buffers_1 mb{d.tmp, n};
            buffer_copy(mb, d.cb);
            d.cb.consume(n);
            d.remain -= n;
            detail::mask_inplace(mb, d.key);
            // send header and payload
            d.state = d.remain > 0 ? 3 : 99;
            assert(! d.ws.wr_block_);
            d.ws.wr_block_ = &d;
            boost::asio::async_write(d.ws.stream_,
                buffer_cat(d.fh_buf.data(),
                    mb), std::move(*this));
            return;
        }

        // sent masked payload
        case 3:
        {
            auto const n =
                detail::clamp(d.remain, d.tmp_size);
            mutable_buffers_1 mb{d.tmp,
                static_cast<std::size_t>(n)};
            buffer_copy(mb, d.cb);
            d.cb.consume(n);
            d.remain -= n;
            detail::mask_inplace(mb, d.key);
            // send payload
            if(d.remain == 0)
                d.state = 99;
            assert(! d.ws.wr_block_);
            d.ws.wr_block_ = &d;
            boost::asio::async_write(
                d.ws.stream_, mb, std::move(*this));
            return;
        }
        }
    }
    if(ec)
        d.ws.error_ = true;
    if(d.ws.wr_block_ == &d)
        d.ws.wr_block_ = nullptr;
    if(d.tmp)
    {
        boost_asio_handler_alloc_helpers::
            deallocate(d.tmp, d.tmp_size, d.h);
        d.tmp = nullptr;
    }
    d.ws.rd_op_.maybe_invoke();
    d.h(ec);
}

} // websocket
} // beast

#endif
