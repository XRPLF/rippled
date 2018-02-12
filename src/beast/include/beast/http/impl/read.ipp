//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_HTTP_IMPL_READ_IPP_HPP
#define BEAST_HTTP_IMPL_READ_IPP_HPP

#include <beast/http/type_traits.hpp>
#include <beast/http/error.hpp>
#include <beast/http/parser.hpp>
#include <beast/http/read.hpp>
#include <beast/core/bind_handler.hpp>
#include <beast/core/handler_ptr.hpp>
#include <beast/core/read_size.hpp>
#include <beast/core/type_traits.hpp>
#include <boost/asio/handler_alloc_hook.hpp>
#include <boost/asio/handler_continuation_hook.hpp>
#include <boost/asio/handler_invoke_hook.hpp>
#include <boost/assert.hpp>
#include <boost/config.hpp>
#include <boost/optional.hpp>
#include <boost/throw_exception.hpp>

namespace beast {
namespace http {

namespace detail {

//------------------------------------------------------------------------------

template<class Stream, class DynamicBuffer,
    bool isRequest, class Derived, class Handler>
class read_some_op
{
    int state_ = 0;
    Stream& s_;
    DynamicBuffer& b_;
    basic_parser<isRequest, Derived>& p_;
    boost::optional<typename
        DynamicBuffer::mutable_buffers_type> mb_;
    Handler h_;

public:
    read_some_op(read_some_op&&) = default;
    read_some_op(read_some_op const&) = default;

    template<class DeducedHandler>
    read_some_op(DeducedHandler&& h, Stream& s,
        DynamicBuffer& b, basic_parser<isRequest, Derived>& p)
        : s_(s)
        , b_(b)
        , p_(p)
        , h_(std::forward<DeducedHandler>(h))
    {
    }

    void
    operator()(error_code ec, std::size_t bytes_transferred);

    friend
    void* asio_handler_allocate(
        std::size_t size, read_some_op* op)
    {
        using boost::asio::asio_handler_allocate;
        return asio_handler_allocate(
            size, std::addressof(op->h_));
    }

    friend
    void asio_handler_deallocate(
        void* p, std::size_t size, read_some_op* op)
    {
        using boost::asio::asio_handler_deallocate;
        asio_handler_deallocate(
            p, size, std::addressof(op->h_));
    }

    friend
    bool asio_handler_is_continuation(read_some_op* op)
    {
        using boost::asio::asio_handler_is_continuation;
        return op->state_ >= 2 ? true :
            asio_handler_is_continuation(
                std::addressof(op->h_));
    }

    template<class Function>
    friend
    void asio_handler_invoke(Function&& f, read_some_op* op)
    {
        using boost::asio::asio_handler_invoke;
        asio_handler_invoke(
            f, std::addressof(op->h_));
    }
};

template<class Stream, class DynamicBuffer,
    bool isRequest, class Derived, class Handler>
void
read_some_op<Stream, DynamicBuffer,
    isRequest, Derived, Handler>::
operator()(error_code ec, std::size_t bytes_transferred)
{
    switch(state_)
    {
    case 0:
        state_ = 1;
        if(b_.size() == 0)
            goto do_read;
        goto do_parse;

    case 1:
        state_ = 2;
    case 2:
        if(ec == boost::asio::error::eof)
        {
            BOOST_ASSERT(bytes_transferred == 0);
            if(p_.got_some())
            {
                // caller sees EOF on next read
                ec.assign(0, ec.category());
                p_.put_eof(ec);
                if(ec)
                    goto upcall;
                BOOST_ASSERT(p_.is_done());
                goto upcall;
            }
            ec = error::end_of_stream;
            goto upcall;
        }
        if(ec)
            goto upcall;
        b_.commit(bytes_transferred);

    do_parse:
        b_.consume(p_.put(b_.data(), ec));
        if(! ec || ec != http::error::need_more)
            goto do_upcall;
        ec.assign(0, ec.category());

    do_read:
        try
        {
            mb_.emplace(b_.prepare(
                read_size_or_throw(b_, 65536)));
        }
        catch(std::length_error const&)
        {
            ec = error::buffer_overflow;
            goto do_upcall;
        }
        return s_.async_read_some(*mb_, std::move(*this));

    do_upcall:
        if(state_ >= 2)
            goto upcall;
        state_ = 3;
        return s_.get_io_service().post(
            bind_handler(std::move(*this), ec, 0));

    case 3:
        break;
    }
upcall:
    h_(ec);
}

//------------------------------------------------------------------------------

struct parser_is_done
{
    template<bool isRequest, class Derived>
    bool
    operator()(basic_parser<
        isRequest, Derived> const& p) const
    {
        return p.is_done();
    }
};

struct parser_is_header_done
{
    template<bool isRequest, class Derived>
    bool
    operator()(basic_parser<
        isRequest, Derived> const& p) const
    {
        return p.is_header_done();
    }
};

template<class Stream, class DynamicBuffer,
    bool isRequest, class Derived, class Condition,
        class Handler>
class read_op
{
    int state_ = 0;
    Stream& s_;
    DynamicBuffer& b_;
    basic_parser<isRequest, Derived>& p_;
    Handler h_;

public:
    read_op(read_op&&) = default;
    read_op(read_op const&) = default;

