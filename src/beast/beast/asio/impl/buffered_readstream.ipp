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

#ifndef BEAST_ASIO_BUFFERED_READSTREAM_IPP_INLUDED
#define BEAST_ASIO_BUFFERED_READSTREAM_IPP_INLUDED

#include <beast/asio/consuming_buffers.h>

namespace beast {
namespace asio {

namespace detail {

template<class StreamBuffers, class Stream,
    class Buffers, class Handler>
class buffered_read_op
{
    Stream& s_;
    consuming_buffers<
        boost::asio::const_buffer,
            StreamBuffers>& cb_;
    Handler h_;
    consuming_buffers<
        boost::asio::const_buffer,
            Buffers>& bs_;

public:
    buffered_read_op(buffered_read_op&&) = default;
    buffered_read_op(buffered_read_op const&) = default;
    buffered_read_op& operator=(buffered_read_op&&) = delete;
    buffered_read_op& operator=(buffered_read_op const&) = delete;

    template<class DeducedBuffers, class DeducedHandler>
    buffered_read_op(Stream& s, consuming_buffers<
        boost::asio::const_buffer, StreamBuffers>& cb,
            DeducedBuffers&& bs, DeducedHandler&& h)
        : s_(s)
        , cb_(cb)
        , h_(std::forward<DeducedHandler>(h))
        , bs_(std::forward<DeducedBuffers(bs))
    {
    }

    friend
    auto asio_handler_allocate(
        std::size_t size, buffered_read_op* op)
    {
        return boost_asio_handler_alloc_helpers::
            allocate(size, op->h_);
    }

    friend
    auto asio_handler_deallocate(
        void* p, std::size_t size, buffered_read_op* op)
    {
        return boost_asio_handler_alloc_helpers::
            deallocate(p, size, op->h_);
    }

    friend
    auto asio_handler_is_continuation(buffered_read_op* op)
    {
        return op->state_ >= 1 ||
            boost_asio_handler_cont_helpers::
                is_continuation(op->h_);
    }

    template <class Function>
    friend
    auto asio_handler_invoke(Function&& f, buffered_read_op* op)
    {
        return boost_asio_handler_invoke_helpers::
            invoke(f, op->h_);
    }
};

} // detail

template<class Buffers, class Stream>
template<class DeducedBuffers, class... Args>
buffered_readstream<Buffers, Stream>::buffered_readstream(
        DeducedBuffers&&bs, Args&&... args)
    : next_layer_(std::forward<Args>(args)...)
    , bs_(std::forward<DeducedBuffers>(bs))
{
}

template<class Buffers, class Stream>
template<class MutableBuffers>
std::size_t
buffered_readstream<Buffers, Stream>::read_some(
    MutableBuffers const& buffers)
{
    error_code ec;
    auto n = read_some(buffers, ec);
    if(ec)
        throw boost::system::system_error{ec};
    return n;
}

template<class Buffers, class Stream>
template<class MutableBuffers>
std::size_t
buffered_readstream<Buffers, Stream>::read_some(
    MutableBuffers const& buffers, boost::system::error_code& ec)
{
    using namespace boost::asio;
    auto n = buffer_copy(buffers, bs_);
    bs_.consume(n);
    auto cb = consumed_buffers<
        MutableBuffers const&>(n, buffers);
    return n + next_layer_.read_some(cb, ec);
}

template<class Buffers, class Stream>
template<class MutableBuffers, class ReadHandler>
BOOST_ASIO_INITFN_RESULT_TYPE(ReadHandler,
    void(boost::system::error_code, std::size_t))
buffered_readstream<Buffers, Stream>::async_read_some(
    MutableBuffers&& buffers, ReadHandler&& handler)
{
    using namespace boost::asio;
    detail::async_result_init<ReadHandler,
        void(boost::system::error_code, std::size_t)> init(
            std::forward<ReadHandler>(handler));
    detail::buffered_read_op<Buffers, Stream,
        std::decay_t<MutableBuffers>, BOOST_ASIO_HANDLER_TYPE(
            ReadHandler, void(boost::system::error_code, std::size_t))>{
                next_layer_, bs_, std::forward<MutableBuffers>(buffers),
                    init.handler};
    return init.result.get();
}

} // asio
} // beast

#endif
