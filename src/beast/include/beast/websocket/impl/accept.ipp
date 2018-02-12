//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_WEBSOCKET_IMPL_ACCEPT_IPP
#define BEAST_WEBSOCKET_IMPL_ACCEPT_IPP

#include <beast/websocket/detail/type_traits.hpp>
#include <beast/http/empty_body.hpp>
#include <beast/http/parser.hpp>
#include <beast/http/read.hpp>
#include <beast/http/string_body.hpp>
#include <beast/http/write.hpp>
#include <beast/core/buffer_prefix.hpp>
#include <beast/core/handler_ptr.hpp>
#include <beast/core/detail/type_traits.hpp>
#include <boost/asio/handler_alloc_hook.hpp>
#include <boost/asio/handler_continuation_hook.hpp>
#include <boost/asio/handler_invoke_hook.hpp>
#include <boost/assert.hpp>
#include <boost/throw_exception.hpp>
#include <memory>
#include <type_traits>

namespace beast {
namespace websocket {

//------------------------------------------------------------------------------

// Respond to an upgrade HTTP request
template<class NextLayer>
template<class Handler>
class stream<NextLayer>::response_op
{
    struct data
    {
        bool cont;
        stream<NextLayer>& ws;
        response_type res;
        int state = 0;

        template<class Allocator, class Decorator>
        data(Handler&, stream<NextLayer>& ws_, http::header<
            true, http::basic_fields<Allocator>> const& req,
                Decorator const& decorator,
                    bool cont_)
            : cont(cont_)
            , ws(ws_)
            , res(ws_.build_response(req, decorator))
        {
        }

        template<class Allocator,
            class Buffers, class Decorator>
        data(Handler&, stream<NextLayer>& ws_, http::header<
            true, http::basic_fields<Allocator>> const& req,
                Buffers const& buffers,
                    Decorator const& decorator,
                        bool cont_)
            : cont(cont_)
            , ws(ws_)
            , res(ws_.build_response(req, decorator))
        {
            using boost::asio::buffer_copy;
            using boost::asio::buffer_size;
            // VFALCO What about catch(std::length_error const&)?
            ws.stream_.buffer().commit(buffer_copy(
                ws.stream_.buffer().prepare(
                    buffer_size(buffers)), buffers));
        }
    };

    handler_ptr<data, Handler> d_;

public:
    response_op(response_op&&) = default;
    response_op(response_op const&) = default;

    template<class DeducedHandler, class... Args>
    response_op(DeducedHandler&& h,
            stream<NextLayer>& ws, Args&&... args)
        : d_(std::forward<DeducedHandler>(h),
            ws, std::forward<Args>(args)...)
    {
        (*this)(error_code{}, false);
    }

    void operator()(
        error_code ec, bool again = true);

    friend
    void* asio_handler_allocate(
        std::size_t size, response_op* op)
    {
        using boost::asio::asio_handler_allocate;
        return asio_handler_allocate(
            size, std::addressof(op->d_.handler()));
    }

    friend
    void asio_handler_deallocate(
        void* p, std::size_t size, response_op* op)
    {
        using boost::asio::asio_handler_deallocate;
        asio_handler_deallocate(
            p, size, std::addressof(op->d_.handler()));
    }

    friend
    bool asio_handler_is_continuation(response_op* op)
    {
        return op->d_->cont;
    }

    template<class Function>
    friend
    void asio_handler_invoke(Function&& f, response_op* op)
    {
        using boost::asio::asio_handler_invoke;
        asio_handler_invoke(
            f, std::addressof(op->d_.handler()));
    }
};

template<class NextLayer>
template<class Handler>
void
stream<NextLayer>::response_op<Handler>::
operator()(error_code ec, bool again)
{
    auto& d = *d_;
    d.cont = d.cont || again;
    while(! ec && d.state != 99)
    {
        switch(d.state)
        {
        case 0:
            // send response
            d.state = 1;
            http::async_write(d.ws.next_layer(),
                d.res, std::move(*this));
            return;

        // sent response
        case 1:
            d.state = 99;
            if(d.res.result() !=
                    http::status::switching_protocols)
                ec = error::handshake_failed;
            if(! ec)
            {
                pmd_read(d.ws.pmd_config_, d.res);
                d.ws.open(role_type::server);
            }
            break;
        }
    }
    d_.invoke(ec);
}

//------------------------------------------------------------------------------

// read and respond to an upgrade request
//
template<class NextLayer>
template<class Decorator, class Handler>
class stream<NextLayer>::accept_op
{
    struct data
    {
        stream<NextLayer>& ws;
        Decorator decorator;
        http::request_parser<http::empty_body> p;