    template<class DeducedHandler>
    read_op(DeducedHandler&& h, Stream& s,
        DynamicBuffer& b, basic_parser<isRequest,
            Derived>& p)
        : s_(s)
        , b_(b)
        , p_(p)
        , h_(std::forward<DeducedHandler>(h))
    {
    }

    void
    operator()(error_code ec);

    friend
    void* asio_handler_allocate(
        std::size_t size, read_op* op)
    {
        using boost::asio::asio_handler_allocate;
        return asio_handler_allocate(
            size, std::addressof(op->h_));
    }

    friend
    void asio_handler_deallocate(
        void* p, std::size_t size, read_op* op)
    {
        using boost::asio::asio_handler_deallocate;
        asio_handler_deallocate(
            p, size, std::addressof(op->h_));
    }

    friend
    bool asio_handler_is_continuation(read_op* op)
    {
        using boost::asio::asio_handler_is_continuation;
        return op->state_ >= 3 ? true :
            asio_handler_is_continuation(
                std::addressof(op->h_));
    }

    template<class Function>
    friend
    void asio_handler_invoke(Function&& f, read_op* op)
    {
        using boost::asio::asio_handler_invoke;
        asio_handler_invoke(
            f, std::addressof(op->h_));
    }
};

template<class Stream, class DynamicBuffer,
    bool isRequest, class Derived, class Condition,
        class Handler>
void
read_op<Stream, DynamicBuffer,
    isRequest, Derived, Condition, Handler>::
operator()(error_code ec)
{
    switch(state_)
    {
    case 0:
        if(Condition{}(p_))
        {
            state_ = 1;
            return s_.get_io_service().post(
                bind_handler(std::move(*this), ec));
        }
        state_ = 2;

    do_read:
        return async_read_some(
            s_, b_, p_, std::move(*this));

    case 1:
        goto upcall;

    case 2:
    case 3:
        if(ec)
            goto upcall;
        if(Condition{}(p_))
            goto upcall;
        state_ = 3;
        goto do_read;
    }
upcall:
    h_(ec);
}

//------------------------------------------------------------------------------

template<class Stream, class DynamicBuffer,
    bool isRequest, class Body, class Allocator,
        class Handler>
class read_msg_op
{
    using parser_type =
        parser<isRequest, Body, Allocator>;

    using message_type =
        typename parser_type::value_type;

    struct data
    {
        int state = 0;
        Stream& s;
        DynamicBuffer& b;
        message_type& m;
        parser_type p;

        data(Handler&, Stream& s_,
                DynamicBuffer& b_, message_type& m_)
            : s(s_)
            , b(b_)
            , m(m_)
            , p(std::move(m))
        {
            p.eager(true);
        }
    };

    handler_ptr<data, Handler> d_;

public:
    read_msg_op(read_msg_op&&) = default;
    read_msg_op(read_msg_op const&) = default;

    template<class DeducedHandler, class... Args>
    read_msg_op(DeducedHandler&& h, Stream& s, Args&&... args)
        : d_(std::forward<DeducedHandler>(h),
            s, std::forward<Args>(args)...)
    {
    }

    void
    operator()(error_code ec);

