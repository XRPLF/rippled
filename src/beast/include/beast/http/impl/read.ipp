//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_HTTP_IMPL_READ_IPP_HPP
#define BEAST_HTTP_IMPL_READ_IPP_HPP

#include <beast/http/concepts.hpp>
#include <beast/http/parser_v1.hpp>
#include <beast/core/bind_handler.hpp>
#include <beast/core/handler_alloc.hpp>
#include <beast/core/stream_concepts.hpp>
#include <cassert>

namespace beast {
namespace http {

namespace detail {

template<class Stream,
    class DynamicBuffer, class Parser, class Handler>
class parse_op
{
    using alloc_type =
        handler_alloc<char, Handler>;

    struct data
    {
        Stream& s;
        DynamicBuffer& db;
        Parser& p;
        Handler h;
        bool started = false;
        bool cont;
        int state = 0;

        template<class DeducedHandler>
        data(DeducedHandler&& h_, Stream& s_,
                DynamicBuffer& sb_, Parser& p_)
            : s(s_)
            , db(sb_)
            , p(p_)
            , h(std::forward<DeducedHandler>(h_))
            , cont(boost_asio_handler_cont_helpers::
                is_continuation(h))
        {
        }
    };

    std::shared_ptr<data> d_;

public:
    parse_op(parse_op&&) = default;
    parse_op(parse_op const&) = default;

    template<class DeducedHandler, class... Args>
    parse_op(DeducedHandler&& h, Stream& s, Args&&... args)
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
        std::size_t size, parse_op* op)
    {
        return boost_asio_handler_alloc_helpers::
            allocate(size, op->d_->h);
    }

    friend
    void asio_handler_deallocate(
        void* p, std::size_t size, parse_op* op)
    {
        return boost_asio_handler_alloc_helpers::
            deallocate(p, size, op->d_->h);
    }

    friend
    bool asio_handler_is_continuation(parse_op* op)
    {
        return op->d_->cont;
    }

    template<class Function>
    friend
    void asio_handler_invoke(Function&& f, parse_op* op)
    {
        return boost_asio_handler_invoke_helpers::
            invoke(f, op->d_->h);
    }
};

template<class Stream,
    class DynamicBuffer, class Parser, class Handler>
void
parse_op<Stream, DynamicBuffer, Parser, Handler>::
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
                d.p.write(d.db.data(), ec);
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
            d.db.consume(used);
            if(d.p.complete())
            {
                // call handler
                d.state = 99;
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
            d.s.async_read_some(d.db.prepare(
                read_size_helper(d.db, 65536)),
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
                assert(ec || d.p.complete());
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
            d.db.commit(bytes_transferred);
            auto const used = d.p.write(d.db.data(), ec);
            if(ec)
            {
                // call handler
                d.state = 99;
                break;
            }
            if(used > 0)
                d.started = true;
            d.db.consume(used);
            if(d.p.complete())
            {
                // call handler
                d.state = 99;
                break;
            }
            d.state = 1;
            break;
        }
        }
    }
    d.h(ec);
}

//------------------------------------------------------------------------------

template<class Stream, class DynamicBuffer,
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
        DynamicBuffer& db;
        message_type& m;
        parser_type p;
        Handler h;
        bool started = false;
        bool cont;
        int state = 0;

        template<class DeducedHandler>
        data(DeducedHandler&& h_, Stream& s_,
                DynamicBuffer& sb_, message_type& m_)
            : s(s_)
            , db(sb_)
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
        (*this)(error_code{}, false);
    }

    void
    operator()(error_code ec, bool again = true);

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

    template<class Function>
    friend
    void asio_handler_invoke(Function&& f, read_op* op)
    {
        return boost_asio_handler_invoke_helpers::
            invoke(f, op->d_->h);
    }
};

template<class Stream, class DynamicBuffer,
    bool isRequest, class Body, class Headers,
        class Handler>
void
read_op<Stream, DynamicBuffer, isRequest, Body, Headers, Handler>::
operator()(error_code ec, bool again)
{
    auto& d = *d_;
    d.cont = d.cont || again;
    while(! ec && d.state != 99)
    {
        switch(d.state)
        {
        case 0:
            d.state = 1;
            async_parse(d.s, d.db, d.p, std::move(*this));
            return;

        case 1:
            // call handler
            d.state = 99;
            d.m = d.p.release();
            break;
        }
    }
    d.h(ec);
}

} // detail

//------------------------------------------------------------------------------

template<class SyncReadStream, class DynamicBuffer, class Parser>
void
parse(SyncReadStream& stream,
    DynamicBuffer& dynabuf, Parser& parser)
{
    static_assert(is_SyncReadStream<SyncReadStream>::value,
        "SyncReadStream requirements not met");
    static_assert(is_DynamicBuffer<DynamicBuffer>::value,
        "DynamicBuffer requirements not met");
    static_assert(is_Parser<Parser>::value,
        "Parser requirements not met");
    error_code ec;
    parse(stream, dynabuf, parser, ec);
    if(ec)
        throw boost::system::system_error{ec};
}

