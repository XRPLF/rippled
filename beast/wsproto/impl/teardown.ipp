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

#ifndef BEAST_WSPROTO_TEARDOWN_IPP_INCLUDED
#define BEAST_WSPROTO_TEARDOWN_IPP_INCLUDED

#include <beast/asio/async_completion.h>
#include <beast/asio/type_check.h>
#include <memory>

namespace beast {
namespace wsproto {

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
        bool cont;
        int state = 0;

        template<class DeducedHandler>
        data(DeducedHandler&& h_,
                socket_type& socket_)
            : socket(socket_)
            , h(std::forward<DeducedHandler>(h_))
            , cont(boost_asio_handler_cont_helpers::
                is_continuation(h))
        {
        }
    };

    std::shared_ptr<data> d_;

public:
    template<class DeducedHandler>
    teardown_tcp_op(
        DeducedHandler&& h,
            socket_type& socket)
        : d_(std::make_shared<data>(
            std::forward<DeducedHandler>(h),
                socket))
    {
        (*this)(error_code{}, 0, false);
    }

    void
    operator()(
        error_code ec, std::size_t, bool again = true);

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
    auto asio_handler_is_continuation(teardown_tcp_op* op)
    {
        return op->d_->cont;
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
    d.h(ec);
}

} // detail

//------------------------------------------------------------------------------

inline
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
async_teardown(
    boost::asio::ip::tcp::socket& socket,
        TeardownHandler&& handler)
{
    static_assert(beast::is_Handler<
        TeardownHandler, void(error_code)>::value,
            "TeardownHandler requirements not met");
    detail::teardown_tcp_op<std::decay_t<
        TeardownHandler>>{std::forward<
            TeardownHandler>(handler), socket};
}

} // wsproto
} // beast

#endif
