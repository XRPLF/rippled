//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_WEBSOCKET_IMPL_HANDSHAKE_IPP
#define BEAST_WEBSOCKET_IMPL_HANDSHAKE_IPP

#include <beast/http/empty_body.hpp>
#include <beast/http/message.hpp>
#include <beast/http/read.hpp>
#include <beast/http/write.hpp>
#include <beast/core/handler_helpers.hpp>
#include <beast/core/handler_ptr.hpp>
#include <beast/core/stream_concepts.hpp>
#include <boost/assert.hpp>
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
        std::string key;
        http::request<http::empty_body> req;
        http::response<http::string_body> resp;
        int state = 0;

        data(Handler& handler, stream<NextLayer>& ws_,
            boost::string_ref const& host,
                boost::string_ref const& resource)
            : cont(beast_asio_helpers::
                is_continuation(handler))
            , ws(ws_)
            , req(ws.build_request(host, resource, key))
        {
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
        return beast_asio_helpers::
            allocate(size, op->d_.handler());
    }

    friend
    void asio_handler_deallocate(
        void* p, std::size_t size, handshake_op* op)
    {
        return beast_asio_helpers::
            deallocate(p, size, op->d_.handler());
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
        return beast_asio_helpers::
            invoke(f, op->d_.handler());
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
            pmd_read(
                d.ws.pmd_config_, d.req.fields);
            http::async_write(d.ws.stream_,
                d.req, std::move(*this));
            return;
        }

        // sent upgrade
        case 1:
            // read http response
            d.state = 2;
            http::async_read(d.ws.next_layer(),
                d.ws.stream_.buffer(), d.resp,
                    std::move(*this));
            return;

        // got response
        case 2:
        {
            d.ws.do_response(d.resp, d.key, ec);
            // call handler
            d.state = 99;
            break;
        }
        }
    }
    d_.invoke(ec);
}

template<class NextLayer>
template<class HandshakeHandler>
typename async_completion<
    HandshakeHandler, void(error_code)>::result_type
stream<NextLayer>::
async_handshake(boost::string_ref const& host,
    boost::string_ref const& resource, HandshakeHandler&& handler)
{
    static_assert(is_AsyncStream<next_layer_type>::value,
        "AsyncStream requirements not met");
    beast::async_completion<
        HandshakeHandler, void(error_code)
            > completion{handler};
    handshake_op<decltype(completion.handler)>{
        completion.handler, *this, host, resource};
    return completion.result.get();
}

template<class NextLayer>
void
stream<NextLayer>::
handshake(boost::string_ref const& host,
    boost::string_ref const& resource)
{
    static_assert(is_SyncStream<next_layer_type>::value,
        "SyncStream requirements not met");
    error_code ec;
    handshake(host, resource, ec);
    if(ec)
        throw system_error{ec};
}

template<class NextLayer>
void
stream<NextLayer>::
handshake(boost::string_ref const& host,
    boost::string_ref const& resource, error_code& ec)
{
    static_assert(is_SyncStream<next_layer_type>::value,
        "SyncStream requirements not met");
    reset();
    std::string key;
    {
        auto const req =
            build_request(host, resource, key);
        pmd_read(pmd_config_, req.fields);
        http::write(stream_, req, ec);
    }
    if(ec)
        return;
    http::response<http::string_body> res;
    http::read(next_layer(), stream_.buffer(), res, ec);
    if(ec)
        return;
    do_response(res, key, ec);
}

//------------------------------------------------------------------------------

} // websocket
} // beast

#endif
