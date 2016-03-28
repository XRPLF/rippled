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

#ifndef BEAST_ASIO_BUFFERS_READSTREAM_IPP_INLUDED
#define BEAST_ASIO_BUFFERS_READSTREAM_IPP_INLUDED

#include <beast/asio/bind_handler.h>
#include <beast/asio/handler_alloc.h>
#include <beast/asio/type_check.h>
#include <boost/asio/async_result.hpp>

namespace beast {
namespace asio {

template<class Stream, class ConstBufferSequence>
template<class MutableBufferSequence, class Handler>
class buffers_readstream<
    Stream, ConstBufferSequence>::read_some_op
{
    using alloc_type =
        handler_alloc<char, Handler>;

    struct data
    {
        buffers_readstream& brs;
        MutableBufferSequence bs;
        Handler h;
        int state = 0;

        template<class DeducedHandler>
        data(DeducedHandler&& h_,
            buffers_readstream& brs_,
                MutableBufferSequence const& bs_)
            : brs(brs_)
            , bs(bs_)
            , h(std::forward<DeducedHandler>(h_))
        {
        }
    };

    std::shared_ptr<data> d_;

public:
    read_some_op(read_some_op&&) = default;
    read_some_op(read_some_op const&) = default;

    template<class DeducedHandler, class... Args>
    read_some_op(DeducedHandler&& h, Args&&... args)
        : d_(std::allocate_shared<data>(alloc_type{h},
            std::forward<DeducedHandler>(h),
                std::forward<Args>(args)...))
    {
        (*this)(error_code{}, 0);
    }

    void
    operator()(error_code ec,
        std::size_t bytes_transferred);

    friend
    auto asio_handler_allocate(
        std::size_t size, read_some_op* op)
    {
        return boost_asio_handler_alloc_helpers::
            allocate(size, op->d_->h);
    }

    friend
    auto asio_handler_deallocate(
        void* p, std::size_t size, read_some_op* op)
    {
        return boost_asio_handler_alloc_helpers::
            deallocate(p, size, op->d_->h);
    }

    friend
    auto asio_handler_is_continuation(read_some_op* op)
    {
        return boost_asio_handler_cont_helpers::
            is_continuation(op->d_->h);
    }

    template <class Function>
    friend
    auto asio_handler_invoke(Function&& f, read_some_op* op)
    {
        return boost_asio_handler_invoke_helpers::
            invoke(f, op->d_->h);
    }
};

template<class Stream, class ConstBufferSequence>
template<class MutableBufferSequence, class Handler>
void
buffers_readstream<Stream, ConstBufferSequence>::
read_some_op<MutableBufferSequence, Handler>::operator()(
    error_code ec, std::size_t bytes_transferred)
{
    auto& d = *d_;
    while(! ec && d.state != 99)
    {
        switch(d.state)
        {
        case 0:
            if(boost::asio::buffer_size(d.brs.cb_) = 0)
            {
                d.state = 1;
                break;
            }
            d.state = 2;
            d.brs.get_io_service().post(
                beast::asio::bind_handler(
                    std::move(*this), ec, 0));
            return;

        case 1:
            // read
            d.state = 99;
            d.brs.next_layer_.async_read_some(
                d.bs, std::move(*this));
            return;

        // copy
        case 2:
            bytes_transferred =
                buffer_copy(d.bs, d.brs.sb_.data());
            d.brs.size_ -= bytes_transferred;
            d.brs.cb_.consume(bytes_transferred);
            // call handler
            d.state = 99;
            break;
        }
    }
    d.h(ec, bytes_transferred);
}

//------------------------------------------------------------------------------

template<class Stream, class ConstBufferSequence>
template<class... Args>
buffers_readstream<Stream, ConstBufferSequence>::
buffers_readstream(
    ConstBufferSequence const& buffers, Args&&... args)
    : size_(boost::asio::buffer_size(buffers))
    , next_layer_(std::forward<Args>(args)...)
    , cb_(buffers)
{
    static_assert(is_Stream<next_layer_type>::value,
        "Stream requirements not met");
    static_assert(
        is_ConstBufferSequence<ConstBufferSequence>::value,
            "ConstBufferSequence requirements not met");
}

template<class Stream, class ConstBufferSequence>
template<class OtherConstBufferSequence, class WriteHandler>
void
buffers_readstream<Stream, ConstBufferSequence>::
async_write_some(OtherConstBufferSequence const& buffers,
    WriteHandler&& handler)
{
    static_assert(is_ConstBufferSequence<
        ConstBufferSequence>::value,
            "ConstBufferSequence requirements not met");
    static_assert(is_Handler<WriteHandler,
        void(error_code, std::size_t)>::value,
            "WriteHandler requirements not met");
    using namespace boost::asio;
    next_layer_.async_write_some(buffers,
        std::forward<WriteHandler>(handler));
}

template<class Stream, class ConstBufferSequence>
template<class MutableBufferSequence>
std::size_t
buffers_readstream<Stream, ConstBufferSequence>::
read_some(MutableBufferSequence const& buffers)
{
    static_assert(is_MutableBufferSequence<
        MutableBufferSequence>::value,
            "MutableBufferSequence requirements not met");
    error_code ec;
    auto n = read_some(buffers, ec);
    if(ec)
        throw boost::system::system_error{ec};
    return n;
}

template<class Stream, class ConstBufferSequence>
template<class MutableBufferSequence>
std::size_t
buffers_readstream<Stream, ConstBufferSequence>::
read_some(MutableBufferSequence const& buffers,
    error_code& ec)
{
    static_assert(is_MutableBufferSequence<
        MutableBufferSequence>::value,
            "MutableBufferSequence requirements not met");
    if(size_ == 0)
        return next_layer_.read_some(buffers, ec);
    auto const bytes_transferred =
        boost::asio::buffer_copy(buffers, cb_);
    size_ -= bytes_transferred;
    cb_.consume(bytes_transferred);
    return bytes_transferred;
}

template<class Stream, class ConstBufferSequence>
template<class MutableBufferSequence, class ReadHandler>
void
buffers_readstream<Stream, ConstBufferSequence>::
async_read_some(
    MutableBufferSequence const& buffers,
        ReadHandler&& handler)
{
    static_assert(is_MutableBufferSequence<
        MutableBufferSequence>::value,
            "MutableBufferSequence requirements not met");
    static_assert(is_Handler<ReadHandler,
        void(error_code, std::size_t)>::value,
            "ReadHandler requirements not met");
    read_some_op<MutableBufferSequence,
        std::decay_t<ReadHandler>>{
            std::forward<ReadHandler>(handler),
                *this, buffers};
}

} // asio
} // beast

#endif
