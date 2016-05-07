//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_HTTP_STREAM_IPP_INCLUDED
#define BEAST_HTTP_STREAM_IPP_INCLUDED

#include <beast/core/bind_handler.hpp>
#include <beast/core/handler_alloc.hpp>
#include <beast/http/message_v1.hpp>
#include <beast/http/read.hpp>
#include <beast/http/write.hpp>
#include <cassert>

namespace beast {
namespace http {

template<class NextLayer, class Allocator>
template<bool isRequest, class Body, class Headers,
    class Handler>
class stream<NextLayer, Allocator>::read_op
{
    using alloc_type =
        handler_alloc<char, Handler>;

    struct data
    {
        stream<NextLayer>& s;
        message_v1<isRequest, Body, Headers>& m;
        Handler h;
        bool cont;
        int state = 0;

        template<class DeducedHandler>
        data(DeducedHandler&& h_, stream<NextLayer>& s_,
                message_v1<isRequest, Body, Headers>& m_)
            : s(s_)
            , m(m_)
            , h(std::forward<DeducedHandler>(h_))
            , cont(boost_asio_handler_cont_helpers::
                is_continuation(h))
        {
        }
    };

    std::shared_ptr<data> d_;

public:
    read_op(read_op&&) = default;
    read_op(read_op const&) = default;

    template<class DeducedHandler, class... Args>
    read_op(DeducedHandler&& h,
            stream<NextLayer>& s, Args&&... args)
        : d_(std::allocate_shared<data>(alloc_type{h},
            std::forward<DeducedHandler>(h), s,
                std::forward<Args>(args)...))
    {
        (*this)(error_code{}, false);
    }

    void operator()(error_code const& ec, bool again = true);

    friend
    void* asio_handler_allocate(
        std::size_t size, read_op* op)
    {
        return boost_asio_handler_alloc_helpers::
            allocate(size, op->d_->h);
    }

    friend
    void asio_handler_deallocate(
        void* p, std::size_t size, read_op* op)
    {
        return boost_asio_handler_alloc_helpers::
            deallocate(p, size, op->d_->h);
    }

    friend
    bool asio_handler_is_continuation(read_op* op)
    {
        return op->d_->cont;
    }

    template <class Function>
    friend
    void asio_handler_invoke(Function&& f, read_op* op)
    {
        return boost_asio_handler_invoke_helpers::
            invoke(f, op->d_->h);
    }
};

template<class NextLayer, class Allocator>
template<bool isRequest, class Body, class Headers, class Handler>
void
stream<NextLayer, Allocator>::
read_op<isRequest, Body, Headers, Handler>::
operator()(error_code const& ec, bool again)
{
    auto& d = *d_;
    d.cont = d.cont || again;
    while(! ec && d.state != 99)
    {
        switch(d.state)
        {
        case 0:
            d.state = 99;
            beast::http::async_read(d.s.next_layer_,
                d.s.rd_buf_, d.m, std::move(*this));
            return;
        }
    }
    d.h(ec);
}

//------------------------------------------------------------------------------

template<class NextLayer, class Allocator>
template<bool isRequest, class Body, class Headers,
    class Handler>
class stream<NextLayer, Allocator>::write_op : public op
{
    using alloc_type =
        handler_alloc<char, Handler>;

    struct data
    {
        stream<NextLayer>& s;
        message_v1<isRequest, Body, Headers> m;
        Handler h;
        bool cont;
        int state = 0;

        template<class DeducedHandler>
        data(DeducedHandler&& h_, stream<NextLayer>& s_,
            message_v1<isRequest, Body, Headers> const& m_,
                bool cont_)
            : s(s_)
            , m(m_)
            , h(std::forward<DeducedHandler>(h_))
            , cont(cont_)
        {
        }

        template<class DeducedHandler>
        data(DeducedHandler&& h_, stream<NextLayer>& s_,
            message_v1<isRequest, Body, Headers>&& m_,
                bool cont_)
            : s(s_)
            , m(std::move(m_))
            , h(std::forward<DeducedHandler>(h_))
            , cont(cont_)
        {
        }
    };

    std::shared_ptr<data> d_;

public:
    write_op(write_op&&) = default;
    write_op(write_op const&) = default;

    template<class DeducedHandler, class... Args>
    write_op(DeducedHandler&& h,
        stream<NextLayer>& s, Args&&... args)
        : d_(std::allocate_shared<data>(alloc_type{h},
            std::forward<DeducedHandler>(h), s,
                std::forward<Args>(args)...))
    {
    }

    void
    operator()() override
    {
        (*this)(error_code{}, false);
    }

    void cancel() override;

    void operator()(error_code const& ec, bool again = true);

    friend
    void* asio_handler_allocate(
        std::size_t size, write_op* op)
    {
        return boost_asio_handler_alloc_helpers::
            allocate(size, op->d_->h);
    }

    friend
    void asio_handler_deallocate(
        void* p, std::size_t size, write_op* op)
    {
        return boost_asio_handler_alloc_helpers::
            deallocate(p, size, op->d_->h);
    }

    friend
    bool asio_handler_is_continuation(write_op* op)
    {
        return op->d_->cont;
    }

