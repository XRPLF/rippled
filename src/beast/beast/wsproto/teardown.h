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

#ifndef BEAST_WSPROTO_TEARDOWN_H_INCLUDED
#define BEAST_WSPROTO_TEARDOWN_H_INCLUDED

#include <beast/wsproto/error.h>
#include <beast/asio/async_completion.h>
#include <beast/asio/type_check.h>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <memory>

namespace beast {
namespace wsproto {

// Helper assists in graceful closure

template<class Socket>
void
teardown(Socket& socket)
{
    error_code ec;
    teardown(socket, ec);
    if(ec)
        throw boost::system::system_error{ec};
}

// Requires: caller-provided overload
template<class Socket>
void
teardown(Socket& socket, error_code& ec) = delete;

// Requires: caller-provided overload
template<class AsyncSocket, class TeardownHandler>
void
async_teardown(AsyncSocket& socket, TeardownHandler&& handler) = delete;

//------------------------------------------------------------------------------

namespace detail {

template<class Handler>
class teardown_tcp_op
{
    using socket_type =
        boost::asio::ip::tcp::socket;

    struct data
    {
        socket_type& socket;
        Handler h;
        char buf[8192];
        int state = 0;

        template<class DeducedHandler>
        data(DeducedHandler&& h_,
                socket_type& socket_)
            : socket(socket_)
            , h(std::forward<DeducedHandler>(h_))
        {
        }
    };

    std::shared_ptr<data> d_;

public:
    template<class DeducedHandler>
    explicit
    teardown_tcp_op(
        DeducedHandler&& h,
            socket_type& socket)
        : d_(std::make_shared<data>(
            std::forward<DeducedHandler>(h),
                socket))
    {
        (*this)(error_code{}, 0);
    }

    void
    operator()(error_code ec, std::size_t)
    {
        using boost::asio::buffer;
        auto& d = *d_;
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
        d.h(ec);
    }

    friend
    auto asio_handler_allocate(std::size_t size,
        teardown_tcp_op* op)
    {
        return boost_asio_handler_alloc_helpers::
            allocate(size, op->d_->h);
    }

    friend
    auto asio_handler_deallocate(void* p,
        std::size_t size, teardown_tcp_op* op)
    {
        return boost_asio_handler_alloc_helpers::
            deallocate(p, size, op->d_->h);
    }

    friend
    auto asio_handler_is_continuation(
        teardown_tcp_op* op)
    {
        return op->d_->state >= 1 ||
            boost_asio_handler_cont_helpers::
                is_continuation(op->d_->h);
    }

    template <class Function>
    friend
    auto asio_handler_invoke(Function&& f,
        teardown_tcp_op* op)
    {
        return boost_asio_handler_invoke_helpers::
            invoke(f, op->d_->h);
    }
};

} // detail

template<class = void>
void
teardown(
    boost::asio::ip::tcp::socket& socket,
        error_code& ec)
{
    using boost::asio::buffer;
    socket.shutdown(
        boost::asio::ip::tcp::socket::shutdown_send, ec);
    while(! ec)
    {
        char buf[65536];
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
auto
async_teardown(
    boost::asio::ip::tcp::socket& socket,
        TeardownHandler&& handler)
{
    static_assert(beast::is_Handler<
        TeardownHandler, void(error_code)>::value,
            "TeardownHandler requirements not met");
    beast::async_completion<
        TeardownHandler, void(error_code)
            > completion(handler);
    detail::teardown_tcp_op<
        decltype(completion.handler)>{
            completion.handler, socket};
    return completion.result.get();
}

//------------------------------------------------------------------------------

template<class Stream>
void
teardown(
    boost::asio::ssl::stream<Stream>& stream,
        error_code& ec)
{
    stream.shutdown(ec);
}

template<class Stream, class TeardownHandler>
auto
async_teardown(
    boost::asio::ssl::stream<Stream>& stream,
        TeardownHandler&& handler)
{
    static_assert(beast::is_Handler<
        TeardownHandler, void(error_code)>::value,
            "TeardownHandler requirements not met");
    return stream.async_shutdown(
        std::forward<TeardownHandler>(handler));
}

} // wsproto

//------------------------------------------------------------------------------

namespace wsproto_helpers {

// Calls to teardown and async_teardown must be made from
// a namespace that does not contain any overloads of these
// functions. The wsproto_helpers namespace is defined here
// for that purpose.

template<class Socket>
void
call_teardown(Socket& socket, wsproto::error_code& ec)
{
    using wsproto::teardown;
    teardown(socket, ec);
}

template<class Socket, class TeardownHandler>
// VFALCO use return_type declval here for async_result
void
call_async_teardown(Socket& socket, TeardownHandler&& handler)
{
    using wsproto::async_teardown;
    return async_teardown(
        socket,std::forward<TeardownHandler>(handler));
}

} // wsproto_helpers

} // beast

#endif
