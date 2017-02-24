//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_WEBSOCKET_IMPL_SSL_IPP_INCLUDED
#define BEAST_WEBSOCKET_IMPL_SSL_IPP_INCLUDED

#include <beast/core/async_completion.hpp>
#include <beast/core/handler_helpers.hpp>
#include <beast/core/handler_concepts.hpp>
#include <beast/core/handler_ptr.hpp>

namespace beast {
namespace websocket {

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
        bool cont;
        stream_type& stream;
        int state = 0;

        data(Handler& handler, stream_type& stream_)
            : cont(beast_asio_helpers::
                is_continuation(handler))
            , stream(stream_)
        {
        }
    };

    handler_ptr<data, Handler> d_;

public:
    template<class DeducedHandler>
    explicit
    teardown_ssl_op(
            DeducedHandler&& h, stream_type& stream)
        : d_(std::forward<DeducedHandler>(h), stream)
    {
        (*this)(error_code{}, false);
    }

    void
    operator()(error_code ec, bool again = true);

    friend
    void* asio_handler_allocate(std::size_t size,
        teardown_ssl_op* op)
    {
        return beast_asio_helpers::
            allocate(size, op->d_.handler());
    }

    friend
    void asio_handler_deallocate(void* p,
        std::size_t size, teardown_ssl_op* op)
    {
        return beast_asio_helpers::
            deallocate(p, size, op->d_.handler());
    }

    friend
    bool asio_handler_is_continuation(
        teardown_ssl_op* op)
    {
        return op->d_->cont;
    }

    template<class Function>
    friend
    void asio_handler_invoke(Function&& f,
        teardown_ssl_op* op)
    {
        return beast_asio_helpers::
            invoke(f, op->d_.handler());
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
    d_.invoke(ec);
}

} // detail

//------------------------------------------------------------------------------

template<class AsyncStream>
void
teardown(teardown_tag,
    boost::asio::ssl::stream<AsyncStream>& stream,
        error_code& ec)
{
    stream.shutdown(ec);
}

template<class AsyncStream, class TeardownHandler>
void
async_teardown(teardown_tag,
    boost::asio::ssl::stream<AsyncStream>& stream,
        TeardownHandler&& handler)
{
    static_assert(beast::is_CompletionHandler<
        TeardownHandler, void(error_code)>::value,
            "TeardownHandler requirements not met");
    detail::teardown_ssl_op<AsyncStream, typename std::decay<
        TeardownHandler>::type>{std::forward<TeardownHandler>(
            handler), stream};
}

} // websocket
} // beast

#endif
