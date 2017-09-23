//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_IMPL_BUFFERED_READ_STREAM_IPP
#define BEAST_IMPL_BUFFERED_READ_STREAM_IPP

#include <beast/core/bind_handler.hpp>
#include <beast/core/error.hpp>
#include <beast/core/handler_ptr.hpp>
#include <beast/core/type_traits.hpp>
#include <beast/core/detail/config.hpp>
#include <boost/asio/handler_alloc_hook.hpp>
#include <boost/asio/handler_continuation_hook.hpp>
#include <boost/asio/handler_invoke_hook.hpp>
#include <boost/throw_exception.hpp>

namespace beast {

template<class Stream, class DynamicBuffer>
template<class MutableBufferSequence, class Handler>
class buffered_read_stream<
    Stream, DynamicBuffer>::read_some_op
{
    int step_ = 0;
    buffered_read_stream& s_;
    MutableBufferSequence b_;
    Handler h_;

public:
    read_some_op(read_some_op&&) = default;
    read_some_op(read_some_op const&) = default;

    template<class DeducedHandler, class... Args>
    read_some_op(DeducedHandler&& h,
        buffered_read_stream& s,
            MutableBufferSequence const& b)
        : s_(s)
        , b_(b)
        , h_(std::forward<DeducedHandler>(h))
    {
    }

    void
    operator()(error_code const& ec,
        std::size_t bytes_transferred);

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
        return asio_handler_is_continuation(
            std::addressof(op->h_));
    }

    template<class Function>
    friend
    void asio_handler_invoke(Function&& f, read_some_op* op)
    {
        using boost::asio::asio_handler_invoke;
        asio_handler_invoke(f, std::addressof(op->h_));
    }
};

template<class Stream, class DynamicBuffer>
template<class MutableBufferSequence, class Handler>
void
buffered_read_stream<Stream, DynamicBuffer>::
read_some_op<MutableBufferSequence, Handler>::operator()(
    error_code const& ec, std::size_t bytes_transferred)
{
    switch(step_)
    {
    case 0:
        if(s_.sb_.size() == 0)
        {
            if(s_.capacity_ == 0)
            {
                // read (unbuffered)
                step_ = 1;
                return s_.next_layer_.async_read_some(
                    b_, std::move(*this));
            }

            // read
            step_ = 2;
            return s_.next_layer_.async_read_some(
                s_.sb_.prepare(s_.capacity_),
                    std::move(*this));

        }
        step_ = 3;
        s_.get_io_service().post(
            bind_handler(std::move(*this), ec, 0));
        return;

    case 1:
        // upcall
        break;

    case 2:
        s_.sb_.commit(bytes_transferred);
        BEAST_FALLTHROUGH;

    case 3:
        bytes_transferred =
            boost::asio::buffer_copy(b_, s_.sb_.data());
        s_.sb_.consume(bytes_transferred);
        break;
    }
    h_(ec, bytes_transferred);
}

//------------------------------------------------------------------------------

template<class Stream, class DynamicBuffer>
template<class... Args>
buffered_read_stream<Stream, DynamicBuffer>::
buffered_read_stream(Args&&... args)
    : next_layer_(std::forward<Args>(args)...)
{
}

template<class Stream, class DynamicBuffer>
template<class ConstBufferSequence, class WriteHandler>
auto
buffered_read_stream<Stream, DynamicBuffer>::
async_write_some(ConstBufferSequence const& buffers,
        WriteHandler&& handler) ->
    async_return_type<WriteHandler, void(error_code)>
{
    static_assert(is_async_write_stream<next_layer_type>::value,
        "AsyncWriteStream requirements not met");
    static_assert(is_const_buffer_sequence<
        ConstBufferSequence>::value,
            "ConstBufferSequence requirements not met");
    static_assert(is_completion_handler<WriteHandler,
        void(error_code, std::size_t)>::value,
            "WriteHandler requirements not met");
    return next_layer_.async_write_some(buffers,
        std::forward<WriteHandler>(handler));
}

template<class Stream, class DynamicBuffer>
template<class MutableBufferSequence>
std::size_t
buffered_read_stream<Stream, DynamicBuffer>::
read_some(
    MutableBufferSequence const& buffers)
{
    static_assert(is_sync_read_stream<next_layer_type>::value,
        "SyncReadStream requirements not met");
    static_assert(is_mutable_buffer_sequence<
        MutableBufferSequence>::value,
            "MutableBufferSequence requirements not met");
    error_code ec;
    auto n = read_some(buffers, ec);
    if(ec)
        BOOST_THROW_EXCEPTION(system_error{ec});
    return n;
}

template<class Stream, class DynamicBuffer>
template<class MutableBufferSequence>
std::size_t
buffered_read_stream<Stream, DynamicBuffer>::
read_some(MutableBufferSequence const& buffers,
    error_code& ec)
{
    static_assert(is_sync_read_stream<next_layer_type>::value,
        "SyncReadStream requirements not met");
    static_assert(is_mutable_buffer_sequence<
        MutableBufferSequence>::value,
            "MutableBufferSequence requirements not met");
    using boost::asio::buffer_size;
    using boost::asio::buffer_copy;
    if(sb_.size() == 0)
    {
        if(capacity_ == 0)
            return next_layer_.read_some(buffers, ec);
        sb_.commit(next_layer_.read_some(
            sb_.prepare(capacity_), ec));
        if(ec)
            return 0;
    }
    else
    {
        ec.assign(0, ec.category());
    }
    auto bytes_transferred =
        buffer_copy(buffers, sb_.data());
    sb_.consume(bytes_transferred);
    return bytes_transferred;
}

template<class Stream, class DynamicBuffer>
template<class MutableBufferSequence, class ReadHandler>
auto
buffered_read_stream<Stream, DynamicBuffer>::
async_read_some(MutableBufferSequence const& buffers,
        ReadHandler&& handler) ->
    async_return_type<ReadHandler, void(error_code)>
{
    static_assert(is_async_read_stream<next_layer_type>::value,
        "Stream requirements not met");
    static_assert(is_mutable_buffer_sequence<
        MutableBufferSequence>::value,
            "MutableBufferSequence requirements not met");
    async_completion<ReadHandler,
        void(error_code, std::size_t)> init{handler};
    read_some_op<MutableBufferSequence, handler_type<
        ReadHandler, void(error_code, std::size_t)>>{
            init.completion_handler, *this, buffers}(
                error_code{}, 0);
    return init.result.get();
}

} // beast

#endif