    template <class Function>
    friend
    void asio_handler_invoke(Function&& f, write_op* op)
    {
        return boost_asio_handler_invoke_helpers::
            invoke(f, op->d_->h);
    }
};

template<class NextLayer, class Allocator>
template<bool isRequest, class Body, class Headers, class Handler>
void
stream<NextLayer, Allocator>::
write_op<isRequest, Body, Headers, Handler>::
cancel()
{
    auto& d = *d_;
    d.s.get_io_service().post(
        bind_handler(std::move(*this),
            boost::asio::error::operation_aborted));
}

template<class NextLayer, class Allocator>
template<bool isRequest, class Body, class Headers, class Handler>
void
stream<NextLayer, Allocator>::
write_op<isRequest, Body, Headers, Handler>::
operator()(error_code const& ec, bool again)
{
    auto& d = *d_;
    d.cont = d.cont || again;
    while(! ec && d.state != 99)
    {
        switch(d.state)
        {
        case 0:
            d.state = 99;
            beast::http::async_write(d.s.next_layer_,
                d.m, std::move(*this));
            return;
        }
    }
    d.h(ec);
    if(! d.s.wr_q_.empty())
    {
        auto& op = d.s.wr_q_.front();
        op();
        // VFALCO Use allocator
        delete &op;
        d.s.wr_q_.pop_front();
    }
    else
    {
        d.s.wr_active_ = false;
    }
}

//------------------------------------------------------------------------------

template<class NextLayer, class Allocator>
stream<NextLayer, Allocator>::
~stream()
{
    // Can't destroy with pending operations!
    assert(wr_q_.empty());
}

template<class NextLayer, class Allocator>
template<class... Args>
stream<NextLayer, Allocator>::
stream(Args&&... args)
    : next_layer_(std::forward<Args>(args)...)
{
}

template<class NextLayer, class Allocator>
void
stream<NextLayer, Allocator>::
cancel(error_code& ec)
{
    cancel_all();
    lowest_layer().cancel(ec);
}

template<class NextLayer, class Allocator>
template<bool isRequest, class Body, class Headers>
void
stream<NextLayer, Allocator>::
read(message_v1<isRequest, Body, Headers>& msg,
    error_code& ec)
{
    beast::http::read(next_layer_, rd_buf_, msg, ec);
}

template<class NextLayer, class Allocator>
template<bool isRequest, class Body, class Headers,
    class ReadHandler>
auto
stream<NextLayer, Allocator>::
async_read(message_v1<isRequest, Body, Headers>& msg,
    ReadHandler&& handler) ->
        typename async_completion<
            ReadHandler, void(error_code)>::result_type
{
    async_completion<
        ReadHandler, void(error_code)
            > completion(handler);
    read_op<isRequest, Body, Headers,
        decltype(completion.handler)>{
            completion.handler, *this, msg};
    return completion.result.get();
}

template<class NextLayer, class Allocator>
template<bool isRequest, class Body, class Headers>
void
stream<NextLayer, Allocator>::
write(message_v1<isRequest, Body, Headers> const& msg,
    error_code& ec)
{
    beast::http::write(next_layer_, msg, ec);
}

template<class NextLayer, class Allocator>
template<bool isRequest, class Body, class Headers,
    class WriteHandler>
auto
stream<NextLayer, Allocator>::
async_write(message_v1<isRequest, Body, Headers> const& msg,
    WriteHandler&& handler) ->
        typename async_completion<
            WriteHandler, void(error_code)>::result_type
{
    async_completion<
        WriteHandler, void(error_code)> completion(handler);
    auto const cont = wr_active_ ||
        boost_asio_handler_cont_helpers::is_continuation(handler);
    if(! wr_active_)
    {
        wr_active_ = true;
        write_op<isRequest, Body, Headers,
            decltype(completion.handler)>{
                completion.handler, *this, msg, cont }();
    }
    else
    {
        // VFALCO Use allocator
        wr_q_.push_back(*new write_op<isRequest, Body, Headers,
            decltype(completion.handler)>(
                completion.handler, *this, msg, cont));
    }
    return completion.result.get();
}

template<class NextLayer, class Allocator>
template<bool isRequest, class Body, class Headers,
    class WriteHandler>
auto
stream<NextLayer, Allocator>::
async_write(message_v1<isRequest, Body, Headers>&& msg,
    WriteHandler&& handler) ->
        typename async_completion<
            WriteHandler, void(error_code)>::result_type
{
    async_completion<
        WriteHandler, void(error_code)> completion(handler);
    auto const cont = wr_active_ ||
        boost_asio_handler_cont_helpers::is_continuation(handler);
    if(! wr_active_)
    {
        wr_active_ = true;
        write_op<isRequest, Body, Headers,
            decltype(completion.handler)>{completion.handler,
            *this, std::move(msg), cont}();
    }
    else
    {
        // VFALCO Use allocator
        wr_q_.push_back(*new write_op<isRequest, Body, Headers,
            decltype(completion.handler)>(completion.handler,
                *this, std::move(msg), cont));
    }
    return completion.result.get();
}

template<class NextLayer, class Allocator>
void
stream<NextLayer, Allocator>::
cancel_all()
{
    for(auto it = wr_q_.begin(); it != wr_q_.end();)
    {
        auto& op = *it++;
        op.cancel();
        // VFALCO Use allocator
        delete &op;
    }
    wr_q_.clear();
}

} // http
} // beast

#endif
