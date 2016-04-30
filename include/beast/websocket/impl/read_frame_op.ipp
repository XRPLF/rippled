//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_WEBSOCKET_IMPL_READ_FRAME_OP_HPP
#define BEAST_WEBSOCKET_IMPL_READ_FRAME_OP_HPP

#include <beast/websocket/teardown.hpp>
#include <beast/handler_alloc.hpp>
#include <beast/prepare_buffers.hpp>
#include <beast/static_streambuf.hpp>
#include <cassert>
#include <memory>

namespace beast {
namespace websocket {

// Reads a single message frame,
// processes any received control frames.
//
template<class NextLayer>
template<class Streambuf, class Handler>
class stream<NextLayer>::read_frame_op
{
    using alloc_type =
        handler_alloc<char, Handler>;

    using fb_type =
        detail::frame_streambuf;

    using fmb_type =
        typename fb_type::mutable_buffers_type;

    using smb_type =
        typename Streambuf::mutable_buffers_type;

    struct data : op
    {
        stream<NextLayer>& ws;
        frame_info& fi;
        Streambuf& sb;
        smb_type smb;
        Handler h;
        fb_type fb;
        fmb_type fmb;
        bool cont;
        int state = 0;

        template<class DeducedHandler>
        data(DeducedHandler&& h_, stream<NextLayer>& ws_,
                frame_info& fi_, Streambuf& sb_)
            : ws(ws_)
            , fi(fi_)
            , sb(sb_)
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
        auto& d = *d_;
        d.cont = false;
        (*this)(error_code{}, 0, false);
    }

    void operator()(error_code const& ec)
    {
        (*this)(ec, 0);
    }

    void operator()(error_code ec,
        std::size_t bytes_transferred, bool again = true);

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

