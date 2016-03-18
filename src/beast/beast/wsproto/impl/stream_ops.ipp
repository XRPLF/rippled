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

#ifndef BEAST_WSPROTO_IMPL_STREAM_OPS_H_INCLUDED
#define BEAST_WSPROTO_IMPL_STREAM_OPS_H_INCLUDED

#include <beast/wsproto/detail/frame.h>
#include <beast/asio/append_buffers.h>
#include <beast/asio/clip_buffers.h>
#include <beast/asio/streambuf.h>
#include <beast/http/parser.h>
#include <beast/is_call_possible.h>
#include <boost/asio.hpp>
#include <boost/asio/detail/handler_alloc_helpers.hpp>
#include <boost/asio/detail/handler_cont_helpers.hpp>
#include <boost/asio/detail/handler_invoke_helpers.hpp>
#include <boost/optional.hpp>
#include <array>
#include <memory>
#include <stdexcept>

namespace beast {
namespace wsproto {
namespace detail {

// send the entire contents of a streambuf
template<class Stream,
    class Streambuf, class Handler>
class streambuf_op
{
    struct data
    {
        Stream& s;
        Streambuf sb;
        error_code ec_final;
        Handler h;
        bool cont = false;

        template<class DeducedHandler>
        data(Stream& s_, Streambuf&& sb_,
            error_code const& ec_final_,
                DeducedHandler&& h_)
            : s(s_)
            , sb(std::move(sb_))
            , ec_final(ec_final_)
            , h(std::forward<Handler>(h_))
        {
        }
    };

    std::shared_ptr<data> d_;

public:
    streambuf_op(streambuf_op&&) = default;
    streambuf_op(streambuf_op const&) = default;

    template<class... Args>
    explicit
    streambuf_op(Args&&... args)
        : d_(std::make_shared<data>(
            std::forward<Args>(args)...))
    {
    }

    void operator()()
    {
        boost::asio::async_write(d_->s,
            d_->sb.data(), std::move(*this));
    }

    void operator()(error_code const& ec, std::size_t)
    {
        d_->cont = true;
        d_->h(ec ? ec : d_->ec_final);
    }

    friend
    auto asio_handler_allocate(
        std::size_t size, streambuf_op* op)
    {
        return boost_asio_handler_alloc_helpers::
            allocate(size, op->d_->h);
    }

    friend
    auto asio_handler_deallocate(
        void* p, std::size_t size, streambuf_op* op)
    {
        return boost_asio_handler_alloc_helpers::
            deallocate(p, size, op->d_->h);
    }

    friend
    auto asio_handler_is_continuation(streambuf_op* op)
    {
        return op->d_->cont ||
            boost_asio_handler_invoke_helpers::
                invoke(f, op->d_->h);
    }

    template <class Function>
    friend
    auto asio_handler_invoke(Function&& f, streambuf_op* op)
    {
        return boost_asio_handler_invoke_helpers::
            invoke(f, op->d_->h);
    }
};

} // detail

//------------------------------------------------------------------------------

// Multi-function composed read operation.
// Reads a frame header, or some payload data.
// Handles control frames.
//
template<class Stream>
template<class Buffers, class Handler>
class stream<Stream>::read_op
{
    struct data
    {
        stream<Stream>& ws;
        Buffers bs;
        Handler h;
        frame_header* fh;
        int state = 0;
        detail::fh_buffer fb;
        //std::array<std::uint8_t, 125> cbuf;
        //boost::asio::mutable_buffers_1 cbs;

        template<class DeducedBuffers, class DeducedHandler>
        data(stream<Stream>& ws_,
                DeducedBuffers&& bs_, DeducedHandler&& h_)
            : ws(ws_)
            , bs(std::forward<DeducedBuffers>(bs_))
            , h(std::forward<DeducedHandler>(h_))
            , fh(nullptr)
        {
        }

        template<class DeducedHandler>
        data(stream<Stream>& ws_, frame_header& fh_,
                DeducedHandler&& h_)
            : ws(ws_)
            , h(std::forward<DeducedHandler>(h_))
            , fh(&fh_)
        {
        }
    };

    std::shared_ptr<data> d_;

public:
    read_op(read_op&&) = default;
    read_op(read_op const&) = default;

    template<class... Args>
    explicit
    read_op(Args&&... args)
        : d_(std::make_shared<data>(
            std::forward<Args>(args)...))
    {
    }

    void operator()()
    {
        using namespace boost::asio;
        if(d_->ws.rs_.need == 0)
            // read fixed part of frame header
            return boost::asio::async_read(d_->ws.stream_,
                mutable_buffers_1(d_->fb.data(), 2),
                    std::move(*this));
        if(d_->fh)
            throw std::logic_error("bad read state");
        d_->state = 2;
        (*this)(error_code{}, 0);
    }