template<class SyncReadStream, class DynamicBuffer, class Parser>
void
parse(SyncReadStream& stream, DynamicBuffer& dynabuf,
    Parser& parser, error_code& ec)
{
    static_assert(is_SyncReadStream<SyncReadStream>::value,
        "SyncReadStream requirements not met");
    static_assert(is_DynamicBuffer<DynamicBuffer>::value,
        "DynamicBuffer requirements not met");
    static_assert(is_Parser<Parser>::value,
        "Parser requirements not met");
    bool started = false;
    for(;;)
    {
        auto used =
            parser.write(dynabuf.data(), ec);
        if(ec)
            return;
        dynabuf.consume(used);
        if(used > 0)
            started = true;
        if(parser.complete())
            break;
        dynabuf.commit(stream.read_some(
            dynabuf.prepare(read_size_helper(
                dynabuf, 65536)), ec));
        if(ec && ec != boost::asio::error::eof)
            return;
        if(ec == boost::asio::error::eof)
        {
            if(! started)
                return;
            // Caller will see eof on next read.
            ec = {};
            parser.write_eof(ec);
            if(ec)
                return;
            assert(parser.complete());
            break;
        }
    }
}

template<class AsyncReadStream,
    class DynamicBuffer, class Parser, class ReadHandler>
typename async_completion<
    ReadHandler, void(error_code)>::result_type
async_parse(AsyncReadStream& stream,
    DynamicBuffer& dynabuf, Parser& parser, ReadHandler&& handler)
{
    static_assert(is_AsyncReadStream<AsyncReadStream>::value,
        "AsyncReadStream requirements not met");
    static_assert(is_DynamicBuffer<DynamicBuffer>::value,
        "DynamicBuffer requirements not met");
    static_assert(is_Parser<Parser>::value,
        "Parser requirements not met");
    beast::async_completion<ReadHandler,
        void(error_code)> completion(handler);
    detail::parse_op<AsyncReadStream, DynamicBuffer,
        Parser, decltype(completion.handler)>{
            completion.handler, stream, dynabuf, parser};
    return completion.result.get();
}

template<class SyncReadStream, class DynamicBuffer,
    bool isRequest, class Body, class Headers>
void
read(SyncReadStream& stream, DynamicBuffer& dynabuf,
    message_v1<isRequest, Body, Headers>& msg)
{
    static_assert(is_SyncReadStream<SyncReadStream>::value,
        "SyncReadStream requirements not met");
    static_assert(is_DynamicBuffer<DynamicBuffer>::value,
        "DynamicBuffer requirements not met");
    static_assert(is_ReadableBody<Body>::value,
        "ReadableBody requirements not met");
    error_code ec;
    beast::http::read(stream, dynabuf, msg, ec);
    if(ec)
        throw system_error{ec};
}

template<class SyncReadStream, class DynamicBuffer,
    bool isRequest, class Body, class Headers>
void
read(SyncReadStream& stream, DynamicBuffer& dynabuf,
    message_v1<isRequest, Body, Headers>& m,
        error_code& ec)
{
    static_assert(is_SyncReadStream<SyncReadStream>::value,
        "SyncReadStream requirements not met");
    static_assert(is_DynamicBuffer<DynamicBuffer>::value,
        "DynamicBuffer requirements not met");
    static_assert(is_ReadableBody<Body>::value,
        "ReadableBody requirements not met");
    parser_v1<isRequest, Body, Headers> p;
    beast::http::parse(stream, dynabuf, p, ec);
    if(ec)
        return;
    assert(p.complete());
    m = p.release();
}

template<class AsyncReadStream, class DynamicBuffer,
    bool isRequest, class Body, class Headers,
        class ReadHandler>
typename async_completion<
    ReadHandler, void(error_code)>::result_type
async_read(AsyncReadStream& stream, DynamicBuffer& dynabuf,
    message_v1<isRequest, Body, Headers>& m,
        ReadHandler&& handler)
{
    static_assert(is_AsyncReadStream<AsyncReadStream>::value,
        "AsyncReadStream requirements not met");
    static_assert(is_DynamicBuffer<DynamicBuffer>::value,
        "DynamicBuffer requirements not met");
    static_assert(is_ReadableBody<Body>::value,
        "ReadableBody requirements not met");
    beast::async_completion<ReadHandler,
        void(error_code)> completion(handler);
    detail::read_op<AsyncReadStream, DynamicBuffer,
        isRequest, Body, Headers, decltype(
            completion.handler)>{completion.handler,
                stream, dynabuf, m};
    return completion.result.get();
}

} // http
} // beast

#endif
