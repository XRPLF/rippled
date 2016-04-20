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

#ifndef BEAST_WSPROTO_SSL_IPP_INCLUDED
#define BEAST_WSPROTO_SSL_IPP_INCLUDED

#include <beast/asio/async_completion.h>
#include <beast/asio/type_check.h>

namespace beast {
namespace wsproto {

namespace detail {

/*

See
http://stackoverflow.com/questions/32046034/what-is-the-proper-way-to-securely-disconnect-an-asio-ssl-socket/32054476#32054476

Behavior of ssl::stream regarding close_

    If the remote host calls async_shutdown then the
    local host's async_read will complete with eof.

    If both hosts call async_shutdown then the calls
    to async_shutdown will complete with eof.

*/
template<class AsyncStream, class Handler>
class teardown_ssl_op
{
    using stream_type =
        boost::asio::ssl::stream<AsyncStream>;

    struct data
    {
        stream_type& stream;
        Handler h;
        bool cont;
        int state = 0;

        template<class DeducedHandler>
        data(DeducedHandler&& h_,
                stream_type& stream_)
            : stream(stream_)
            , h(std::forward<DeducedHandler>(h_))
            , cont(boost_asio_handler_cont_helpers::
                is_continuation(h))
        {
        }
    };

    std::shared_ptr<data> d_;

public:
    template<class DeducedHandler>
    explicit
    teardown_ssl_op(
        DeducedHandler&& h,
            stream_type& stream)
        : d_(std::make_shared<data>(
            std::forward<DeducedHandler>(h),
                stream))
    {
        (*this)(error_code{}, false);
    }

    void
    operator()(error_code ec, bool again = true);

    friend
    auto asio_handler_allocate(std::size_t size,
        teardown_ssl_op* op)
    {
        return boost_asio_handler_alloc_helpers::
            allocate(size, op->d_->h);
    }

    friend
    auto asio_handler_deallocate(void* p,
        std::size_t size, teardown_ssl_op* op)
    {
        return boost_asio_handler_alloc_helpers::
            deallocate(p, size, op->d_->h);
    }

    friend
    auto asio_handler_is_continuation(
        teardown_ssl_op* op)
    {
        return op->d_->cont;
    }

    template <class Function>
    friend
    auto asio_handler_invoke(Function&& f,
        teardown_ssl_op* op)
    {
        return boost_asio_handler_invoke_helpers::
            invoke(f, op->d_->h);
    }
};

template<class AsyncStream, class Handler>
void
teardown_ssl_op<AsyncStream, Handler>::
operator()(error_code ec, bool again)
{
    auto& d = *d_;
    d.cont = d.cont || again;
    while(!ec && d.state != 99)
    {
        switch(d.state)
        {
        case 0:
            d.state = 99;
            d.stream.async_shutdown(*this);
            return;
        }
    }
    d.h(ec);
}

} // detail

//------------------------------------------------------------------------------

template<class AsyncStream>
void
teardown(
    boost::asio::ssl::stream<AsyncStream>& stream,
        error_code& ec)
{
    stream.shutdown(ec);
}

template<class AsyncStream, class TeardownHandler>
void
async_teardown(
    boost::asio::ssl::stream<AsyncStream>& stream,
        TeardownHandler&& handler)
{
    static_assert(beast::is_Handler<
        TeardownHandler, void(error_code)>::value,
            "TeardownHandler requirements not met");
    detail::teardown_ssl_op<AsyncStream, std::decay_t<
        TeardownHandler>>{std::forward<TeardownHandler>(
            handler), stream};
}

} // wsproto
} // beast

#endif
