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

#ifndef BEAST_WSPROTO_HEADER_OP_H_INCLUDED
#define BEAST_WSPROTO_HEADER_OP_H_INCLUDED

#include <beast/wsproto/detail/handler_alloc.h>
#include <beast/asio/static_streambuf.h>
#include <cassert>

namespace beast {
namespace wsproto {

// Read frame header.
// This will also process control frames transparently.
//
template<class Stream>
template<class Handler>
class socket<Stream>::header_op
{
    struct data
    {
        using streambuf_type =
            asio::static_streambuf_n<139>;
        using buffers_type =
            typename streambuf_type::mutable_buffers_type;
        socket<Stream>& ws;
        frame_header& fh;
        Handler h;
        streambuf_type sb;
        buffers_type mb;
        int state = 0;

        template<class DeducedHandler>
        data(DeducedHandler&& h_,
                socket<Stream>& ws_, frame_header& fh_)
            : ws(ws_)
            , fh(fh_)
            , h(std::forward<DeducedHandler>(h_))
        {
        }
    };

    std::shared_ptr<data> d_;

public:
    header_op(header_op&&) = default;
    header_op(header_op const&) = default;

    template<class DeducedHandler, class... Args>
    explicit
    header_op(DeducedHandler&& h, Args&&... args)
        : d_(std::allocate_shared<data>(
            detail::handler_alloc<int, Handler>{h},
                std::forward<DeducedHandler>(h),
                    std::forward<Args>(args)...))
    {
    }

    void operator()()
    {
        using namespace boost::asio;
        auto& d = *d_;
        assert(d.ws.rd_need_ == 0);
        if(d.state == 0 && d.ws.closing_)
        {
            //...
        }
        (*this)(error_code{}, 0);
    }

