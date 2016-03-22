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

#include <beast/asio/static_streambuf.h>

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
        socket<Stream>& ws;
        frame_header& fh;
        Handler h;
        detail::fh_buffer fb; // VFALCO could use buf instead
        typename asio::static_streambuf::mutable_buffers_type mb;
        asio::static_streambuf_n<131> sb;
        int state = 0;

        template<class DeducedHandler>
        data(socket<Stream>& ws_, frame_header& fh_,
                DeducedHandler&& h_)
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

    template<class... Args>
    explicit
    header_op(Args&&... args)
        : d_(std::make_shared<data>(
            std::forward<Args>(args)...))
    {
    }

    void operator()()
    {
        using namespace boost::asio;
        auto& d = *d_;

        if(d.ws.rd_need_ != 0)
            throw std::logic_error("bad read state");
        d.ws.rd_active_ = true;
        (*this)(error_code{}, 0);
    }

    void operator()(error_code ec,
        std::size_t bytes_transferred)
    {
        using namespace boost::asio;
        auto& d = *d_;
        for(;! ec && d.state != 99;)
        {
            switch(d.state)
            {
            // start or repeat
            case 0:
            case 1:
                d.state = 2;
                // read fixed frame header
                boost::asio::async_read(d.ws.stream_,
                    mutable_buffers_1(d.fb.data(), 2),
                        std::move(*this));
                return;

            // got fixed frame header
            case 2:
                d.state = 3;
                // read variable frame header
                boost::asio::async_read(d.ws.stream_,
                    mutable_buffers_1(d.fb.data() + 2,
                        detail::decode_fh1(d.ws.rd_fh_,
                            d.fb)), std::move(*this));
                return;

            // got variable frame header
            case 3:
                ec = detail::decode_fh2(d.ws.rd_fh_, d.fb);
                if(ec)
                    break;
                ec = d.ws.prepare_fh();
                if(ec)
                    break;
                if(! is_control(d.ws.rd_fh_.op))
                {
                    d.fh = d.ws.rd_fh_;
                    // call handler
                    d.state = 99;
                    break;
                }
                if(d.ws.rd_fh_.len == 0)
                {
                    // empty control frame payload
                    d.state = 5;
                    break;
                }
                d.mb = d.sb.prepare(d.ws.rd_fh_.len);
                d.state = 4;
                // read control frame payload
                boost::asio::async_read(d.ws.stream_,
                    d.mb, std::move(*this));
                return;

            // got control frame payload
            case 4:
                if(d.ws.rd_fh_.mask)
                    detail::mask_inplace(
                        d.mb, d.ws.rd_key_);
                d.sb.commit(bytes_transferred);
                d.state = 5;
                break;

            // process control frame
            case 5:
                if(d.ws.rd_fh_.op == opcode::close)
                {
                    if(d.ws.closing_)
                    {
                        ec = error::closed;
                        // call handler
                        break;
                    }
                    d.ws.closing_ = true;
                    reason_code rc;
                    ec = detail::read(rc, d.sb.data());
                    // VFALCO How should we handle the error?
                    if(ec)
                        // call handler
                        break;
                    d.sb.reset();
                    d.ws.template write_close<
                        asio::static_streambuf>(
                            d.sb, rc.code, rc.reason);
                    d.state = 6;
                    if(d.ws.wr_active_)
                    {
                        // suspend
                        d.ws.wr_invoke_.emplace<
                            header_op>(std::move(*this));
                        return;
                    }
                    break;
                }
                else if(d.ws.rd_fh_.op == opcode::ping)
                {
                    std::string data;
                    ec = detail::read(data, d.sb.data());
                    if(ec)
                        break;
                    d.sb.reset();
                    d.ws.template write_ping<
                        asio::static_streambuf>(
                            d.sb, opcode::pong, data);
                    d.state = 8;
                    if(d.ws.wr_active_)
                    {
                        // suspend
                        d.ws.wr_invoke_.emplace<
                            header_op>(std::move(*this));
                        return;
                    }
                    break;
                }
                else if(d.ws.rd_fh_.op == opcode::pong)
                {
                    std::string data;
                    ec = detail::read(data, d.sb.data());
                    if(ec)
                        break;
                    d.sb.reset();
                    d.ws.template write_ping<
                        asio::static_streambuf>(
                            d.sb, opcode::ping, data);
                    d.state = 8;
                    if(d.ws.wr_active_)
                    {
                        // suspend
                        d.ws.wr_invoke_.emplace<
                            header_op>(std::move(*this));
                        return;
                    }
                    break;
                }
                // Will never get here, because the frame
                // header would have failed validation.
                throw std::logic_error("unknown opcode");

            case 6:
                d.state = 7;
                d.ws.wr_active_ = true;
                // write close frame
                boost::asio::async_write(d.ws.stream_,
                    d.sb.data(), std::move(*this));
                return;

            // sent close frame
            case 7:
                d.ws.wr_active_ = false;
                ec = error::closed;
                // call handler
                break;

            case 8:
                d.state = 9;
                d.ws.wr_active_ = true;
                // write pong frame
                boost::asio::async_write(d.ws.stream_,
                    d.sb.data(), std::move(*this));
                return;

            // sent ping/pong
            case 9:
                d.ws.wr_active_ = false;
                d.state = 1;
                break;
            }
        }
        d.ws.rd_active_ = false;
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
