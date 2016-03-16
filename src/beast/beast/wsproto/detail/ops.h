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

#ifndef BEAST_WSPROTO_DETAIL_OPS_H_INCLUDED
#define BEAST_WSPROTO_DETAIL_OPS_H_INCLUDED

#include <beast/wsproto/stream.h>
#include <beast/wsproto/detail/frame.h>
#include <beast/asio/append_buffers.h>
#include <beast/asio/streambuf.h>
#include <beast/http/parser.h>
#include <beast/is_call_possible.h>
#include <boost/asio.hpp>
#include <boost/asio/detail/handler_alloc_helpers.hpp>
#include <boost/asio/detail/handler_cont_helpers.hpp>
#include <boost/asio/detail/handler_invoke_helpers.hpp>
#include <boost/optional.hpp>
#include <memory>

namespace beast {
namespace wsproto {
namespace detail {

#if 0
template<class Op>
class async_wrapper
{
    struct State
    {
        Op op;
        bool cont = false;

        template<class... Args>
        State(Args&&... args)
            : op(std::forward<Args>(args)...)
        {
        }
    };

    std::shared_ptr<State> s_;

public:
    async_wrapper(async_wrapper&&) = default;
    async_wrapper(async_wrapper const&) = default;

    template<class... Args>
    explicit
    async_wrapper(Args&&... args)
        : s_(std::make_shared<State>(
            std::forward<Args>(args)...))
    {
    }

    template<class... Args>
    void operator()()
    {
        s_->cont = true;
        s_->op(std::move(*this));
    }

    template<class... Args>
    void operator()(Args&&... args)
    {
        s_->cont = true;
        s_->op(std::move(*this),
            std::forward<Args>(args)...);
    }

    friend
    auto asio_handler_allocate(
        std::size_t size, async_wrapper* w)
    {
        return boost_asio_handler_alloc_helpers::
            allocate(size, w->s_->op.h);
    }

    friend
    auto asio_handler_deallocate(
        void* p, std::size_t size, async_wrapper* w)
    {
        return boost_asio_handler_alloc_helpers::
            deallocate(p, size, w->s_->op.h);
    }

    friend
    auto asio_handler_is_continuation(async_wrapper* w)
    {
        return w->s_->cont ||
            boost_asio_handler_cont_helpers::
                is_continuation(w->s_->op.h);
    }

    template <class Function>
    friend
    auto asio_handler_invoke(
        Function&& f, async_wrapper* w)
    {
        return boost_asio_handler_invoke_helpers::
            invoke(f, w->s_->op.h);
    }
};

// write a frame
template<class Stream, class ConstBuffers, class Handler>
struct write_plain_frame_op
{
    Stream& s_;
    frame_header fh_;
    ConstBuffers bs_;
    fh_buffer fb_;
    boost::asio::const_buffers_1 b_;

public:
    Handler h;

    template<class DeducedConstBuffers, class DeducedHandler>
    write_plain_frame_op(Stream& s, frame_header const& fh,
            DeducedConstBuffers&& bs, DeducedHandler&& h_)
        : s_(s)
        , fh_(fh)
        , bs_(std::forward<DeducedConstBuffers>(bs))
        , h(std::forward<DeducedHandler>(h_))
    {
        b_ = encode_fh(fb_, fh);
    }

    Handler&
    handler()
    {
        return h;
    }

    template<class Op>
    void
    operator()(Op&& op)
    {
        using namespace boost::asio;
        async_write(s_,
            beast::asio::append_buffers(
                b_, bs_), std::move(*this));
    }

    template<class Op>
    void
    operator()(Op&& op, error_code const& ec,
        std::size_t bytes_transferred)
    {
        h_(ec, std::move(fh_));
    }
};

template<class Stream, class ConstBuffers, class Handler>
void
async_write_plain_frame(Stream& stream,
    frame_header const& fh, ConstBuffers&& bs,
        Handler&& h)
{
#if 0
    async_wrapper<write_plain_frame_op<
        std::decay_t<Stream>
#endif
}
#endif

//------------------------------------------------------------------------------

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
        using namespace boost::asio;
        async_write(d_->s, d_->sb.data(),
            std::move(*this));
    }

    void operator()(error_code const& ec,
        std::size_t)
    {
        return d_->s.get_io_service().wrap(
            std::move(d_->h))(ec ? ec : d_->ec_final);
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
        return boost_asio_handler_cont_helpers::
            is_continuation(op->d_->h);
    }

    template <class Function>
    friend
    auto asio_handler_invoke(Function&& f, streambuf_op* op)
    {
        return boost_asio_handler_invoke_helpers::
            invoke(f, op->d_->h);
    }
};

//------------------------------------------------------------------------------

// read a frame header
template<class Stream, class Handler>
class read_fh_op
{
    struct data
    {
        Stream& s;
        frame_state& fs;
        frame_header& fh;
        Handler h;
        int state = 0;
        fh_buffer buf;