    template <class Function>
    friend
    void asio_handler_invoke(Function&& f, read_frame_op* op)
    {
        return boost_asio_handler_invoke_helpers::
            invoke(f, op->d_->h);
    }
};

template<class NextLayer>
template<class Buffers, class Handler>
void
stream<NextLayer>::read_frame_op<Buffers, Handler>::
operator()(error_code ec,std::size_t bytes_transferred, bool again)
{
    auto& d = *d_;
    d.cont = d.cont || again;
    close_code::value code = close_code::none;
    while(! ec && d.state != 99)
    {
        switch(d.state)
        {
        case 0:
            if(d.ws.error_)
            {
                // call handler
                d.state = 99;
                d.ws.get_io_service().post(
                    bind_handler(std::move(*this),
                        boost::asio::error::operation_aborted, 0));
                return;
            }
            if(d.ws.rd_need_ > 0)
            {
                d.state = 1;
                break;
            }
            d.state = 2;
            break;

        case 1:
            // read payload
            d.state = 3;
            d.smb = d.sb.prepare(
                detail::clamp(d.ws.rd_need_));
            d.ws.stream_.async_read_some(
                d.smb, std::move(*this));
            return;

        case 2:
            // read fixed header
            d.state = 5;
            boost::asio::async_read(d.ws.stream_,
                d.fb.prepare(2), std::move(*this));
            return;

        // got payload
        case 3:
        {
            d.ws.rd_need_ -= bytes_transferred;
            auto const pb = prepare_buffers(
                bytes_transferred, d.smb);
            if(d.ws.rd_fh_.mask)
                detail::mask_inplace(pb, d.ws.rd_key_);
            if(d.ws.rd_opcode_ == opcode::text)
            {
                if(! d.ws.rd_utf8_check_.write(pb) ||
                    (d.ws.rd_need_ == 0 && d.ws.rd_fh_.fin &&
                        ! d.ws.rd_utf8_check_.finish()))
                {
                    // invalid utf8
                    d.state = 16;
                    code = close_code::bad_payload;
                    break;
                }
            }
            d.sb.commit(bytes_transferred);
            d.state = 4;
            break;
        }

        // call handler
        case 4:
            d.state = 99;
            d.fi.op = d.ws.rd_opcode_;
            d.fi.fin = d.ws.rd_fh_.fin &&
                d.ws.rd_need_ == 0;
            break;

        // got fixed header
        case 5:
        {
            d.fb.commit(bytes_transferred);
            code = close_code::none;
            auto const n = detail::read_fh1(
                d.ws.rd_fh_, d.fb, d.ws.role_, code);
            if(code != close_code::none)
            {
                // protocol error
                d.state = 16;
                break;
            }
            d.state = 6;
            if (n == 0)
            {
                bytes_transferred = 0;
                break;
            }
            // read variable header
            boost::asio::async_read(d.ws.stream_,
                d.fb.prepare(n), std::move(*this));
            return;
        }

        // got variable header
        case 6:
            d.fb.commit(bytes_transferred);
            code = close_code::none;
            detail::read_fh2(d.ws.rd_fh_,
                d.fb, d.ws.role_, code);
            if(code == close_code::none)
                d.ws.prepare_fh(code);
            if(code != close_code::none)
            {
                // protocol error
                d.state = 16;
                break;
            }
            if(detail::is_control(d.ws.rd_fh_.op))
            {
                if(d.ws.rd_fh_.len > 0)
                {
                    // read control payload
                    d.state = 7;
                    d.fmb = d.fb.prepare(static_cast<
                        std::size_t>(d.ws.rd_fh_.len));
                    boost::asio::async_read(d.ws.stream_,
                        d.fmb, std::move(*this));
                    return;
                }
                d.state = 8;
                break;
            }
            if(d.ws.rd_need_ > 0)
            {
                d.state = 1;
                break;
            }
            if(! d.ws.rd_fh_.fin)
            {
                d.state = 2;
                break;
            }
            // empty frame with fin
            d.state = 4;
            break;

        // got control payload
        case 7:
            if(d.ws.rd_fh_.mask)
                detail::mask_inplace(
                    d.fmb, d.ws.rd_key_);
            d.fb.commit(bytes_transferred);
            d.state = 8;
            break;

        // do control
        case 8:
            if(d.ws.rd_fh_.op == opcode::ping)
            {
                code = close_code::none;
                ping_payload_type data;
                detail::read(data, d.fb.data(), code);
                if(code != close_code::none)
                {
                    // protocol error
                    d.state = 16;
                    break;
                }
                d.fb.reset();
                if(d.ws.wr_close_)
                {
                    d.state = 2;
                    break;
                }
                d.ws.template write_ping<static_streambuf>(
                    d.fb, opcode::pong, data);
                if(d.ws.wr_block_)
                {
                    assert(d.ws.wr_block_ != &d);
                    // suspend
                    d.state = 13;
                    d.ws.rd_op_.template emplace<
                        read_frame_op>(std::move(*this));
                    return;
                }
                d.state = 14;
                break;
            }
            else if(d.ws.rd_fh_.op == opcode::pong)
            {
                code = close_code::none;
                ping_payload_type data;
                detail::read(data, d.fb.data(), code);
                if(code != close_code::none)
                {
                    // protocol error
                    d.state = 16;
                    break;
                }
                d.fb.reset();
                // VFALCO TODO maybe_invoke an async pong handler
                //             For now just ignore the pong.
                d.state = 2;
                break;
            }
            assert(d.ws.rd_fh_.op == opcode::close);
            {
                detail::read(d.ws.cr_, d.fb.data(), code);
                if(code != close_code::none)
                {
                    d.state = 16;
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
                        d.state = 9;
                        d.ws.rd_op_.template emplace<
                            read_frame_op>(std::move(*this));
                        return;
                    }
                    d.state = 10;
                    break;
                }
                // call handler;
                d.state = 99;
                ec = error::closed;
                break;
            }

        // resume
        case 9:
            if(d.ws.error_)
            {
                // call handler
                d.state = 99;
                ec = boost::asio::error::operation_aborted;
                break;
            }
            if(d.ws.wr_close_)
            {
                // call handler
                d.state = 99;
                ec = error::closed;
                break;
            }
            d.state = 10;
            break;

        // send close
        case 10:
            d.state = 11;
            assert(! d.ws.wr_block_);
            d.ws.wr_block_ = &d;
            boost::asio::async_write(d.ws.stream_,
                d.fb.data(), std::move(*this));
            return;;

        // teardown
        case 11:
            d.state = 12;
            wsproto_helpers::call_async_teardown(
                d.ws.next_layer(), std::move(*this));
            return;

        case 12:
            // call handler
            d.state = 99;
            ec = error::closed;
            break;

        // resume
        case 13:
            if(d.ws.error_)
            {
                // call handler
                d.state = 99;
                ec = boost::asio::error::operation_aborted;
                break;
            }
            if(d.ws.wr_close_)
            {
                d.fb.reset();
                d.state = 2;
                break;
            }
            d.state = 14;
            break;

        case 14:
            // write ping/pong
            d.state = 15;
            assert(! d.ws.wr_block_);
            d.ws.wr_block_ = &d;
            boost::asio::async_write(d.ws.stream_,
                d.fb.data(), std::move(*this));
            return;

        // sent ping/pong
        case 15:
            d.fb.reset();
            d.state = 2;
            d.ws.wr_block_ = nullptr;
            break;

        // fail the connection
        case 16:
            if(! d.ws.wr_close_)
            {
                d.fb.reset();
                d.ws.template write_close<
                    static_streambuf>(d.fb, code);
                if(d.ws.wr_block_)
                {
                    // suspend
                    d.state = 17;
                    d.ws.rd_op_.template emplace<
                        read_frame_op>(std::move(*this));
                    return;
                }
                d.state = 18;
                break;
            }

        // resume
        case 17:
            if(d.ws.wr_close_)
            {
                d.state = 19;
                break;
            }
            d.state = 18;
            break;

        case 18:
            // send close
            d.state = 19;
            d.ws.wr_close_ = true;
            assert(! d.ws.wr_block_);
            d.ws.wr_block_ = &d;
            boost::asio::async_write(d.ws.stream_,
                d.fb.data(), std::move(*this));
            return;

        // teardown
        case 19:
            d.state = 20;
            wsproto_helpers::call_async_teardown(
                d.ws.next_layer(), std::move(*this));
            return;

        case 20:
            // call handler
            d.state = 99;
            ec = error::failed;
            break;
        }
    }
    if(ec)
        d.ws.error_ = true;
    if(d.ws.wr_block_ == &d)
        d.ws.wr_block_ = nullptr;
    d.h(ec);
    d.ws.wr_op_.maybe_invoke();
}

} // websocket
} // beast

#endif
