//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_HTTP_IMPL_READ_IPP_HPP
#define BEAST_HTTP_IMPL_READ_IPP_HPP

#include <beast/http/concepts.hpp>
#include <beast/http/header_parser_v1.hpp>
#include <beast/http/parse.hpp>
#include <beast/http/parser_v1.hpp>
#include <beast/core/bind_handler.hpp>
#include <beast/core/handler_helpers.hpp>
#include <beast/core/handler_ptr.hpp>
#include <beast/core/stream_concepts.hpp>
#include <boost/assert.hpp>

namespace beast {
namespace http {

namespace detail {

template<class Stream, class DynamicBuffer,
    bool isRequest, class Fields,
        class Handler>
class read_header_op
{
    using parser_type =
        header_parser_v1<isRequest, Fields>;

    using message_type =
        header<isRequest, Fields>;

    struct data
    {
        bool cont;
        Stream& s;
        DynamicBuffer& db;
        message_type& m;
        parser_type p;
        bool started = false;
        int state = 0;

        data(Handler& handler, Stream& s_,
                DynamicBuffer& sb_, message_type& m_)
            : cont(beast_asio_helpers::
                is_continuation(handler))
            , s(s_)
            , db(sb_)
            , m(m_)
        {
        }
    };

    handler_ptr<data, Handler> d_;

public:
    read_header_op(read_header_op&&) = default;
    read_header_op(read_header_op const&) = default;

    template<class DeducedHandler, class... Args>
    read_header_op(
            DeducedHandler&& h, Stream& s, Args&&... args)
        : d_(std::forward<DeducedHandler>(h),
                s, std::forward<Args>(args)...)
    {
        (*this)(error_code{}, false);
    }

    void
    operator()(error_code ec, bool again = true);

    friend
    void* asio_handler_allocate(
        std::size_t size, read_header_op* op)
    {
        return beast_asio_helpers::
            allocate(size, op->d_.handler());
    }

    friend
    void asio_handler_deallocate(
        void* p, std::size_t size, read_header_op* op)
    {
        return beast_asio_helpers::
            deallocate(p, size, op->d_.handler());
    }

    friend
    bool asio_handler_is_continuation(read_header_op* op)
    {
        return op->d_->cont;
    }

    template<class Function>
    friend
    void asio_handler_invoke(Function&& f, read_header_op* op)
    {
        return beast_asio_helpers::
            invoke(f, op->d_.handler());
    }
};

template<class Stream, class DynamicBuffer,
    bool isRequest, class Fields,
        class Handler>
void
read_header_op<Stream, DynamicBuffer, isRequest, Fields, Handler>::
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
    d_.invoke(ec);
}

} // detail

template<class SyncReadStream, class DynamicBuffer,
    bool isRequest, class Fields>
void
read(SyncReadStream& stream, DynamicBuffer& dynabuf,
    header<isRequest, Fields>& msg)
{
    static_assert(is_SyncReadStream<SyncReadStream>::value,
        "SyncReadStream requirements not met");
    static_assert(is_DynamicBuffer<DynamicBuffer>::value,
        "DynamicBuffer requirements not met");
    error_code ec;
    beast::http::read(stream, dynabuf, msg, ec);
    if(ec)
        throw system_error{ec};
}

template<class SyncReadStream, class DynamicBuffer,
    bool isRequest, class Fields>
void
read(SyncReadStream& stream, DynamicBuffer& dynabuf,
    header<isRequest, Fields>& m,
        error_code& ec)
{
    static_assert(is_SyncReadStream<SyncReadStream>::value,
        "SyncReadStream requirements not met");
    static_assert(is_DynamicBuffer<DynamicBuffer>::value,
        "DynamicBuffer requirements not met");
    header_parser_v1<isRequest, Fields> p;
    beast::http::parse(stream, dynabuf, p, ec);
    if(ec)
        return;
    BOOST_ASSERT(p.complete());
    m = p.release();
}

template<class AsyncReadStream, class DynamicBuffer,
    bool isRequest, class Fields,
        class ReadHandler>
typename async_completion<
    ReadHandler, void(error_code)>::result_type
async_read(AsyncReadStream& stream, DynamicBuffer& dynabuf,
    header<isRequest, Fields>& m,
        ReadHandler&& handler)
{
    static_assert(is_AsyncReadStream<AsyncReadStream>::value,
        "AsyncReadStream requirements not met");
    static_assert(is_DynamicBuffer<DynamicBuffer>::value,
        "DynamicBuffer requirements not met");
    beast::async_completion<ReadHandler,
        void(error_code)> completion{handler};
    detail::read_header_op<AsyncReadStream, DynamicBuffer,
        isRequest, Fields, decltype(
            completion.handler)>{completion.handler,
                stream, dynabuf, m};
    return completion.result.get();
}

//------------------------------------------------------------------------------

namespace detail {

template<class Stream, class DynamicBuffer,
    bool isRequest, class Body, class Fields,
        class Handler>
class read_op
{
    using parser_type =
        parser_v1<isRequest, Body, Fields>;

