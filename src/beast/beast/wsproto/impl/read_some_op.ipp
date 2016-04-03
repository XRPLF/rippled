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

#ifndef BEAST_WSPROTO_READ_SOME_OP_H_INCLUDED
#define BEAST_WSPROTO_READ_SOME_OP_H_INCLUDED

#include <beast/wsproto/teardown.h>
#include <beast/asio/handler_alloc.h>
#include <beast/asio/prepare_buffers.h>
#include <beast/asio/static_streambuf.h>
#include <cassert>
#include <memory>

namespace beast {
namespace wsproto {

// read message payload, process control frames.
//
template<class Stream>
template<class Streambuf, class Handler>
class socket<Stream>::read_some_op
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
        socket<Stream>& ws;
        msg_info& mi;
        Streambuf& sb;
        smb_type smb;
        Handler h;
        fb_type fb;
        fmb_type fmb;
        int state = 0;

        template<class DeducedHandler>
        data(DeducedHandler&& h_, socket<Stream>& ws_,
                msg_info& mi_, Streambuf& sb_)
            : ws(ws_)
            , mi(mi_)
            , sb(sb_)
            , h(std::forward<DeducedHandler>(h_))
        {
        }
    };

    std::shared_ptr<data> d_;

public:
    read_some_op(read_some_op&&) = default;
    read_some_op(read_some_op const&) = default;

    template<class DeducedHandler>
    explicit
    read_some_op(DeducedHandler&& h, socket<Stream>& ws,
            msg_info& mi, Streambuf& sb)
        : d_(std::allocate_shared<data>(alloc_type{h},
            std::forward<DeducedHandler>(h), ws, mi, sb))
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

    void operator()(error_code ec,
        std::size_t bytes_transferred);

    friend
    auto asio_handler_allocate(
        std::size_t size, read_some_op* op)
    {
        return boost_asio_handler_alloc_helpers::
            allocate(size, op->d_->h);
    }

    friend
    auto asio_handler_deallocate(
        void* p, std::size_t size, read_some_op* op)
    {
        return boost_asio_handler_alloc_helpers::
            deallocate(p, size, op->d_->h);
    }

    friend
    auto asio_handler_is_continuation(read_some_op* op)
    {
        return op->d_->state >= 30 ||
            boost_asio_handler_cont_helpers::
                is_continuation(op->d_->h);
    }

    template <class Function>
    friend
    auto asio_handler_invoke(Function&& f, read_some_op* op)
    {
        return boost_asio_handler_invoke_helpers::
            invoke(f, op->d_->h);
    }
};


