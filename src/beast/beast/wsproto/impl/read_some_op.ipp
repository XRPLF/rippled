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

namespace beast {
namespace wsproto {

// read message payload, process control frames.
//
template<class Stream>
template<class Streambuf, class Handler>
class socket<Stream>::read_some_op
{
    using alloc_type =
        asio::handler_alloc<char, Handler>;

    using fb_type =
        asio::static_streambuf_n<139>; // 14 + 125

    using fmb_type =
        typename fb_type::mutable_buffers_type;

    using smb_type =
        typename Streambuf::mutable_buffers_type;

    struct data
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
        return op->d_->state >= 2 ||
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
    using namespace boost::asio;
    auto& d = *d_;
    close::value code;
    for(;! ec && d.state != 99;)
    {
        switch(d.state)
        {
        case 0:
            if(d.ws.rd_need_ > 0)
            {
                d.state = 1;
                break;
            }
            d.state = 2;
            break;

        case 2:
            // read fixed header
            d.state = 10;
            boost::asio::async_read(d.ws.stream_,
                d.fb.prepare(2), std::move(*this));
            return;

        case 1:
        case 3:
            // read payload
            d.state = 4;
            d.smb = d.sb.prepare(d.ws.rd_need_);
            d.ws.stream_.async_read_some(
                d.smb, std::move(*this));
            return;

        // got payload
        case 4:
        {
            d.ws.rd_need_ -= bytes_transferred;
            auto const pb = asio::prepare_buffers(
                bytes_transferred, d.smb);
            if(d.ws.rd_fh_.mask)
                detail::mask_inplace(pb, d.ws.rd_key_);
            if(d.ws.rd_op_ == opcode::text)
            {
                if(! d.ws.rd_utf8_check_.write(pb) ||
                    (d.ws.rd_need_ == 0 && d.ws.rd_fh_.fin &&
                        ! d.ws.rd_utf8_check_.finish()))
                {
                    // invalid utf8
                    d.state = 30;
                    code = close::bad_payload;
                    break;
                }
            }
            d.sb.commit(bytes_transferred);
            d.state = 5;
            break;
        }

        // call handler
        case 5:
        {
            d.state = 99;
            d.mi.op = d.ws.rd_op_;
            d.mi.fin = d.ws.rd_fh_.fin &&
                d.ws.rd_need_ == 0;
            break;
        }

        // got fixed header
        case 10:
        {
            d.fb.commit(bytes_transferred);
            code = close::none;
            auto const n = detail::read_fh1(
                d.ws.rd_fh_, d.fb, d.ws.role_, code);
            if(code)
            {
                // protocol error
                d.state = 30;
                break;
            }
            // read variable header
            d.state = 11;
            boost::asio::async_read(d.ws.stream_,
                d.fb.prepare(n), std::move(*this));
            return;
        }

        // got variable header
        case 11:
            d.fb.commit(bytes_transferred);
            code = close::none;
            detail::read_fh2(d.ws.rd_fh_,
                d.fb, d.ws.role_, code);
            if(! code)
                d.ws.prepare_fh(code);
            if(code)
            {
                // protocol error
                d.state = 30;
                break;
            }
            if(detail::is_control(d.ws.rd_fh_.op))
            {
                if(d.ws.rd_fh_.len == 0)
                {
                    // do control
                    d.state = 13;
                    break;
                }
                // read control payload
                d.state = 12;
                d.fmb = d.fb.prepare(d.ws.rd_fh_.len);
                boost::asio::async_read(d.ws.stream_,
                    d.fmb, std::move(*this));
                return;
            }
            if(d.ws.rd_fh_.len > 0)
            {
                d.state = 3;
                break;
            }
            if(! d.ws.rd_fh_.fin)
            {
                d.state = 2;
                break;
            }
            // empty frame with fin
            d.state = 5;
            break;

        // got control payload
        case 12:
            if(d.ws.rd_fh_.mask)
                detail::mask_inplace(
                    d.fmb, d.ws.rd_key_);
            d.fb.commit(bytes_transferred);
            d.state = 13;
            break;

        // do control
        case 13:
            if(d.ws.rd_fh_.op == opcode::ping)
            {
                code = close::none;
                ping_payload_type data;
                detail::read(data, d.fb.data(), code);
                if(code)
                {
                    // protocol error
                    d.state = 30;
                    break;
                }
                d.fb.reset();
                d.ws.template write_ping<
                    asio::static_streambuf>(
                        d.fb, opcode::pong, data);
                d.state = 14;
                if(d.ws.wr_active_)
                {
                    // suspend
                    d.ws.wr_invoke_.template emplace<
                        read_some_op>(std::move(*this));
                    return;
                }
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
                    d.state = 30;
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
                // VFALCO Can this ever happen?
                if(d.ws.fail_)
                {
                    // call handler
                    d.state = 99;
                    ec = error::closed;
                    break;
                }
                close_reason cr;
                close::value code{};
                detail::read(cr, d.fb.data(), code);
                if(code)
                {
                    cr.code = code;
                    cr.reason = "";
                }
                else if(cr.code == close::none)
                {
                    cr.code = close::normal;
                    cr.reason = "";
                }
                else if(! detail::is_valid(cr.code))
                {
                    cr.code = close::protocol_error;
                    cr.reason = "";
                }
                // VFALCO This handling of a normal close needs work
                d.state = 31;
                d.fb.reset();
                d.ws.template write_close<
                    asio::static_streambuf>(d.fb, cr);
                // VFALCO Should we set fail_ here?
                d.ws.fail_ = true;
                if(d.ws.wr_active_)
                {
                    // suspend
                    d.ws.wr_invoke_.template emplace<
                        read_some_op>(std::move(*this));
                    return;
                }
                break;
            }

        case 14:
            // write ping/pong
            d.state = 15;
            d.ws.wr_active_ = true;
            boost::asio::async_write(d.ws.stream_,
                d.fb.data(), std::move(*this));
            return;

        // sent ping/pong
        case 15:
            d.state = 2;
            d.fb.reset();
            d.ws.wr_active_ = false;
            break;

        // fail the connection
        case 30:
            d.fb.reset();
            d.ws.template write_close<
                asio::static_streambuf>(d.fb, code);
            d.state = 31;
            d.ws.fail_ = true;
            if(d.ws.wr_active_)
            {
                // suspend
                d.ws.wr_invoke_.template emplace<
                    read_some_op>(std::move(*this));
                return;
            }
            break;

        case 31:
            // send close
            d.state = 32;
            d.ws.wr_active_ = true;
            boost::asio::async_write(d.ws.stream_,
                d.fb.data(), std::move(*this));
            return;

        // teardown
        case 32:
            d.state = 33;
            wsproto_helpers::call_async_teardown(
                d.ws.next_layer_, std::move(*this));
            return;

        case 33:
            d.ws.wr_active_ = false;
            // call handler
            d.state = 99;
            ec = error::closed;
            break;
        }
    }
    d.ws.rd_invoke_.maybe_invoke();
    d.h(ec, bytes_transferred);
}

} // wsproto
} // beast

#endif