    void operator()(error_code ec,
        std::size_t bytes_transferred)
    {
        using namespace boost::asio;
        if(! ec)
        {
            switch(d_->state)
            {
            // got fixed frame header data
            case 0:
                d_->state = 1;
                return boost::asio::async_read(d_->ws.stream_,
                    mutable_buffers_1(d_->fb.data() + 2,
                        detail::decode_fh1(d_->ws.rs_.fh,
                            d_->fb)), std::move(*this));

            // got variable frame header data
            case 1:
                if((ec = detail::decode_fh2(
                        d_->ws.rs_.fh, d_->fb)))
                    break;
                if((ec = d_->ws.process_fh()))
                    break;
#if 0
                if(d_->fh)
                    // caller wants just the frame header
                    d_->h(ec);
                    return;
#endif
                d_->state = 2;
                return boost::asio::async_read(d_->ws.stream_,
                    asio::clip_buffers(d_->ws.rs_.need, d_->bs),
                        std::move(*this));

            // got frame payload data
            case 2:
                if(bytes_transferred > d_->ws.rs_.need)
                    throw std::logic_error("extra data");
                d_->ws.rs_.need -= bytes_transferred;
                if(d_->ws.rs_.need == 0 && d_->ws.rs_.fh.fin)
                    ec = error::eom;
                if(d_->ws.rs_.fh.mask)
                    detail::mask_inplace(d_->bs,
                        d_->ws.rs_.key);

                break;
            }
        }
        //d_->h(ec, bytes_transferred);
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
        return op->d_->state ||
            boost_asio_handler_invoke_helpers::
                invoke(f, op->d_->h);
    }

    template <class Function>
    friend
    auto asio_handler_invoke(Function&& f, read_op* op)
    {
        return boost_asio_handler_invoke_helpers::
            invoke(f, op->d_->h);
    }
};

//------------------------------------------------------------------------------

// write a frame
template<class Stream>
template<class Handler>
class stream<Stream>::write_op
{
    struct data
    {
        stream<Stream>& ws;
        frame_header fh;
        Handler h;
        beast::asio::streambuf sb;
        bool cont = false;

        template<class ConstBuffers, class DeducedHandler>
        data(stream<Stream>& ws_, frame_header const& fh_,
                ConstBuffers const& b, DeducedHandler&& h_)
            : ws(ws_)
            , fh(fh_)
            , h(std::forward<DeducedHandler>(h_))
        {
            detail::write_fh(sb, fh);
            detail::write_body(sb, fh, b);
        }
    };

    std::shared_ptr<data> d_;

public:
    write_op(write_op&&) = default;
    write_op(write_op const&) = default;

    template<class...Args>
    explicit
    write_op(Args&&... args)
        : d_(std::make_shared<data>(
            std::forward<Args>(args)...))
    {
    }

    void operator()()
    {
        boost::asio::async_write(
            d_->ws.stream_, d_->sb.data(),
                std::move(*this));
    }

    void operator()(error_code const& ec,
        std::size_t bytes_transferred)
    {
        d_->cont = true;
        d_->h(ec);
    }

    friend
    auto asio_handler_allocate(
        std::size_t size, write_op* op)
    {
        return boost_asio_handler_alloc_helpers::
            allocate(size, op->d_->h);
    }

    friend
    auto asio_handler_deallocate(
        void* p, std::size_t size, write_op* op)
    {
        return boost_asio_handler_alloc_helpers::
            deallocate(p, size, op->d_->h);
    }

    friend
    auto asio_handler_is_continuation(write_op* op)
    {
        return op->d_->cont ||
            boost_asio_handler_invoke_helpers::
                invoke(f, op->d_->h);
    }

    template <class Function>
    friend
    auto asio_handler_invoke(Function&& f, write_op* op)
    {
        return boost_asio_handler_invoke_helpers::
            invoke(f, op->d_->h);
    }
};

//------------------------------------------------------------------------------

namespace detail {

// read an entire message
template<class Stream, class Streambuf, class Handler>
class read_msg_op
{
    struct data
    {
        stream<Stream>& ws;
        Streambuf& sb;
        Handler h;
        frame_header fh;
        bool cont = false;

        template<class DeducedHandler>
        data(stream<Stream>& ws_, Streambuf& sb_,
                DeducedHandler&& h_)
            : ws(ws_)
            , sb(sb_)
            , h(std::forward<DeducedHandler>(h_))
        {
        }
    };

    std::shared_ptr<data> d_;

public:
    read_msg_op(read_msg_op&&) = default;
    read_msg_op(read_msg_op const&) = default;

    template<class... Args>
    explicit
    read_msg_op(Args&&... args)
        : d_(std::make_shared<data>(
            std::forward<Args>(args)...))
    {
    }

    void operator()()
    {
        d_->ws.async_read_fh(d_->fh,
            std::move(*this));
    }

    void operator()(error_code ec)
    {
        d_->cont = true;
        if(! ec)
            return d_->ws.async_read_some(
                d_->sb.prepare(d_->ws.rs_.fh.len),
                    std::move(*this));
        d_->h(ec);
    }

    void operator()(error_code ec,
        std::size_t bytes_transferred)
    {
        using namespace boost::asio;
        auto const eom = ec == error::eom;
        if(! ec || eom)
        {
            d_->sb.commit(bytes_transferred);
            if(! eom)
                return d_->ws.async_read_fh(
                    d_->ws.rs_.fh, std::move(*this));
            ec = {};
        }
        d_->h(ec);
    }

    friend
    auto asio_handler_allocate(
        std::size_t size, read_msg_op* op)
    {
        return boost_asio_handler_alloc_helpers::
            allocate(size, op->d_->h);
    }

    friend
    auto asio_handler_deallocate(
        void* p, std::size_t size, read_msg_op* op)
    {
        return boost_asio_handler_alloc_helpers::
            deallocate(p, size, op->d_->h);
    }

    friend
    auto asio_handler_is_continuation(read_msg_op* op)
    {
        return op->d_->cont ||
            boost_asio_handler_invoke_helpers::
                invoke(f, op->d_->h);
    }

    template <class Function>
    friend
    auto asio_handler_invoke(Function&& f, read_msg_op* op)
    {
        return boost_asio_handler_invoke_helpers::
            invoke(f, op->d_->h);
    }
};

} // detail

} // wsproto
} // beast

#endif