    using message_type =
        message<isRequest, Body, Fields>;

    struct data
    {
        bool cont;
        Stream& s;
        DynamicBuffer& db;
        message_type& m;
        parser_type p;
        bool started = false;
        int state = 0;

        data(Handler& handler, Stream& s_,
                DynamicBuffer& sb_, message_type& m_)
            : cont(beast_asio_helpers::
                is_continuation(handler))
            , s(s_)
            , db(sb_)
            , m(m_)
        {
        }
    };

    handler_ptr<data, Handler> d_;

public:
    read_op(read_op&&) = default;
    read_op(read_op const&) = default;

    template<class DeducedHandler, class... Args>
    read_op(DeducedHandler&& h, Stream& s, Args&&... args)
        : d_(std::forward<DeducedHandler>(h),
            s, std::forward<Args>(args)...)
    {
        (*this)(error_code{}, false);
    }

    void
    operator()(error_code ec, bool again = true);

    friend
    void* asio_handler_allocate(
        std::size_t size, read_op* op)
    {
        return beast_asio_helpers::
            allocate(size, op->d_.handler());
    }

    friend
    void asio_handler_deallocate(
        void* p, std::size_t size, read_op* op)
    {
        return beast_asio_helpers::
            deallocate(p, size, op->d_.handler());
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
        return beast_asio_helpers::
            invoke(f, op->d_.handler());
    }
};

template<class Stream, class DynamicBuffer,
    bool isRequest, class Body, class Fields,
        class Handler>
void
read_op<Stream, DynamicBuffer, isRequest, Body, Fields, Handler>::
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
    d_.invoke(ec);
}

} // detail

template<class SyncReadStream, class DynamicBuffer,
    bool isRequest, class Body, class Fields>
void
read(SyncReadStream& stream, DynamicBuffer& dynabuf,
    message<isRequest, Body, Fields>& msg)
{
    static_assert(is_SyncReadStream<SyncReadStream>::value,
        "SyncReadStream requirements not met");
    static_assert(is_DynamicBuffer<DynamicBuffer>::value,
        "DynamicBuffer requirements not met");
    static_assert(is_Body<Body>::value,
        "Body requirements not met");
    static_assert(has_reader<Body>::value,
        "Body has no reader");
    static_assert(is_Reader<typename Body::reader,
        message<isRequest, Body, Fields>>::value,
            "Reader requirements not met");
    error_code ec;
    beast::http::read(stream, dynabuf, msg, ec);
    if(ec)
        throw system_error{ec};
}

template<class SyncReadStream, class DynamicBuffer,
    bool isRequest, class Body, class Fields>
void
read(SyncReadStream& stream, DynamicBuffer& dynabuf,
    message<isRequest, Body, Fields>& m,
        error_code& ec)
{
    static_assert(is_SyncReadStream<SyncReadStream>::value,
        "SyncReadStream requirements not met");
    static_assert(is_DynamicBuffer<DynamicBuffer>::value,
        "DynamicBuffer requirements not met");
    static_assert(is_Body<Body>::value,
        "Body requirements not met");
    static_assert(has_reader<Body>::value,
        "Body has no reader");
    static_assert(is_Reader<typename Body::reader,
        message<isRequest, Body, Fields>>::value,
            "Reader requirements not met");
    parser_v1<isRequest, Body, Fields> p;
    beast::http::parse(stream, dynabuf, p, ec);
    if(ec)
        return;
    BOOST_ASSERT(p.complete());
    m = p.release();
}

template<class AsyncReadStream, class DynamicBuffer,
    bool isRequest, class Body, class Fields,
        class ReadHandler>
typename async_completion<
    ReadHandler, void(error_code)>::result_type
async_read(AsyncReadStream& stream, DynamicBuffer& dynabuf,
    message<isRequest, Body, Fields>& m,
        ReadHandler&& handler)
{
    static_assert(is_AsyncReadStream<AsyncReadStream>::value,
        "AsyncReadStream requirements not met");
    static_assert(is_DynamicBuffer<DynamicBuffer>::value,
        "DynamicBuffer requirements not met");
    static_assert(is_Body<Body>::value,
        "Body requirements not met");
    static_assert(has_reader<Body>::value,
        "Body has no reader");
    static_assert(is_Reader<typename Body::reader,
        message<isRequest, Body, Fields>>::value,
            "Reader requirements not met");
    beast::async_completion<ReadHandler,
        void(error_code)> completion{handler};
    detail::read_op<AsyncReadStream, DynamicBuffer,
        isRequest, Body, Fields, decltype(
            completion.handler)>{completion.handler,
                stream, dynabuf, m};
    return completion.result.get();
}

} // http
} // beast

#endif
