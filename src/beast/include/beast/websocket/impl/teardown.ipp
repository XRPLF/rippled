//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_WEBSOCKET_IMPL_TEARDOWN_IPP
#define BEAST_WEBSOCKET_IMPL_TEARDOWN_IPP

#include <beast/core/async_result.hpp>
#include <beast/core/handler_ptr.hpp>
#include <beast/core/type_traits.hpp>
#include <boost/asio/handler_alloc_hook.hpp>
#include <boost/asio/handler_continuation_hook.hpp>
#include <boost/asio/handler_invoke_hook.hpp>
#include <memory>

namespace beast {
namespace websocket {

namespace detail {

template<class Handler>
class teardown_tcp_op
{
    using socket_type =
        boost::asio::ip::tcp::socket;

    struct data
    {
        bool cont;
        socket_type& socket;
        char buf[2048];
        int state = 0;

        data(Handler& handler, socket_type& socket_)
            : socket(socket_)
        {
            using boost::asio::asio_handler_is_continuation;
            cont = asio_handler_is_continuation(std::addressof(handler));
        }
    };

    handler_ptr<data, Handler> d_;

public:
    template<class DeducedHandler>
    teardown_tcp_op(
        DeducedHandler&& h,
            socket_type& socket)
        : d_(std::forward<DeducedHandler>(h), socket)
    {
        (*this)(error_code{}, 0, false);
    }

    void
    operator()(error_code ec, std::size_t, bool again = true);

    friend
    void* asio_handler_allocate(std::size_t size,
        teardown_tcp_op* op)
    {
        using boost::asio::asio_handler_allocate;
        return asio_handler_allocate(
            size, std::addressof(op->d_.handler()));
    }

    friend
    void asio_handler_deallocate(void* p,
        std::size_t size, teardown_tcp_op* op)
    {
        using boost::asio::asio_handler_deallocate;
        asio_handler_deallocate(
            p, size, std::addressof(op->d_.handler()));
    }

    friend
    bool asio_handler_is_continuation(teardown_tcp_op* op)
    {
        return op->d_->cont;
    }

    template<class Function>
    friend
    void asio_handler_invoke(Function&& f,
        teardown_tcp_op* op)
    {
        using boost::asio::asio_handler_invoke;
        asio_handler_invoke(
            f, std::addressof(op->d_.handler()));
    }
};

template<class Handler>
void
teardown_tcp_op<Handler>::
operator()(error_code ec, std::size_t, bool again)
{
    using boost::asio::buffer;
    auto& d = *d_;
    d.cont = d.cont || again;
    while(! ec)
    {
        switch(d.state)
        {
        case 0:
            d.state = 1;
            d.socket.shutdown(
                boost::asio::ip::tcp::socket::shutdown_send, ec);
            break;

        case 1:
            d.socket.async_read_some(
                buffer(d.buf), std::move(*this));
            return;
        }
    }
    if(ec == boost::asio::error::eof)
    {
        d.socket.close(ec);
        ec = error_code{};
    }
    d_.invoke(ec);
}

} // detail

//------------------------------------------------------------------------------

inline
void
teardown(teardown_tag,
    boost::asio::ip::tcp::socket& socket,
        error_code& ec)
{
    using boost::asio::buffer;
    socket.shutdown(
        boost::asio::ip::tcp::socket::shutdown_send, ec);
    while(! ec)
    {
        char buf[8192];
        auto const n = socket.read_some(
            buffer(buf), ec);
        if(! n)
            break;
    }
    if(ec == boost::asio::error::eof)
        ec = error_code{};
    socket.close(ec);
}

template<class TeardownHandler>
inline
void
async_teardown(teardown_tag,
    boost::asio::ip::tcp::socket& socket,
        TeardownHandler&& handler)
{
    static_assert(beast::is_completion_handler<
        TeardownHandler, void(error_code)>::value,
            "TeardownHandler requirements not met");
    detail::teardown_tcp_op<typename std::decay<
        TeardownHandler>::type>{std::forward<
            TeardownHandler>(handler), socket};
}

} // websocket
} // beast

#endif