    friend
    void* asio_handler_allocate(
        std::size_t size, read_msg_op* op)
    {
        using boost::asio::asio_handler_allocate;
        return asio_handler_allocate(
            size, std::addressof(op->d_.handler()));
    }

    friend
    void asio_handler_deallocate(
        void* p, std::size_t size, read_msg_op* op)
    {
        using boost::asio::asio_handler_deallocate;
        asio_handler_deallocate(
            p, size, std::addressof(op->d_.handler()));
    }

    friend
    bool asio_handler_is_continuation(read_msg_op* op)
    {
        using boost::asio::asio_handler_is_continuation;
        return op->d_->state >= 2 ? true :
            asio_handler_is_continuation(
                std::addressof(op->d_.handler()));
    }

    template<class Function>
    friend
    void asio_handler_invoke(Function&& f, read_msg_op* op)
    {
        using boost::asio::asio_handler_invoke;
        asio_handler_invoke(
            f, std::addressof(op->d_.handler()));
    }
};

template<class Stream, class DynamicBuffer,
    bool isRequest, class Body, class Allocator,
        class Handler>
void
read_msg_op<Stream, DynamicBuffer,
    isRequest, Body, Allocator, Handler>::
operator()(error_code ec)
{
    auto& d = *d_;
    switch(d.state)
    {
    case 0:
        d.state = 1;

    do_read:
        return async_read_some(
            d.s, d.b, d.p, std::move(*this));

    case 1:
    case 2:
        if(ec)
            goto upcall;
        if(d.p.is_done())
        {
            d.m = d.p.release();
            goto upcall;
        }
        d.state = 2;
        goto do_read;
    }
upcall:
    d_.invoke(ec);
}

} // detail

//------------------------------------------------------------------------------

template<
    class SyncReadStream,
    class DynamicBuffer,
    bool isRequest, class Derived>
void
read_some(
    SyncReadStream& stream,
    DynamicBuffer& buffer,
    basic_parser<isRequest, Derived>& parser)
{
    static_assert(is_sync_read_stream<SyncReadStream>::value,
        "SyncReadStream requirements not met");
    static_assert(is_dynamic_buffer<DynamicBuffer>::value,
        "DynamicBuffer requirements not met");
    BOOST_ASSERT(! parser.is_done());
    error_code ec;
    read_some(stream, buffer, parser, ec);
    if(ec)
        BOOST_THROW_EXCEPTION(system_error{ec});
}

template<
    class SyncReadStream,
    class DynamicBuffer,
    bool isRequest, class Derived>
void
read_some(
    SyncReadStream& stream,
    DynamicBuffer& buffer,
    basic_parser<isRequest, Derived>& parser,
    error_code& ec)
{
    static_assert(is_sync_read_stream<SyncReadStream>::value,
        "SyncReadStream requirements not met");
    static_assert(is_dynamic_buffer<DynamicBuffer>::value,
        "DynamicBuffer requirements not met");
    BOOST_ASSERT(! parser.is_done());
    if(buffer.size() == 0)
        goto do_read;
    for(;;)
    {
        // invoke parser
        buffer.consume(parser.put(buffer.data(), ec));
        if(! ec)
            break;
        if(ec != http::error::need_more)
            break;
    do_read:
        boost::optional<typename
            DynamicBuffer::mutable_buffers_type> b;
        try
        {
            b.emplace(buffer.prepare(
                read_size_or_throw(buffer, 65536)));
        }
        catch(std::length_error const&)
        {
            ec = error::buffer_overflow;
            return;
        }
        auto const bytes_transferred =
            stream.read_some(*b, ec);
        if(ec == boost::asio::error::eof)
        {
            BOOST_ASSERT(bytes_transferred == 0);
            if(parser.got_some())
            {
                // caller sees EOF on next read
                parser.put_eof(ec);
                if(ec)
                    break;
                BOOST_ASSERT(parser.is_done());
                break;
            }
            ec = error::end_of_stream;
            break;
        }
        if(ec)
            break;
        buffer.commit(bytes_transferred);
    }
}

template<
    class AsyncReadStream,
    class DynamicBuffer,
    bool isRequest, class Derived,
    class ReadHandler>
async_return_type<
    ReadHandler, void(error_code)>
async_read_some(
    AsyncReadStream& stream,
    DynamicBuffer& buffer,
    basic_parser<isRequest, Derived>& parser,
    ReadHandler&& handler)
{
    static_assert(is_async_read_stream<AsyncReadStream>::value,
        "AsyncReadStream requirements not met");
    static_assert(is_dynamic_buffer<DynamicBuffer>::value,
        "DynamicBuffer requirements not met");
    BOOST_ASSERT(! parser.is_done());
    async_completion<ReadHandler, void(error_code)> init{handler};
    detail::read_some_op<AsyncReadStream,
        DynamicBuffer, isRequest, Derived, handler_type<
            ReadHandler, void(error_code, std::size_t)>>{
                init.completion_handler, stream, buffer, parser}(
                    error_code{}, 0);
    return init.result.get();
}

//------------------------------------------------------------------------------

template<
    class SyncReadStream,
    class DynamicBuffer,
    bool isRequest, class Derived>
void
read_header(
    SyncReadStream& stream,
    DynamicBuffer& buffer,
    basic_parser<isRequest, Derived>& parser)
{
    static_assert(is_sync_read_stream<SyncReadStream>::value,
        "SyncReadStream requirements not met");
    static_assert(is_dynamic_buffer<DynamicBuffer>::value,
        "DynamicBuffer requirements not met");
    error_code ec;
    read_header(stream, buffer, parser, ec);
    if(ec)
        BOOST_THROW_EXCEPTION(system_error{ec});
}

template<
    class SyncReadStream,
    class DynamicBuffer,
    bool isRequest, class Derived>
void
read_header(
    SyncReadStream& stream,
    DynamicBuffer& buffer,
    basic_parser<isRequest, Derived>& parser,
    error_code& ec)
{
    static_assert(is_sync_read_stream<SyncReadStream>::value,
        "SyncReadStream requirements not met");
    static_assert(is_dynamic_buffer<DynamicBuffer>::value,
        "DynamicBuffer requirements not met");
    parser.eager(false);
    if(parser.is_header_done())
    {
        ec.assign(0, ec.category());
        return;
    }
    do
    {
        read_some(stream, buffer, parser, ec);
        if(ec)
            return;
    }
    while(! parser.is_header_done());
}

template<
    class AsyncReadStream,
    class DynamicBuffer,
    bool isRequest, class Derived,
    class ReadHandler>
async_return_type<ReadHandler, void(error_code)>
async_read_header(
    AsyncReadStream& stream,
    DynamicBuffer& buffer,
    basic_parser<isRequest, Derived>& parser,
    ReadHandler&& handler)
{
    static_assert(is_async_read_stream<AsyncReadStream>::value,
        "AsyncReadStream requirements not met");
    static_assert(is_dynamic_buffer<DynamicBuffer>::value,
        "DynamicBuffer requirements not met");
    parser.eager(false);
    async_completion<ReadHandler,
        void(error_code)> init{handler};
    detail::read_op<AsyncReadStream, DynamicBuffer,
        isRequest, Derived, detail::parser_is_header_done,
            handler_type<ReadHandler, void(error_code)>>{
                init.completion_handler, stream, buffer, parser}(
                    error_code{});
    return init.result.get();
}

//------------------------------------------------------------------------------

template<
    class SyncReadStream,
    class DynamicBuffer,
    bool isRequest, class Derived>
void
read(
    SyncReadStream& stream,
    DynamicBuffer& buffer,
    basic_parser<isRequest, Derived>& parser)
{
    static_assert(is_sync_read_stream<SyncReadStream>::value,
        "SyncReadStream requirements not met");
    static_assert(is_dynamic_buffer<DynamicBuffer>::value,
        "DynamicBuffer requirements not met");
    error_code ec;
    read(stream, buffer, parser, ec);
    if(ec)
        BOOST_THROW_EXCEPTION(system_error{ec});
}

template<
    class SyncReadStream,
    class DynamicBuffer,
    bool isRequest, class Derived>
void
read(
    SyncReadStream& stream,
    DynamicBuffer& buffer,
    basic_parser<isRequest, Derived>& parser,
    error_code& ec)
{
    static_assert(is_sync_read_stream<SyncReadStream>::value,
        "SyncReadStream requirements not met");
    static_assert(is_dynamic_buffer<DynamicBuffer>::value,
        "DynamicBuffer requirements not met");
    parser.eager(true);
    if(parser.is_done())
    {
        ec.assign(0, ec.category());
        return;
    }
    do
    {
        read_some(stream, buffer, parser, ec);
        if(ec)
            return;
    }
    while(! parser.is_done());
}

template<
    class AsyncReadStream,
    class DynamicBuffer,
    bool isRequest, class Derived,
    class ReadHandler>
async_return_type<ReadHandler, void(error_code)>
async_read(
    AsyncReadStream& stream,
    DynamicBuffer& buffer,
    basic_parser<isRequest, Derived>& parser,
    ReadHandler&& handler)
{
    static_assert(is_async_read_stream<AsyncReadStream>::value,
        "AsyncReadStream requirements not met");
    static_assert(is_dynamic_buffer<DynamicBuffer>::value,
        "DynamicBuffer requirements not met");
    parser.eager(true);
    async_completion<ReadHandler,
        void(error_code)> init{handler};
    detail::read_op<AsyncReadStream, DynamicBuffer,
        isRequest, Derived, detail::parser_is_done,
            handler_type<ReadHandler, void(error_code)>>{
                init.completion_handler, stream, buffer, parser}(
                    error_code{});
    return init.result.get();
}

//------------------------------------------------------------------------------

template<
    class SyncReadStream,
    class DynamicBuffer,
    bool isRequest, class Body, class Allocator>
void
read(
    SyncReadStream& stream,
    DynamicBuffer& buffer,
    message<isRequest, Body, basic_fields<Allocator>>& msg)
{
    static_assert(is_sync_read_stream<SyncReadStream>::value,
        "SyncReadStream requirements not met");
    static_assert(is_dynamic_buffer<DynamicBuffer>::value,
        "DynamicBuffer requirements not met");
    static_assert(is_body<Body>::value,
        "Body requirements not met");
    static_assert(is_body_writer<Body>::value,
        "BodyWriter requirements not met");
    error_code ec;
    read(stream, buffer, msg, ec);
    if(ec)
        BOOST_THROW_EXCEPTION(system_error{ec});
}

template<
    class SyncReadStream,
    class DynamicBuffer,
    bool isRequest, class Body, class Allocator>
void
read(
    SyncReadStream& stream,
    DynamicBuffer& buffer,
    message<isRequest, Body, basic_fields<Allocator>>& msg,
    error_code& ec)
{
    static_assert(is_sync_read_stream<SyncReadStream>::value,
        "SyncReadStream requirements not met");
    static_assert(is_dynamic_buffer<DynamicBuffer>::value,
        "DynamicBuffer requirements not met");
    static_assert(is_body<Body>::value,
        "Body requirements not met");
    static_assert(is_body_writer<Body>::value,
        "BodyWriter requirements not met");
    parser<isRequest, Body, Allocator> p{std::move(msg)};
    p.eager(true);
    read(stream, buffer, p.base(), ec);
    if(ec)
        return;
    msg = p.release();
}

template<
    class AsyncReadStream,
    class DynamicBuffer,
    bool isRequest, class Body, class Allocator,
    class ReadHandler>
async_return_type<ReadHandler, void(error_code)>
async_read(
    AsyncReadStream& stream,
    DynamicBuffer& buffer,
    message<isRequest, Body, basic_fields<Allocator>>& msg,
    ReadHandler&& handler)
{
    static_assert(is_async_read_stream<AsyncReadStream>::value,
        "AsyncReadStream requirements not met");
    static_assert(is_dynamic_buffer<DynamicBuffer>::value,
        "DynamicBuffer requirements not met");
    static_assert(is_body<Body>::value,
        "Body requirements not met");
    static_assert(is_body_writer<Body>::value,
        "BodyWriter requirements not met");
    async_completion<ReadHandler,
        void(error_code)> init{handler};
    detail::read_msg_op<AsyncReadStream, DynamicBuffer,
        isRequest, Body, Allocator, handler_type<
            ReadHandler, void(error_code)>>{
                init.completion_handler, stream, buffer, msg}(
                    error_code{});
    return init.result.get();
}

} // http
} // beast

#endif
