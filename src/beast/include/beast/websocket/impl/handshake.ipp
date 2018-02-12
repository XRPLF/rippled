//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_WEBSOCKET_IMPL_HANDSHAKE_IPP
#define BEAST_WEBSOCKET_IMPL_HANDSHAKE_IPP

#include <beast/websocket/detail/type_traits.hpp>
#include <beast/http/empty_body.hpp>
#include <beast/http/message.hpp>
#include <beast/http/read.hpp>
#include <beast/http/write.hpp>
#include <beast/core/handler_ptr.hpp>
#include <beast/core/type_traits.hpp>
#include <boost/asio/handler_alloc_hook.hpp>
#include <boost/asio/handler_continuation_hook.hpp>
#include <boost/asio/handler_invoke_hook.hpp>
#include <boost/assert.hpp>
#include <boost/throw_exception.hpp>
#include <memory>

namespace beast {
namespace websocket {

//------------------------------------------------------------------------------

// send the upgrade request and process the response
//
template<class NextLayer>
template<class Handler>
class stream<NextLayer>::handshake_op
{
    struct data
    {
        bool cont;
        stream<NextLayer>& ws;
        response_type* res_p;
        detail::sec_ws_key_type key;
        http::request<http::empty_body> req;
        response_type res;
        int state = 0;

        template<class Decorator>
        data(Handler& handler, stream<NextLayer>& ws_,
            response_type* res_p_,
                string_view host,
                    string_view target,
                        Decorator const& decorator)
            : ws(ws_)
            , res_p(res_p_)
            , req(ws.build_request(key,
                host, target, decorator))
        {
            using boost::asio::asio_handler_is_continuation;
            cont = asio_handler_is_continuation(std::addressof(handler));
            ws.reset();
        }
    };

    handler_ptr<data, Handler> d_;

public:
    handshake_op(handshake_op&&) = default;
    handshake_op(handshake_op const&) = default;

    template<class DeducedHandler, class... Args>
    handshake_op(DeducedHandler&& h,
            stream<NextLayer>& ws, Args&&... args)
        : d_(std::forward<DeducedHandler>(h),
            ws, std::forward<Args>(args)...)
    {
        (*this)(error_code{}, false);
    }

    void
    operator()(error_code ec, bool again = true);

    friend
    void* asio_handler_allocate(
        std::size_t size, handshake_op* op)
    {
        using boost::asio::asio_handler_allocate;
        return asio_handler_allocate(
            size, std::addressof(op->d_.handler()));
    }

    friend
    void asio_handler_deallocate(
        void* p, std::size_t size, handshake_op* op)
    {
        using boost::asio::asio_handler_deallocate;
        asio_handler_deallocate(
            p, size, std::addressof(op->d_.handler()));
    }

    friend
    bool asio_handler_is_continuation(handshake_op* op)
    {
        return op->d_->cont;
    }

