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

#ifndef BEAST_ASIO_STREAMBUF_READSTREAM_IPP_INLUDED
#define BEAST_ASIO_STREAMBUF_READSTREAM_IPP_INLUDED

#include <beast/asio/bind_handler.h>
#include <beast/asio/handler_alloc.h>
#include <beast/asio/type_check.h>
#include <boost/asio/async_result.hpp>

namespace beast {
namespace asio {

template<class Stream, class Streambuf>
template<class MutableBufferSequence, class Handler>
class streambuf_readstream<
    Stream, Streambuf>::read_some_op
{
    using alloc_type =
        handler_alloc<char, Handler>;

    struct data
    {
        streambuf_readstream& brs;
        MutableBufferSequence bs;
        Handler h;
        int state = 0;

        template<class DeducedHandler>
        data(DeducedHandler&& h_,
            streambuf_readstream& brs_,
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

template<class Stream, class Streambuf>
template<class MutableBufferSequence, class Handler>
void
streambuf_readstream<Stream, Streambuf>::
read_some_op<MutableBufferSequence, Handler>::operator()(
    error_code ec, std::size_t bytes_transferred)
{
    auto& d = *d_;
    while(! ec && d.state != 99)
    {
        switch(d.state)
        {
        case 0:
            if(d.brs.sb_.size() == 0)
            {
                d.state =
                    d.brs.size_ > 0 ? 2 : 1;
                break;
            }
            d.state = 4;
            d.brs.get_io_service().post(
                beast::asio::bind_handler(
                    std::move(*this), ec, 0));
            return;

        case 1:
            // read (unbuffered)
            d.state = 99;
            d.brs.next_layer_.async_read_some(
                d.bs, std::move(*this));
            return;

        case 2:
            // read
            d.state = 3;
            d.brs.next_layer_.async_read_some(
                d.brs.sb_.prepare(d.brs.size_),
                    std::move(*this));
            return;

        // got data
        case 3:
            d.state = 4;
            d.brs.sb_.commit(bytes_transferred);
            break;

        // copy
        case 4:
            bytes_transferred =
                boost::asio::buffer_copy(
                    d.bs, d.brs.sb_.data());
            d.brs.sb_.consume(bytes_transferred);
            // call handler
            d.state = 99;
            break;
        }
    }
    d.h(ec, bytes_transferred);
}

//------------------------------------------------------------------------------

template<class Stream, class Streambuf>
template<class... Args>
streambuf_readstream<Stream, Streambuf>::
streambuf_readstream(Args&&... args)
    : next_layer_(std::forward<Args>(args)...)
{
    static_assert(is_Stream<next_layer_type>::value,
        "Stream requirements not met");
    static_assert(is_Streambuf<Streambuf>::value,
        "Streambuf requirements not met");
}

template<class Stream, class Streambuf>
template<class ConstBufferSequence, class WriteHandler>
void
streambuf_readstream<Stream, Streambuf>::
async_write_some(ConstBufferSequence const& buffers,
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

template<class Stream, class Streambuf>
template<class MutableBufferSequence>
std::size_t
streambuf_readstream<Stream, Streambuf>::
read_some(
    MutableBufferSequence const& buffers)
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

template<class Stream, class Streambuf>
template<class MutableBufferSequence>
std::size_t
streambuf_readstream<Stream, Streambuf>::
read_some(MutableBufferSequence const& buffers,
    error_code& ec)
{
    static_assert(is_MutableBufferSequence<
        MutableBufferSequence>::value,
            "MutableBufferSequence requirements not met");
    using namespace boost::asio;
    if(buffer_size(buffers) == 0)
        return 0;
    if(size_ == 0)
        return next_layer_.read_some(buffers, ec);
    if(sb_.size() == 0)
    {
        sb_.commit(next_layer_.read_some(
            sb_.prepare(size_), ec));
        if(ec)
            return 0;
    }
    auto bytes_transferred =
        buffer_copy(buffers, sb_.data());
    sb_.consume(bytes_transferred);
    return bytes_transferred;
}

template<class Stream, class Streambuf>
template<class MutableBufferSequence, class ReadHandler>
void
streambuf_readstream<Stream, Streambuf>::
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
