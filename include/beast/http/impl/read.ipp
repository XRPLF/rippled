//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_HTTP_IMPL_READ_IPP_HPP
#define BEAST_HTTP_IMPL_READ_IPP_HPP

#include <beast/http/parser_v1.hpp>
#include <beast/core/bind_handler.hpp>
#include <beast/core/handler_alloc.hpp>
#include <beast/core/stream_concepts.hpp>
#include <cassert>

namespace beast {
namespace http {

namespace detail {

template<class Stream, class Streambuf,
    bool isRequest, class Body, class Headers,
        class Handler>
class read_op
{
    using alloc_type =
        handler_alloc<char, Handler>;

    using parser_type =
        parser_v1<isRequest, Body, Headers>;

    using message_type =
        message_v1<isRequest, Body, Headers>;

    struct data
    {
        Stream& s;
        Streambuf& sb;
        message_type& m;
        parser_type p;
        Handler h;
        bool started = false;
        bool cont;
        int state = 0;

        template<class DeducedHandler>
        data(DeducedHandler&& h_, Stream& s_,
                Streambuf& sb_, message_type& m_)
            : s(s_)
            , sb(sb_)
            , m(m_)
            , h(std::forward<DeducedHandler>(h_))
            , cont(boost_asio_handler_cont_helpers::
                is_continuation(h))
        {
        }
    };

    std::shared_ptr<data> d_;

public:
    read_op(read_op&&) = default;
    read_op(read_op const&) = default;

    template<class DeducedHandler, class... Args>
    read_op(DeducedHandler&& h, Stream& s, Args&&... args)
        : d_(std::allocate_shared<data>(alloc_type{h},
            std::forward<DeducedHandler>(h), s,
                std::forward<Args>(args)...))
    {
        (*this)(error_code{}, 0, false);
    }

    void
    operator()(error_code ec,
        std::size_t bytes_transferred, bool again = true);

    friend
    void* asio_handler_allocate(
        std::size_t size, read_op* op)
    {
        return boost_asio_handler_alloc_helpers::
            allocate(size, op->d_->h);
    }

    friend
    void asio_handler_deallocate(
        void* p, std::size_t size, read_op* op)
    {
        return boost_asio_handler_alloc_helpers::
            deallocate(p, size, op->d_->h);
    }

    friend
    bool asio_handler_is_continuation(read_op* op)
    {
        return op->d_->cont;
    }

    template <class Function>
    friend
    void asio_handler_invoke(Function&& f, read_op* op)
    {
        return boost_asio_handler_invoke_helpers::
            invoke(f, op->d_->h);
    }
};

template<class Stream, class Streambuf,
    bool isRequest, class Body, class Headers,
        class Handler>
void
read_op<Stream, Streambuf, isRequest, Body, Headers, Handler>::
operator()(error_code ec, std::size_t bytes_transferred, bool again)
{
    auto& d = *d_;
    d.cont = d.cont || again;
    while(d.state != 99)
    {
        switch(d.state)
        {
        case 0:
        {
            auto const used =
                d.p.write(d.sb.data(), ec);
            if(ec)
            {
                // call handler
                d.state = 99;
                d.s.get_io_service().post(
                    bind_handler(std::move(*this), ec, 0));
                return;
            }
            if(used > 0)
                d.started = true;
            d.sb.consume(used);
            if(d.p.complete())
            {
                // call handler
                d.state = 99;
                d.m = d.p.release();
                d.s.get_io_service().post(
                    bind_handler(std::move(*this), ec, 0));
                return;
            }
            d.state = 1;
            break;
        }

        case 1:
            // read
            d.state = 2;
            d.s.async_read_some(d.sb.prepare(
                read_size_helper(d.sb, 65536)),
                    std::move(*this));
            return;

        // got data
        case 2:
        {
            if(ec == boost::asio::error::eof)
            {
                if(! d.started)
                {
                    // call handler
                    d.state = 99;
                    break;
                }
                // Caller will see eof on next read.
                ec = {};
                d.p.write_eof(ec);
                if(! ec)
                {
                    assert(d.p.complete());
                    d.m = d.p.release();
                }
                // call handler
                d.state = 99;
                break;
            }
            if(ec)
            {
                // call handler
                d.state = 99;
                break;
            }
            d.sb.commit(bytes_transferred);
            d.sb.consume(d.p.write(d.sb.data(), ec));
            if(ec)
            {
                // call handler
                d.state = 99;
                break;
            }
            if(d.p.complete())
            {
                // call handler
                d.state = 99;
                d.m = d.p.release();
                break;
            }
            d.state = 1;
            break;
        }
        }
    }
    d.h(ec);
}

} // detail

//------------------------------------------------------------------------------

template<class SyncReadStream, class Streambuf,
    bool isRequest, class Body, class Headers>
void
read(SyncReadStream& stream, Streambuf& streambuf,
    message_v1<isRequest, Body, Headers>& msg)
{
    error_code ec;
    read(stream, streambuf, msg, ec);
    if(ec)
        throw system_error{ec};
}

template<class SyncReadStream, class Streambuf,
    bool isRequest, class Body, class Headers>
void
read(SyncReadStream& stream, Streambuf& streambuf,
    message_v1<isRequest, Body, Headers>& m,
        error_code& ec)
{
    static_assert(is_SyncReadStream<SyncReadStream>::value,
        "SyncReadStream requirements not met");
    static_assert(is_Streambuf<Streambuf>::value,
        "Streambuf requirements not met");
    parser_v1<isRequest, Body, Headers> p;
    bool started = false;
    for(;;)
    {
        auto used =
            p.write(streambuf.data(), ec);
        if(ec)
            return;
        streambuf.consume(used);
        if(used > 0)
            started = true;
        if(p.complete())
        {
            m = p.release();
            break;
        }
        streambuf.commit(stream.read_some(
            streambuf.prepare(read_size_helper(
                streambuf, 65536)), ec));
        if(ec && ec != boost::asio::error::eof)
            return;
        if(ec == boost::asio::error::eof)
        {
            if(! started)
                return;
            // Caller will see eof on next read.
            ec = {};
            p.write_eof(ec);
            if(ec)
                return;
            assert(p.complete());
            m = p.release();
            break;
        }
    }
}

template<class AsyncReadStream, class Streambuf,
    bool isRequest, class Body, class Headers,
        class ReadHandler>
typename async_completion<
    ReadHandler, void(error_code)>::result_type
async_read(AsyncReadStream& stream, Streambuf& streambuf,
    message_v1<isRequest, Body, Headers>& m,
        ReadHandler&& handler)
{
    static_assert(is_AsyncReadStream<AsyncReadStream>::value,
        "AsyncReadStream requirements not met");
    static_assert(is_Streambuf<Streambuf>::value,
        "Streambuf requirements not met");
    beast::async_completion<ReadHandler,
        void(error_code)> completion(handler);
    detail::read_op<AsyncReadStream, Streambuf,
        isRequest, Body, Headers, decltype(
            completion.handler)>{completion.handler,
                stream, streambuf, m};
    return completion.result.get();
}

} // http
} // beast

#endif
