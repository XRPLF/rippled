//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_IMPL_DYNABUF_READSTREAM_HPP
#define BEAST_IMPL_DYNABUF_READSTREAM_HPP

#include <beast/core/bind_handler.hpp>
#include <beast/core/error.hpp>
#include <beast/core/handler_concepts.hpp>
#include <beast/core/handler_helpers.hpp>
#include <beast/core/handler_ptr.hpp>

namespace beast {

template<class Stream, class DynamicBuffer>
template<class MutableBufferSequence, class Handler>
class dynabuf_readstream<
    Stream, DynamicBuffer>::read_some_op
{
    // VFALCO What about bool cont for is_continuation?
    struct data
    {
        dynabuf_readstream& srs;
        MutableBufferSequence bs;
        int state = 0;

        data(Handler&, dynabuf_readstream& srs_,
                MutableBufferSequence const& bs_)
            : srs(srs_)
            , bs(bs_)
        {
        }
    };

    handler_ptr<data, Handler> d_;

public:
    read_some_op(read_some_op&&) = default;
    read_some_op(read_some_op const&) = default;

    template<class DeducedHandler, class... Args>
    read_some_op(DeducedHandler&& h,
            dynabuf_readstream& srs, Args&&... args)
        : d_(std::forward<DeducedHandler>(h),
            srs, std::forward<Args>(args)...)
    {
        (*this)(error_code{}, 0);
    }

    void
    operator()(error_code const& ec,
        std::size_t bytes_transferred);

    friend
    void* asio_handler_allocate(
        std::size_t size, read_some_op* op)
    {
        return beast_asio_helpers::
            allocate(size, op->d_.handler());
    }

    friend
    void asio_handler_deallocate(
        void* p, std::size_t size, read_some_op* op)
    {
        return beast_asio_helpers::
            deallocate(p, size, op->d_.handler());
    }

    friend
    bool asio_handler_is_continuation(read_some_op* op)
    {
        return beast_asio_helpers::
            is_continuation(op->d_.handler());
    }

    template<class Function>
    friend
    void asio_handler_invoke(Function&& f, read_some_op* op)
    {
        return beast_asio_helpers::
            invoke(f, op->d_.handler());
    }
};

template<class Stream, class DynamicBuffer>
template<class MutableBufferSequence, class Handler>
void
dynabuf_readstream<Stream, DynamicBuffer>::
read_some_op<MutableBufferSequence, Handler>::operator()(
    error_code const& ec, std::size_t bytes_transferred)
{
    auto& d = *d_;
    while(! ec && d.state != 99)
    {
        switch(d.state)
        {
        case 0:
            if(d.srs.sb_.size() == 0)
            {
                d.state =
                    d.srs.capacity_ > 0 ? 2 : 1;
                break;
            }
            d.state = 4;
            d.srs.get_io_service().post(
                bind_handler(std::move(*this), ec, 0));
            return;

        case 1:
            // read (unbuffered)
            d.state = 99;
            d.srs.next_layer_.async_read_some(
                d.bs, std::move(*this));
            return;

        case 2:
            // read
            d.state = 3;
            d.srs.next_layer_.async_read_some(
                d.srs.sb_.prepare(d.srs.capacity_),
                    std::move(*this));
            return;

        // got data
        case 3:
            d.state = 4;
            d.srs.sb_.commit(bytes_transferred);
            break;

        // copy
        case 4:
            bytes_transferred =
                boost::asio::buffer_copy(
                    d.bs, d.srs.sb_.data());
            d.srs.sb_.consume(bytes_transferred);
            // call handler
            d.state = 99;
            break;
        }
    }
    d_.invoke(ec, bytes_transferred);
}

//------------------------------------------------------------------------------

template<class Stream, class DynamicBuffer>
template<class... Args>
dynabuf_readstream<Stream, DynamicBuffer>::
dynabuf_readstream(Args&&... args)
    : next_layer_(std::forward<Args>(args)...)
{
}

template<class Stream, class DynamicBuffer>
template<class ConstBufferSequence, class WriteHandler>
auto
dynabuf_readstream<Stream, DynamicBuffer>::
async_write_some(ConstBufferSequence const& buffers,
    WriteHandler&& handler) ->
        typename async_completion<
            WriteHandler, void(error_code)>::result_type
{
    static_assert(is_AsyncWriteStream<next_layer_type>::value,
        "AsyncWriteStream requirements not met");
    static_assert(is_ConstBufferSequence<
        ConstBufferSequence>::value,
            "ConstBufferSequence requirements not met");
    static_assert(is_CompletionHandler<WriteHandler,
        void(error_code, std::size_t)>::value,
            "WriteHandler requirements not met");
    return next_layer_.async_write_some(buffers,
        std::forward<WriteHandler>(handler));
}

template<class Stream, class DynamicBuffer>
template<class MutableBufferSequence>
std::size_t
dynabuf_readstream<Stream, DynamicBuffer>::
read_some(
    MutableBufferSequence const& buffers)
{
    static_assert(is_SyncReadStream<next_layer_type>::value,
        "SyncReadStream requirements not met");
    static_assert(is_MutableBufferSequence<
        MutableBufferSequence>::value,
            "MutableBufferSequence requirements not met");
    error_code ec;
    auto n = read_some(buffers, ec);
    if(ec)
        throw system_error{ec};
    return n;
}

template<class Stream, class DynamicBuffer>
template<class MutableBufferSequence>
std::size_t
dynabuf_readstream<Stream, DynamicBuffer>::
read_some(MutableBufferSequence const& buffers,
    error_code& ec)
{
    static_assert(is_SyncReadStream<next_layer_type>::value,
        "SyncReadStream requirements not met");
    static_assert(is_MutableBufferSequence<
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
    auto bytes_transferred =
        buffer_copy(buffers, sb_.data());
    sb_.consume(bytes_transferred);
    return bytes_transferred;
}

template<class Stream, class DynamicBuffer>
template<class MutableBufferSequence, class ReadHandler>
auto
dynabuf_readstream<Stream, DynamicBuffer>::
async_read_some(
    MutableBufferSequence const& buffers,
        ReadHandler&& handler) ->
            typename async_completion<
                ReadHandler, void(error_code)>::result_type
{
    static_assert(is_AsyncReadStream<next_layer_type>::value,
        "Stream requirements not met");
    static_assert(is_MutableBufferSequence<
        MutableBufferSequence>::value,
            "MutableBufferSequence requirements not met");
    beast::async_completion<
        ReadHandler, void(error_code, std::size_t)
            > completion(handler);
    read_some_op<MutableBufferSequence,
        decltype(completion.handler)>{
            completion.handler, *this, buffers};
    return completion.result.get();
}

} // beast

#endif