    template<class Function>
    friend
    void asio_handler_invoke(Function&& f, handshake_op* op)
    {
        using boost::asio::asio_handler_invoke;
        asio_handler_invoke(
            f, std::addressof(op->d_.handler()));
    }
};

template<class NextLayer>
template<class Handler>
void
stream<NextLayer>::handshake_op<Handler>::
operator()(error_code ec, bool again)
{
    auto& d = *d_;
    d.cont = d.cont || again;
    while(! ec && d.state != 99)
    {
        switch(d.state)
        {
        case 0:
        {
            // send http upgrade
            d.state = 1;
            // VFALCO Do we need the ability to move
            //        a message on the async_write?
            //
            pmd_read(d.ws.pmd_config_, d.req);
            http::async_write(d.ws.stream_,
                d.req, std::move(*this));
            // TODO We don't need d.req now. Figure
            // out a way to make it a parameter instead
            // of a state variable to reduce footprint.
            return;
        }

        // sent upgrade
        case 1:
            // read http response
            d.state = 2;
            http::async_read(d.ws.next_layer(),
                d.ws.stream_.buffer(), d.res,
                    std::move(*this));
            return;

        // got response
        case 2:
        {
            d.ws.do_response(d.res, d.key, ec);
            // call handler
            d.state = 99;
            break;
        }
        }
    }
    if(d.res_p)
        swap(d.res, *d.res_p);
    d_.invoke(ec);
}

template<class NextLayer>
template<class HandshakeHandler>
async_return_type<
    HandshakeHandler, void(error_code)>
stream<NextLayer>::
async_handshake(string_view host,
    string_view target,
        HandshakeHandler&& handler)
{
    static_assert(is_async_stream<next_layer_type>::value,
        "AsyncStream requirements not met");
    async_completion<HandshakeHandler,
        void(error_code)> init{handler};
    handshake_op<handler_type<
        HandshakeHandler, void(error_code)>>{
            init.completion_handler, *this, nullptr, host,
                target, &default_decorate_req};
    return init.result.get();
}

template<class NextLayer>
template<class HandshakeHandler>
async_return_type<
    HandshakeHandler, void(error_code)>
stream<NextLayer>::
async_handshake(response_type& res,
    string_view host,
        string_view target,
            HandshakeHandler&& handler)
{
    static_assert(is_async_stream<next_layer_type>::value,
        "AsyncStream requirements not met");
    async_completion<HandshakeHandler,
        void(error_code)> init{handler};
    handshake_op<handler_type<
        HandshakeHandler, void(error_code)>>{
            init.completion_handler, *this, &res, host,
                target, &default_decorate_req};
    return init.result.get();
}

template<class NextLayer>
template<class RequestDecorator, class HandshakeHandler>
async_return_type<
    HandshakeHandler, void(error_code)>
stream<NextLayer>::
async_handshake_ex(string_view host,
    string_view target,
        RequestDecorator const& decorator,
            HandshakeHandler&& handler)
{
    static_assert(is_async_stream<next_layer_type>::value,
        "AsyncStream requirements not met");
    static_assert(detail::is_RequestDecorator<
            RequestDecorator>::value,
        "RequestDecorator requirements not met");
    async_completion<HandshakeHandler,
        void(error_code)> init{handler};
    handshake_op<handler_type<
        HandshakeHandler, void(error_code)>>{
            init.completion_handler, *this, nullptr, host,
                target, decorator};
    return init.result.get();
}

template<class NextLayer>
template<class RequestDecorator, class HandshakeHandler>
async_return_type<
    HandshakeHandler, void(error_code)>
stream<NextLayer>::
async_handshake_ex(response_type& res,
    string_view host,
        string_view target,
            RequestDecorator const& decorator,
                HandshakeHandler&& handler)
{
    static_assert(is_async_stream<next_layer_type>::value,
        "AsyncStream requirements not met");
    static_assert(detail::is_RequestDecorator<
            RequestDecorator>::value,
        "RequestDecorator requirements not met");
    async_completion<HandshakeHandler,
        void(error_code)> init{handler};
    handshake_op<handler_type<
        HandshakeHandler, void(error_code)>>{
            init.completion_handler, *this, &res, host,
                target, decorator};
    return init.result.get();
}

template<class NextLayer>
void
stream<NextLayer>::
handshake(string_view host,
    string_view target)
{
    static_assert(is_sync_stream<next_layer_type>::value,
        "SyncStream requirements not met");
    error_code ec;
    handshake(
        host, target, ec);
    if(ec)
        BOOST_THROW_EXCEPTION(system_error{ec});
}

template<class NextLayer>
void
stream<NextLayer>::
handshake(response_type& res,
    string_view host,
        string_view target)
{
    static_assert(is_sync_stream<next_layer_type>::value,
        "SyncStream requirements not met");
    error_code ec;
    handshake(res, host, target, ec);
    if(ec)
        BOOST_THROW_EXCEPTION(system_error{ec});
}

template<class NextLayer>
template<class RequestDecorator>
void
stream<NextLayer>::
handshake_ex(string_view host,
    string_view target,
        RequestDecorator const& decorator)
{
    static_assert(is_sync_stream<next_layer_type>::value,
        "SyncStream requirements not met");
    static_assert(detail::is_RequestDecorator<
            RequestDecorator>::value,
        "RequestDecorator requirements not met");
    error_code ec;
    handshake_ex(host, target, decorator, ec);
    if(ec)
        BOOST_THROW_EXCEPTION(system_error{ec});
}

template<class NextLayer>
template<class RequestDecorator>
void
stream<NextLayer>::
handshake_ex(response_type& res,
    string_view host,
        string_view target,
            RequestDecorator const& decorator)
{
    static_assert(is_sync_stream<next_layer_type>::value,
        "SyncStream requirements not met");
    static_assert(detail::is_RequestDecorator<
            RequestDecorator>::value,
        "RequestDecorator requirements not met");
    error_code ec;
    handshake_ex(res, host, target, decorator, ec);
    if(ec)
        BOOST_THROW_EXCEPTION(system_error{ec});
}

template<class NextLayer>
void
stream<NextLayer>::
handshake(string_view host,
    string_view target, error_code& ec)
{
    static_assert(is_sync_stream<next_layer_type>::value,
        "SyncStream requirements not met");
    do_handshake(nullptr,
        host, target, &default_decorate_req, ec);
}

template<class NextLayer>
void
stream<NextLayer>::
handshake(response_type& res,
    string_view host,
        string_view target,
            error_code& ec)
{
    static_assert(is_sync_stream<next_layer_type>::value,
        "SyncStream requirements not met");
    do_handshake(&res,
        host, target, &default_decorate_req, ec);
}

template<class NextLayer>
template<class RequestDecorator>
void
stream<NextLayer>::
handshake_ex(string_view host,
    string_view target,
        RequestDecorator const& decorator,
            error_code& ec)
{
    static_assert(is_sync_stream<next_layer_type>::value,
        "SyncStream requirements not met");
    static_assert(detail::is_RequestDecorator<
            RequestDecorator>::value,
        "RequestDecorator requirements not met");
    do_handshake(nullptr,
        host, target, decorator, ec);
}

template<class NextLayer>
template<class RequestDecorator>
void
stream<NextLayer>::
handshake_ex(response_type& res,
    string_view host,
        string_view target,
            RequestDecorator const& decorator,
                error_code& ec)
{
    static_assert(is_sync_stream<next_layer_type>::value,
        "SyncStream requirements not met");
    static_assert(detail::is_RequestDecorator<
            RequestDecorator>::value,
        "RequestDecorator requirements not met");
    do_handshake(&res,
        host, target, decorator, ec);
}

//------------------------------------------------------------------------------

} // websocket
} // beast

#endif