        data(Handler&, stream<NextLayer>& ws_,
                Decorator const& decorator_)
            : ws(ws_)
            , decorator(decorator_)
        {
        }

        template<class Buffers>
        data(Handler&, stream<NextLayer>& ws_,
            Buffers const& buffers,
                Decorator const& decorator_)
            : ws(ws_)
            , decorator(decorator_)
        {
            using boost::asio::buffer_copy;
            using boost::asio::buffer_size;
            // VFALCO What about catch(std::length_error const&)?
            ws.stream_.buffer().commit(buffer_copy(
                ws.stream_.buffer().prepare(
                    buffer_size(buffers)), buffers));
        }
    };

    handler_ptr<data, Handler> d_;

public:
    accept_op(accept_op&&) = default;
    accept_op(accept_op const&) = default;

    template<class DeducedHandler, class... Args>
    accept_op(DeducedHandler&& h,
            stream<NextLayer>& ws, Args&&... args)
        : d_(std::forward<DeducedHandler>(h),
            ws, std::forward<Args>(args)...)
    {
    }

    void operator()();

    void operator()(error_code ec);

    friend
    void* asio_handler_allocate(
        std::size_t size, accept_op* op)
    {
        using boost::asio::asio_handler_allocate;
        return asio_handler_allocate(
            size, std::addressof(op->d_.handler()));
    }

    friend
    void asio_handler_deallocate(
        void* p, std::size_t size, accept_op* op)
    {
        using boost::asio::asio_handler_deallocate;
        asio_handler_deallocate(
            p, size, std::addressof(op->d_.handler()));
    }

    friend
    bool asio_handler_is_continuation(accept_op* op)
    {
        using boost::asio::asio_handler_is_continuation;
        return asio_handler_is_continuation(
            std::addressof(op->d_.handler()));
    }

