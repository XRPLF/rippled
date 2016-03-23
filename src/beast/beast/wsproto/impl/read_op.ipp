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

#ifndef BEAST_WSPROTO_READ_OP_H_INCLUDED
#define BEAST_WSPROTO_READ_OP_H_INCLUDED

#include <beast/wsproto/detail/handler_alloc.h>
#include <beast/asio/buffers_adapter.h>
#include <beast/asio/prepare_buffers.h>
#include <cassert>

#include <beast/unit_test/suite.h>
#include <iomanip>

namespace beast {
namespace wsproto {

template<class = void>
std::string
to_hex(boost::asio::const_buffer b)
{
    using namespace boost::asio;
    std::stringstream ss;
    auto p = buffer_cast<std::uint8_t const*>(b);
    auto n = buffer_size(b);
    while(n--)
    {
        ss <<
            std::setfill('0') <<
            std::setw(2) <<
            std::hex << int(*p++) << " ";
    }
    return ss.str();
}

template<class Buffers>
std::string
to_hex(Buffers const& bs)
{
    std::string s;
    for(auto const& b : bs)
        s.append(to_hex(boost::asio::const_buffer(b)));
    return s;
}

inline
std::string
format(std::string s)
{
    auto const w = 84;
    for(int n = w*(s.size()/w); n>0; n-=w)
        s.insert(n, 1, '\n');
    return s;
}

// read non-control payload
//
template<class Stream>
template<class Buffers, class Handler>
class socket<Stream>::read_op
{
    using adapter_type =
        asio::buffers_adapter<Buffers const&>;

    using streambuf_type =
        asio::static_streambuf_n<139>;

    struct data
    {
        socket<Stream>& ws;
        Buffers bs;
        Handler h;
        adapter_type ba;
        streambuf_type sb;
        std::size_t bytes = 0;
        typename adapter_type::mutable_buffers_type bamb;
        typename streambuf_type::mutable_buffers_type sbmb;
        int state = 0;

        template<class DeducedHandler, class DeducedBuffers>
        data(DeducedHandler&& h_, socket<Stream>& ws_,
                DeducedBuffers&& bs_)
            : ws(ws_)
            , bs(std::forward<DeducedBuffers>(bs_))
            , h(std::forward<DeducedHandler>(h_))
            , ba(bs)
        {
        }
    };

    std::shared_ptr<data> d_;

public:
    read_op(read_op&&) = default;
    read_op(read_op const&) = default;

    template<class DeducedHandler, class... Args>
    explicit
    read_op(DeducedHandler&& h, Args&&... args)
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
        assert(d.state != 0 || d.ws.rd_need_ != 0);
        d.state = 0;
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
            // read payload
            case 0:
            case 1:
                d.bamb = d.ba.prepare(std::min(
                    d.ws.rd_need_, d.ba.max_size()));
                d.state = 2;
                d.ws.stream_.async_read_some(
                    d.bamb, std::move(*this));
                return;

            // got payload
            case 2:
                if(d.ws.rd_fh_.mask)
                    detail::mask_inplace(asio::prepare_buffers(
                        bytes_transferred, d.bamb), d.ws.rd_key_);
                d.bytes += bytes_transferred;
                d.ba.commit(bytes_transferred);
                d.ws.rd_need_ -= bytes_transferred;
                if(d.ws.rd_op_ == opcode::text)
                {
                    if(! d.ws.rd_utf8_check_.write(d.ba.data()) ||
                        (d.ws.rd_need_ == 0 && d.ws.rd_fh_.fin &&
                            ! d.ws.rd_utf8_check_.finish()))
                    {
                        // write close
                        d.ws.template write_close<
                            asio::static_streambuf>(
                                d.sb, close::bad_payload);
                        d.state = 3;
                        d.ws.closing_ = true;
                        if(d.ws.wr_active_)
                        {
                            // suspend
                            d.ws.wr_invoke_.template emplace<
                                read_op>(std::move(*this));
                            return;
                        }
                        break;
                    }
                }
                d.ba.consume(bytes_transferred);
                if(d.ws.rd_need_ > 0 && d.ba.max_size() > 0)
                {
                    d.state = 1;
                    break;
                }
                // call handler
                d.state = 99;
                bytes_transferred = d.bytes;
                break;

            // send close (closing)
            case 3:
                d.state = 4;
                d.ws.wr_active_ = true;
                boost::asio::async_write(d.ws.stream_,
                    d.sb.data(), std::move(*this));
                return;

            // sent close (closing)
            case 4:
                d.ws.wr_active_ = false;
                // call handler
                ec = error::closed;
                break;
            }
        }
        d.ws.rd_invoke_.maybe_invoke();
        d.h(ec, bytes_transferred);
    }

    friend
    auto asio_handler_allocate(
        std::size_t size, read_op* op)
    {
        return boost_asio_handler_alloc_helpers::
            allocate(size, op->d_->h);
    }

    friend
    auto asio_handler_deallocate(
        void* p, std::size_t size, read_op* op)
    {
        return boost_asio_handler_alloc_helpers::
            deallocate(p, size, op->d_->h);
    }

    friend
    auto asio_handler_is_continuation(read_op* op)
    {
        return op->d_->state >= 1 ||
            boost_asio_handler_cont_helpers::
                is_continuation(op->d_->h);
    }

    template <class Function>
    friend
    auto asio_handler_invoke(Function&& f, read_op* op)
    {
        return boost_asio_handler_invoke_helpers::
            invoke(f, op->d_->h);
    }
};

} // wsproto
} // beast

#endif