        template<class DeducedHandler>
        data(Stream& s_, frame_state& fs_,
                frame_header& fh_, DeducedHandler&& h_)
            : s(s_)
            , fs(fs_)
            , fh(fh_)
            , h(std::forward<Handler>(h_))
        {
        }
    };

    std::shared_ptr<data> d_;

public:
    read_fh_op(read_fh_op&&) = default;
    read_fh_op(read_fh_op const&) = default;

    template<class... Args>
    explicit
    read_fh_op(Args&&... args)
        : d_(std::make_shared<data>(
            std::forward<Args>(args)...))
    {
        using namespace boost::asio;
        async_read(d_->s, mutable_buffers_1(
            d_->buf.data(), 2), std::move(*this));
    }

    void operator()(error_code ec,
        std::size_t bytes_transferred)
    {
        using namespace boost::asio;
        if(! ec)
        {
            if(d_->state == 0)
            {
                d_->state = 1;
                async_read(d_->s, mutable_buffers_1(
                    d_->buf.data() + 2, detail::decode_fh1(
                        d_->fh, d_->buf)), std::move(*this));
                return;
            }
            ec = detail::decode_fh2(d_->fh, d_->buf);
            if(! ec)
                ec = update_frame_state(d_->fs, d_->fh);
        }
        d_->h(ec);
    }

    friend
    auto asio_handler_allocate(
        std::size_t size, read_fh_op* op)
    {
        return boost_asio_handler_alloc_helpers::
            allocate(size, op->d_->h);
    }

    friend
    auto asio_handler_deallocate(
        void* p, std::size_t size, read_fh_op* op)
    {
        return boost_asio_handler_alloc_helpers::
            deallocate(p, size, op->d_->h);
    }

    friend
    auto asio_handler_is_continuation(read_fh_op* op)
    {
        return (op->d_->state != 0) ? true :
            boost_asio_handler_cont_helpers::
                is_continuation(op->d_->h);
    }

    template <class Function>
    friend
    auto asio_handler_invoke(Function&& f, read_fh_op* op)
    {
        return boost_asio_handler_invoke_helpers::
            invoke(f, op->d_->h);
    }
};

//------------------------------------------------------------------------------

// read a frame body
template<class Stream,
    class MutableBuffers, class Handler>
struct read_op
{
    struct data
    {
        Stream& stream;
        frame_header fh;
        MutableBuffers b;
        Handler h;
        beast::asio::streambuf sb;

        template<class DeducedBuffers, class DeducedHandler>
        data(Stream& stream_, frame_header const& fh_,
                DeducedBuffers&& b_, DeducedHandler&& h_)
            : stream(stream_)
            , fh(fh_)
            , b(std::forward<DeducedBuffers>(b_))
            , h(std::forward<DeducedHandler>(h_))
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
        using namespace boost::asio;
        if(d_->fh.mask)
            async_read(d_->stream,
                d_->sb.prepare(d_->fh.len),
                    std::move(*this));
        else
            async_read(d_->stream,
                d_->b, std::move(*this));
    }

    void operator()(error_code const& ec,
        std::size_t bytes_transferred)
    {
        using namespace boost::asio;
        if(! ec)
        {
            if(d_->fh.mask)
            {
                d_->sb.commit(bytes_transferred);
                mask_and_copy(d_->b, d_->sb.data(),
                    d_->fh.key);
            }
        }
        d_->h(ec, std::move(d_->fh), bytes_transferred);
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
        return boost_asio_handler_cont_helpers::
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

//------------------------------------------------------------------------------

// write a frame
template<class Stream, class Handler>
struct write_op
{
    struct data
    {
        Stream& s;
        frame_header fh;
        Handler h;
        beast::asio::streambuf sb;

        template<class ConstBuffers, class DeducedHandler>
        data(Stream& s_, frame_header const& fh_,
                ConstBuffers const& b, DeducedHandler&& h_)
            : s(s_)
            , fh(fh_)
            , h(std::forward<DeducedHandler>(h_))
        {
            write_fh(sb, fh);
            write_body(sb, fh, b);
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
        using namespace boost::asio;
        async_write(d_->s, d_->sb.data(),
            std::move(*this));
    }

    void operator()(error_code const& ec,
        std::size_t bytes_transferred)
    {
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
        return boost_asio_handler_cont_helpers::
            is_continuation(op->d_->h);
    }

    template <class Function>
    friend
    auto asio_handler_invoke(Function&& f, write_op* op)
    {
        return boost_asio_handler_invoke_helpers::
            invoke(f, op->d_->h);
    }
};

} // detail
} // wsproto
} // beast

#endif