    template<class Function>
    friend
    void asio_handler_invoke(Function&& f, accept_op* op)
    {
        using boost::asio::asio_handler_invoke;
        asio_handler_invoke(
            f, std::addressof(op->d_.handler()));
    }
};

template<class NextLayer>
template<class Decorator, class Handler>
void
stream<NextLayer>::accept_op<Decorator, Handler>::
operator()()
{
    auto& d = *d_;
    http::async_read_header(d.ws.next_layer(), 
        d.ws.stream_.buffer(), d.p,
            std::move(*this));
}

template<class NextLayer>
template<class Decorator, class Handler>
void
stream<NextLayer>::accept_op<Decorator, Handler>::
operator()(error_code ec)
{
    auto& d = *d_;
    if(! ec)
    {
        BOOST_ASSERT(d.p.is_header_done());
        // Arguments from our state must be
        // moved to the stack before releasing
        // the handler.
        auto& ws = d.ws;
        auto const req = d.p.release();
        auto const decorator = d.decorator;
    #if 1
        response_op<Handler>{
            d_.release_handler(),
                ws, req, decorator, true};
    #else
        // VFALCO This *should* work but breaks
        //        coroutine invariants in the unit test.
        //        Also it calls reset() when it shouldn't.
        ws.async_accept_ex(
            req, decorator, d_.release_handler());
    #endif
        return;
    }
    d_.invoke(ec);
}

//------------------------------------------------------------------------------

template<class NextLayer>
void
stream<NextLayer>::
accept()
{
    static_assert(is_sync_stream<next_layer_type>::value,
        "SyncStream requirements not met");
    error_code ec;
    accept(ec);
    if(ec)
        BOOST_THROW_EXCEPTION(system_error{ec});
}

template<class NextLayer>
template<class ResponseDecorator>
void
stream<NextLayer>::
accept_ex(ResponseDecorator const& decorator)
{
    static_assert(is_sync_stream<next_layer_type>::value,
        "SyncStream requirements not met");
    static_assert(detail::is_ResponseDecorator<
        ResponseDecorator>::value,
            "ResponseDecorator requirements not met");
    error_code ec;
    accept_ex(decorator, ec);
    if(ec)
        BOOST_THROW_EXCEPTION(system_error{ec});
}

template<class NextLayer>
void
stream<NextLayer>::
accept(error_code& ec)
{
    static_assert(is_sync_stream<next_layer_type>::value,
        "SyncStream requirements not met");
    reset();
    do_accept(&default_decorate_res, ec);
}

template<class NextLayer>
template<class ResponseDecorator>
void
stream<NextLayer>::
accept_ex(ResponseDecorator const& decorator, error_code& ec)
{
    static_assert(is_sync_stream<next_layer_type>::value,
        "SyncStream requirements not met");
    static_assert(detail::is_ResponseDecorator<
        ResponseDecorator>::value,
            "ResponseDecorator requirements not met");
    reset();
    do_accept(decorator, ec);
}

template<class NextLayer>
template<class ConstBufferSequence>
typename std::enable_if<! http::detail::is_header<
    ConstBufferSequence>::value>::type
stream<NextLayer>::
accept(ConstBufferSequence const& buffers)
{
    static_assert(is_sync_stream<next_layer_type>::value,
        "SyncStream requirements not met");
    static_assert(is_const_buffer_sequence<
        ConstBufferSequence>::value,
            "ConstBufferSequence requirements not met");
    error_code ec;
    accept(buffers, ec);
    if(ec)
        BOOST_THROW_EXCEPTION(system_error{ec});
}

template<class NextLayer>
template<
    class ConstBufferSequence, class ResponseDecorator>
typename std::enable_if<! http::detail::is_header<
    ConstBufferSequence>::value>::type
stream<NextLayer>::
accept_ex(ConstBufferSequence const& buffers,
    ResponseDecorator const &decorator)
{
    static_assert(is_sync_stream<next_layer_type>::value,
        "SyncStream requirements not met");
    static_assert(is_const_buffer_sequence<
        ConstBufferSequence>::value,
            "ConstBufferSequence requirements not met");
    static_assert(detail::is_ResponseDecorator<
        ResponseDecorator>::value,
            "ResponseDecorator requirements not met");
    error_code ec;
    accept_ex(buffers, decorator, ec);
    if(ec)
        BOOST_THROW_EXCEPTION(system_error{ec});
}

template<class NextLayer>
template<class ConstBufferSequence>
typename std::enable_if<! http::detail::is_header<
    ConstBufferSequence>::value>::type
stream<NextLayer>::
accept(ConstBufferSequence const& buffers, error_code& ec)
{
    static_assert(is_sync_stream<next_layer_type>::value,
        "SyncStream requirements not met");
    static_assert(is_const_buffer_sequence<
        ConstBufferSequence>::value,
            "ConstBufferSequence requirements not met");
    reset();
    using boost::asio::buffer_copy;
    using boost::asio::buffer_size;
    stream_.buffer().commit(buffer_copy(
        stream_.buffer().prepare(
            buffer_size(buffers)), buffers));
    do_accept(&default_decorate_res, ec);
}

template<class NextLayer>
template<
    class ConstBufferSequence, class ResponseDecorator>
typename std::enable_if<! http::detail::is_header<
    ConstBufferSequence>::value>::type
stream<NextLayer>::
accept_ex(ConstBufferSequence const& buffers,
    ResponseDecorator const& decorator, error_code& ec)
{
    static_assert(is_sync_stream<next_layer_type>::value,
        "SyncStream requirements not met");
    static_assert(is_const_buffer_sequence<
        ConstBufferSequence>::value,
            "ConstBufferSequence requirements not met");
    static_assert(is_const_buffer_sequence<
        ConstBufferSequence>::value,
            "ConstBufferSequence requirements not met");
    reset();
    using boost::asio::buffer_copy;
    using boost::asio::buffer_size;
    stream_.buffer().commit(buffer_copy(
        stream_.buffer().prepare(
            buffer_size(buffers)), buffers));
    do_accept(decorator, ec);
}

template<class NextLayer>
template<class Allocator>
void
stream<NextLayer>::
accept(http::header<true,
    http::basic_fields<Allocator>> const& req)
{
    static_assert(is_sync_stream<next_layer_type>::value,
        "SyncStream requirements not met");
    error_code ec;
    accept(req, ec);
    if(ec)
        BOOST_THROW_EXCEPTION(system_error{ec});
}

template<class NextLayer>
template<class Allocator, class ResponseDecorator>
void
stream<NextLayer>::
accept_ex(http::header<true,
    http::basic_fields<Allocator>> const& req,
    ResponseDecorator const& decorator)
{
    static_assert(is_sync_stream<next_layer_type>::value,
        "SyncStream requirements not met");
    static_assert(detail::is_ResponseDecorator<
        ResponseDecorator>::value,
            "ResponseDecorator requirements not met");
    error_code ec;
    accept_ex(req, decorator, ec);
    if(ec)
        BOOST_THROW_EXCEPTION(system_error{ec});
}

template<class NextLayer>
template<class Allocator>
void
stream<NextLayer>::
accept(http::header<true,
    http::basic_fields<Allocator>> const& req,
        error_code& ec)
{
    static_assert(is_sync_stream<next_layer_type>::value,
        "SyncStream requirements not met");
    reset();
    do_accept(req, &default_decorate_res, ec);
}

template<class NextLayer>
template<class Allocator, class ResponseDecorator>
void
stream<NextLayer>::
accept_ex(http::header<true,
    http::basic_fields<Allocator>> const& req,
        ResponseDecorator const& decorator, error_code& ec)
{
    static_assert(is_sync_stream<next_layer_type>::value,
        "SyncStream requirements not met");
    static_assert(detail::is_ResponseDecorator<
        ResponseDecorator>::value,
            "ResponseDecorator requirements not met");
    reset();
    do_accept(req, decorator, ec);
}

template<class NextLayer>
template<class Allocator, class ConstBufferSequence>
void
stream<NextLayer>::
accept(http::header<true,
    http::basic_fields<Allocator>> const& req,
        ConstBufferSequence const& buffers)
{
    static_assert(is_sync_stream<next_layer_type>::value,
        "SyncStream requirements not met");
    static_assert(is_const_buffer_sequence<
        ConstBufferSequence>::value,
            "ConstBufferSequence requirements not met");
    error_code ec;
    accept(req, buffers, ec);
    if(ec)
        BOOST_THROW_EXCEPTION(system_error{ec});
}

template<class NextLayer>
template<class Allocator,
    class ConstBufferSequence, class ResponseDecorator>
void
stream<NextLayer>::
accept_ex(http::header<true,
    http::basic_fields<Allocator>> const& req,
        ConstBufferSequence const& buffers,
            ResponseDecorator const& decorator)
{
    static_assert(is_sync_stream<next_layer_type>::value,
        "SyncStream requirements not met");
    static_assert(is_const_buffer_sequence<
        ConstBufferSequence>::value,
            "ConstBufferSequence requirements not met");
    static_assert(detail::is_ResponseDecorator<
        ResponseDecorator>::value,
            "ResponseDecorator requirements not met");
    error_code ec;
    accept_ex(req, buffers, decorator, ec);
    if(ec)
        BOOST_THROW_EXCEPTION(system_error{ec});
}

template<class NextLayer>
template<class Allocator, class ConstBufferSequence>
void
stream<NextLayer>::
accept(http::header<true,
    Allocator> const& req,
        ConstBufferSequence const& buffers, error_code& ec)
{
    static_assert(is_sync_stream<next_layer_type>::value,
        "SyncStream requirements not met");
    static_assert(is_const_buffer_sequence<
        ConstBufferSequence>::value,
            "ConstBufferSequence requirements not met");
    reset();
    using boost::asio::buffer_copy;
    using boost::asio::buffer_size;
    stream_.buffer().commit(buffer_copy(
        stream_.buffer().prepare(
            buffer_size(buffers)), buffers));
    do_accept(req, &default_decorate_res, ec);
}

template<class NextLayer>
template<class Allocator,
    class ConstBufferSequence, class ResponseDecorator>
void
stream<NextLayer>::
accept_ex(http::header<true,
    http::basic_fields<Allocator>> const& req,
        ConstBufferSequence const& buffers,
            ResponseDecorator const& decorator,
                error_code& ec)
{
    static_assert(is_sync_stream<next_layer_type>::value,
        "SyncStream requirements not met");
    static_assert(is_const_buffer_sequence<
        ConstBufferSequence>::value,
            "ConstBufferSequence requirements not met");
    static_assert(detail::is_ResponseDecorator<
        ResponseDecorator>::value,
            "ResponseDecorator requirements not met");
    reset();
    using boost::asio::buffer_copy;
    using boost::asio::buffer_size;
    stream_.buffer().commit(buffer_copy(
        stream_.buffer().prepare(
            buffer_size(buffers)), buffers));
    do_accept(req, decorator, ec);
}

//------------------------------------------------------------------------------

template<class NextLayer>
template<class AcceptHandler>
async_return_type<
    AcceptHandler, void(error_code)>
stream<NextLayer>::
async_accept(AcceptHandler&& handler)
{
    static_assert(is_async_stream<next_layer_type>::value,
        "AsyncStream requirements requirements not met");
    async_completion<AcceptHandler,
        void(error_code)> init{handler};
    reset();
    accept_op<decltype(&default_decorate_res),
        handler_type<AcceptHandler, void(error_code)>>{
            init.completion_handler, *this, &default_decorate_res}();
    return init.result.get();
}

template<class NextLayer>
template<class ResponseDecorator, class AcceptHandler>
async_return_type<
    AcceptHandler, void(error_code)>
stream<NextLayer>::
async_accept_ex(ResponseDecorator const& decorator,
    AcceptHandler&& handler)
{
    static_assert(is_async_stream<next_layer_type>::value,
        "AsyncStream requirements requirements not met");
    static_assert(detail::is_ResponseDecorator<
        ResponseDecorator>::value,
            "ResponseDecorator requirements not met");
    async_completion<AcceptHandler,
        void(error_code)> init{handler};
    reset();
    accept_op<ResponseDecorator, handler_type<
        AcceptHandler, void(error_code)>>{
            init.completion_handler, *this, decorator}();
    return init.result.get();
}

template<class NextLayer>
template<class ConstBufferSequence, class AcceptHandler>
typename std::enable_if<
    ! http::detail::is_header<ConstBufferSequence>::value,
    async_return_type<AcceptHandler, void(error_code)>>::type
stream<NextLayer>::
async_accept(ConstBufferSequence const& buffers,
    AcceptHandler&& handler)
{
    static_assert(is_async_stream<next_layer_type>::value,
        "AsyncStream requirements requirements not met");
    static_assert(is_const_buffer_sequence<
        ConstBufferSequence>::value,
            "ConstBufferSequence requirements not met");
    async_completion<AcceptHandler,
        void(error_code)> init{handler};
    reset();
    accept_op<decltype(&default_decorate_res),
        handler_type<AcceptHandler, void(error_code)>>{
            init.completion_handler, *this, buffers,
                &default_decorate_res}();
    return init.result.get();
}

template<class NextLayer>
template<class ConstBufferSequence,
    class ResponseDecorator, class AcceptHandler>
typename std::enable_if<
    ! http::detail::is_header<ConstBufferSequence>::value,
    async_return_type<AcceptHandler, void(error_code)>>::type
stream<NextLayer>::
async_accept_ex(ConstBufferSequence const& buffers,
    ResponseDecorator const& decorator,
        AcceptHandler&& handler)
{
    static_assert(is_async_stream<next_layer_type>::value,
        "AsyncStream requirements requirements not met");
    static_assert(is_const_buffer_sequence<
        ConstBufferSequence>::value,
            "ConstBufferSequence requirements not met");
    static_assert(detail::is_ResponseDecorator<
        ResponseDecorator>::value,
            "ResponseDecorator requirements not met");
    async_completion<AcceptHandler,
        void(error_code)> init{handler};
    reset();
    accept_op<ResponseDecorator, handler_type<
        AcceptHandler, void(error_code)>>{
            init.completion_handler, *this, buffers,
                decorator}();
    return init.result.get();
}

template<class NextLayer>
template<class Allocator, class AcceptHandler>
async_return_type<
    AcceptHandler, void(error_code)>
stream<NextLayer>::
async_accept(http::header<true,
    http::basic_fields<Allocator>> const& req,
        AcceptHandler&& handler)
{
    static_assert(is_async_stream<next_layer_type>::value,
        "AsyncStream requirements requirements not met");
    async_completion<AcceptHandler,
        void(error_code)> init{handler};
    reset();
    using boost::asio::asio_handler_is_continuation;
    response_op<handler_type<
        AcceptHandler, void(error_code)>>{init.completion_handler,
            *this, req, &default_decorate_res,
                asio_handler_is_continuation(
                    std::addressof(init.completion_handler))};
    return init.result.get();
}

template<class NextLayer>
template<class Allocator,
    class ResponseDecorator, class AcceptHandler>
async_return_type<
    AcceptHandler, void(error_code)>
stream<NextLayer>::
async_accept_ex(http::header<true,
    http::basic_fields<Allocator>> const& req,
        ResponseDecorator const& decorator, AcceptHandler&& handler)
{
    static_assert(is_async_stream<next_layer_type>::value,
        "AsyncStream requirements requirements not met");
    static_assert(detail::is_ResponseDecorator<
        ResponseDecorator>::value,
            "ResponseDecorator requirements not met");
    async_completion<AcceptHandler,
        void(error_code)> init{handler};
    reset();
    using boost::asio::asio_handler_is_continuation;
    response_op<handler_type<
        AcceptHandler, void(error_code)>>{
            init.completion_handler, *this, req, decorator,
                asio_handler_is_continuation(
                    std::addressof(init.completion_handler))};
    return init.result.get();
}

template<class NextLayer>
template<class Allocator,
    class ConstBufferSequence, class AcceptHandler>
async_return_type<
    AcceptHandler, void(error_code)>
stream<NextLayer>::
async_accept(http::header<true,
    http::basic_fields<Allocator>> const& req,
        ConstBufferSequence const& buffers,
            AcceptHandler&& handler)
{
    static_assert(is_async_stream<next_layer_type>::value,
        "AsyncStream requirements requirements not met");
    static_assert(is_const_buffer_sequence<
        ConstBufferSequence>::value,
            "ConstBufferSequence requirements not met");
    async_completion<AcceptHandler,
        void(error_code)> init{handler};
    reset();
    using boost::asio::asio_handler_is_continuation;
    response_op<handler_type<
        AcceptHandler, void(error_code)>>{
            init.completion_handler, *this, req, buffers,
                &default_decorate_res, asio_handler_is_continuation(
                    std::addressof(init.completion_handler))};
    return init.result.get();
}

template<class NextLayer>
template<class Allocator, class ConstBufferSequence,
    class ResponseDecorator, class AcceptHandler>
async_return_type<
    AcceptHandler, void(error_code)>
stream<NextLayer>::
async_accept_ex(http::header<true,
    http::basic_fields<Allocator>> const& req,
        ConstBufferSequence const& buffers,
            ResponseDecorator const& decorator,
                AcceptHandler&& handler)
{
    static_assert(is_async_stream<next_layer_type>::value,
        "AsyncStream requirements requirements not met");
    static_assert(is_const_buffer_sequence<
        ConstBufferSequence>::value,
            "ConstBufferSequence requirements not met");
    static_assert(detail::is_ResponseDecorator<
        ResponseDecorator>::value,
            "ResponseDecorator requirements not met");
    async_completion<AcceptHandler,
        void(error_code)> init{handler};
    reset();
    using boost::asio::asio_handler_is_continuation;
    response_op<handler_type<
        AcceptHandler, void(error_code)>>{init.completion_handler,
            *this, req, buffers, decorator, asio_handler_is_continuation(
                std::addressof(init.completion_handler))};
    return init.result.get();
}

} // websocket
} // beast

#endif