    void operator()(error_code ec,
        std::size_t bytes_transferred)
    {
        using namespace boost::asio;
        auto& d = *d_;
        //auto const eof =
        //    ec == boost::asio::error::eof;
        //if(eof)
        //    ec = {};
        while(! ec && d.state != 99)
        {
            switch(d.state)
            {
            // read fixed header
            case 0:
            case 1:
                d.state = 2;
                boost::asio::async_read(d.ws.stream_,
                    d.sb.prepare(2), std::move(*this));
                return;

            // got fixed header
            case 2:
            {
                d.sb.commit(bytes_transferred);
                close::value code{};
                auto const n = detail::read_fh1(
                    d.ws.rd_fh_, d.sb, d.ws.role_, code);
                if(code)
                {
                    // protocol error
                    d.state = 8;
                    break;
                }
                // read variable header
                d.state = 3;
                boost::asio::async_read(d.ws.stream_,
                    d.sb.prepare(n), std::move(*this));
                return;
            }

            // got variable header
            case 3:
            {
                d.sb.commit(bytes_transferred);
                close::value code{};
                detail::read_fh2(d.ws.rd_fh_,
                    d.sb, d.ws.role_, code);
                if(! code)
                    d.ws.prepare_fh(code);
                if(code)
                {
                    // protocol error
                    d.state = 8;
                    break;
                }
                if(! detail::is_control(d.ws.rd_fh_.op))
                {
                    // call handler
                    d.state = 99;
                    d.fh = d.ws.rd_fh_;
                    break;
                }
                if(d.ws.rd_fh_.len == 0)
                {
                    // do control
                    d.state = 5;
                    break;
                }
                d.mb = d.sb.prepare(d.ws.rd_fh_.len);
                // read control payload
                d.state = 4;
                boost::asio::async_read(d.ws.stream_,
                    d.mb, std::move(*this));
                return;
            }

            // got control payload
            case 4:
                if(d.ws.rd_fh_.mask)
                    detail::mask_inplace(
                        d.mb, d.ws.rd_key_);
                d.sb.commit(bytes_transferred);
                d.state = 5;
                break;

            // do control
            case 5:
                if(d.ws.rd_fh_.op == opcode::ping)
                {
                    // VFALCO We should avoid a memory
                    // alloc, use char[] here instead?
                    std::string data;
                    close::value code{};
                    detail::read(data, d.sb.data(), code);
                    if(code)
                    {
                        // protocol error
                        d.state = 8;
                        break;
                    }
                    d.sb.reset();
                    d.ws.template write_ping<
                        asio::static_streambuf>(
                            d.sb, opcode::pong, data);
                    d.state = 6;
                    if(d.ws.wr_active_)
                    {
                        // suspend
                        d.ws.wr_invoke_.template emplace<
                            header_op>(std::move(*this));
                        return;
                    }
                    break;
                }
                else if(d.ws.rd_fh_.op == opcode::pong)
                {
                    // VFALCO We should avoid a memory
                    // alloc, use char[] here instead?
                    std::string data;
                    close::value code{};
                    detail::read(data, d.sb.data(), code);
                    if(code)
                    {
                        // protocol error
                        d.state = 8;
                        break;
                    }
                    d.sb.reset();
                    d.ws.template write_ping<
                        asio::static_streambuf>(
                            d.sb, opcode::ping, data);
                    d.state = 6;
                    if(d.ws.wr_active_)
                    {
                        // suspend
                        d.ws.wr_invoke_.template emplace<
                            header_op>(std::move(*this));
                        return;
                    }
                    break;
                }
                assert(d.ws.rd_fh_.op == opcode::close);
                {
                    if(d.ws.closing_)
                    {
                        // call handler
                        d.state = 99;
                        ec = error::closed;
                        break;
                    }
                    // VFALCO We should not use std::string
                    //        in reason_code here
                    reason_code rc;
                    close::value code{};
                    detail::read(rc, d.sb.data(), code);
                    if(code)
                    {
                        rc.code = code;
                        rc.reason = "";
                    }
                    else if(! rc.code)
                    {
                        rc.code = close::normal;
                        rc.reason = "";
                    }
                    else if(! detail::is_valid(*rc.code))
                    {
                        rc.code = close::protocol_error;
                        rc.reason = "";
                    }
                    d.sb.reset();
                    d.ws.template write_close<
                        asio::static_streambuf>(
                            d.sb, *rc.code, rc.reason);
                    d.state = 9;
                    d.ws.closing_ = true;
                    if(d.ws.wr_active_)
                    {
                        // suspend
                        d.ws.wr_invoke_.template emplace<
                            header_op>(std::move(*this));
                        return;
                    }
                    break;
                }

            case 6:
                // write ping/pong
                d.state = 7;
                d.ws.wr_active_ = true;
                boost::asio::async_write(d.ws.stream_,
                    d.sb.data(), std::move(*this));
                return;

            // sent ping/pong
            case 7:
                d.state = 1;
                d.ws.wr_active_ = false;
                d.sb.reset();
                break;

            // do close
            case 8:
                d.sb.reset();
                d.ws.template write_close<
                    asio::static_streambuf>(
                        d.sb, close::protocol_error);
                d.state = 9;
                d.ws.closing_ = true;
                if(d.ws.wr_active_)
                {
                    // suspend
                    d.ws.wr_invoke_.template emplace<
                        header_op>(std::move(*this));
                    return;
                }
                break;

            // send close
            case 9:
                d.state = 10;
                d.ws.wr_active_ = true;
                boost::asio::async_write(d.ws.stream_,
                    d.sb.data(), std::move(*this));
                return;

            case 10:
                d.ws.wr_active_ = false;
                // call handler
                d.state = 99;
                ec = error::closed;
                break;
            }
        }
        d.ws.rd_invoke_.maybe_invoke();
        d.h(ec);
    }

    friend
    auto asio_handler_allocate(
        std::size_t size, header_op* op)
    {
        return boost_asio_handler_alloc_helpers::
            allocate(size, op->d_->h);
    }

    friend
    auto asio_handler_deallocate(
        void* p, std::size_t size, header_op* op)
    {
        return boost_asio_handler_alloc_helpers::
            deallocate(p, size, op->d_->h);
    }

    friend
    auto asio_handler_is_continuation(header_op* op)
    {
        return op->d_->state >= 2 ||
            boost_asio_handler_cont_helpers::
                is_continuation(op->d_->h);
    }

    template <class Function>
    friend
    auto asio_handler_invoke(Function&& f, header_op* op)
    {
        return boost_asio_handler_invoke_helpers::
            invoke(f, op->d_->h);
    }
};

} // wsproto
} // beast

#endif