template<class Stream>
template<class Buffers, class Handler>
void
socket<Stream>::read_some_op<
    Buffers, Handler>::operator()(error_code ec,
        std::size_t bytes_transferred)
{
    auto& d = *d_;
    close::value code;
    while(! ec && d.state != 999)
    {
        switch(d.state)
        {
        case 0:
            if(d.ws.error_)
            {
                // call handler
                d.state = 999;
                d.ws.get_io_service().post(
                    bind_handler(std::move(*this),
                        boost::asio::error::operation_aborted, 0));
                return;
            }
            if(d.ws.rd_need_ > 0)
            {
                d.state = 10;
                break;
            }
            d.state = 20;
            break;

        case 10:
        case 30:
            // read payload
            d.state = 50;
            d.smb = d.sb.prepare(d.ws.rd_need_);
            d.ws.stream_.async_read_some(
                d.smb, std::move(*this));
            return;

        case 20:
        case 40:
            // read fixed header
            d.state = 70;
            boost::asio::async_read(d.ws.stream_,
                d.fb.prepare(2), std::move(*this));
            return;

        // got payload
        case 50:
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
                    d.state = 300;
                    code = close::bad_payload;
                    break;
                }
            }
            d.sb.commit(bytes_transferred);
            d.state = 60;
            break;
        }
        
        // call handler
        case 60:
            d.state = 999;
            d.mi.op = d.ws.rd_opcode_;
            d.mi.fin = d.ws.rd_fh_.fin &&
                d.ws.rd_need_ == 0;
            break;

        // got fixed header
        case 70:
        {
            d.fb.commit(bytes_transferred);
            code = close::none;
            auto const n = detail::read_fh1(
                d.ws.rd_fh_, d.fb, d.ws.role_, code);
            if(code)
            {
                // protocol error
                d.state = 300;
                break;
            }
            d.state = 80;
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
        case 80:
            d.fb.commit(bytes_transferred);
            code = close::none;
            detail::read_fh2(d.ws.rd_fh_,
                d.fb, d.ws.role_, code);
            if(! code)
                d.ws.prepare_fh(code);
            if(code)
            {
                // protocol error
                d.state = 300;
                break;
            }
            if(detail::is_control(d.ws.rd_fh_.op))
            {
                if(d.ws.rd_fh_.len > 0)
                {
                    // read control payload
                    d.state = 90;
                    d.fmb = d.fb.prepare(d.ws.rd_fh_.len);
                    boost::asio::async_read(d.ws.stream_,
                        d.fmb, std::move(*this));
                    return;
                }
                d.state = 100;
                break;
            }
            if(d.ws.rd_need_ > 0)
            {
                d.state = 30;
                break;
            }
            if(! d.ws.rd_fh_.fin)
            {
                d.state = 40;
                break;
            }
            // empty frame with fin
            d.state = 60;
            break;

        // got control payload
        case 90:
            if(d.ws.rd_fh_.mask)
                detail::mask_inplace(
                    d.fmb, d.ws.rd_key_);
            d.fb.commit(bytes_transferred);
            d.state = 100;
            break;

        // do control
        case 100:
            if(d.ws.rd_fh_.op == opcode::ping)
            {
                code = close::none;
                ping_payload_type data;
                detail::read(data, d.fb.data(), code);
                if(code)
                {
                    // protocol error
                    d.state = 300;
                    break;
                }
                d.fb.reset();
                if(d.ws.wr_close_)
                {
                    d.state = 40;
                    break;
                }
                d.ws.template write_ping<static_streambuf>(
                    d.fb, opcode::pong, data);
                if(d.ws.wr_block_)
                {
                    assert(d.ws.wr_block_ != &d);
                    // suspend
                    d.state = 150;
                    d.ws.rd_op_.template emplace<
                        read_some_op>(std::move(*this));
                    return;
                }
                d.state = 160;
                break;
            }
            else if(d.ws.rd_fh_.op == opcode::pong)
            {
                code = close::none;
                ping_payload_type data;
                detail::read(data, d.fb.data(), code);
                if(code)
                {
                    // protocol error
                    d.state = 300;
                    break;
                }
                d.fb.reset();
                // VFALCO TODO maybe_invoke an async pong handler
                //             For now just ignore the pong.
                d.state = 40;
                break;
            }
            assert(d.ws.rd_fh_.op == opcode::close);
            {
                detail::read(d.ws.cr_, d.fb.data(), code);
                if(code)
                {
                    d.state = 300;
                    break;
                }
                if(! d.ws.wr_close_)
                {
                    auto cr = d.ws.cr_;
                    if(cr.code == close::none)
                        cr.code = close::normal;
                    cr.reason = "";
                    d.fb.reset();
                    d.ws.template write_close<
                        static_streambuf>(d.fb, cr);
                    if(d.ws.wr_block_)
                    {
                        // suspend
                        d.state = 110;
                        d.ws.rd_op_.template emplace<
                            read_some_op>(std::move(*this));
                        return;
                    }
                    d.state = 120;
                    break;
                }
                // call handler;
                d.state = 999;
                ec = error::closed;
                break;
            }

        // resume
        case 110:
            if(d.ws.error_)
            {
                // call handler
                d.state = 999;
                ec = boost::asio::error::operation_aborted;
                break;
            }
            if(d.ws.wr_close_)
            {
                // call handler
                d.state = 999;
                ec = error::closed;
                break;
            }
            d.state = 120;
            break;

        // send close
        case 120:
            d.state = 130;
            assert(! d.ws.wr_block_);
            d.ws.wr_block_ = &d;
            boost::asio::async_write(d.ws.stream_,
                d.fb.data(), std::move(*this));
            return;;

        // teardown
        case 130:
            d.state = 140;
            wsproto_helpers::call_async_teardown(
                d.ws.next_layer_, std::move(*this));
            return;

        case 140:
            // call handler
            d.state = 999;
            ec = error::closed;
            break;

        // resume
        case 150:
            if(d.ws.error_)
            {
                // call handler
                d.state = 999;
                ec = boost::asio::error::operation_aborted;
                break;
            }
            if(d.ws.wr_close_)
            {
                d.fb.reset();
                d.state = 40;
                break;
            }
            d.state = 160;
            break;

        case 160:
            // write ping/pong
            d.state = 170;
            assert(! d.ws.wr_block_);
            d.ws.wr_block_ = &d;
            boost::asio::async_write(d.ws.stream_,
                d.fb.data(), std::move(*this));
            return;

        // sent ping/pong
        case 170:
            d.fb.reset();
            d.state = 40;
            d.ws.wr_block_ = nullptr;
            break;

        // fail the connection
        case 300:
            if(! d.ws.wr_close_)
            {
                d.fb.reset();
                d.ws.template write_close<
                    static_streambuf>(d.fb, code);
                if(d.ws.wr_block_)
                {
                    // suspend
                    d.state = 310;
                    d.ws.rd_op_.template emplace<
                        read_some_op>(std::move(*this));
                    return;
                }
                d.state = 320;
                break;
            }

        // resume
        case 310:
            if(d.ws.wr_close_)
            {
                d.state = 330;
                break;
            }
            d.state = 320;
            break;

        case 320:
            // send close
            d.state = 330;
            d.ws.wr_close_ = true;
            assert(! d.ws.wr_block_);
            d.ws.wr_block_ = &d;
            boost::asio::async_write(d.ws.stream_,
                d.fb.data(), std::move(*this));
            return;

        // teardown
        case 330:
            d.state = 340;
            wsproto_helpers::call_async_teardown(
                d.ws.next_layer_, std::move(*this));
            return;

        case 340:
            // call handler
            d.state = 999;
            ec = error::failed;
            break;
        }
    }
    if(ec)
        d.ws.error_ = true;
    if(d.ws.wr_block_ == &d)
        d.ws.wr_block_ = nullptr;
    d.h(ec, bytes_transferred);
    d.ws.wr_op_.maybe_invoke();
}

} // wsproto
} // beast

#endif
