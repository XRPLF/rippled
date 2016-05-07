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

#ifndef BEAST_TEST_FAIL_STREAM_HPP
#define BEAST_TEST_FAIL_STREAM_HPP

#include <beast/core/async_completion.hpp>
#include <beast/core/bind_handler.hpp>
#include <beast/core/error.hpp>
#include <beast/core/detail/get_lowest_layer.hpp>
#include <beast/websocket/teardown.hpp>

namespace beast {
namespace test {

/** A stream wrapper that fails.

    On the Nth operation, the stream will fail with the specified
    error code, or the default error code of invalid_argument.
*/
template<class NextLayer>
class fail_stream
{
    error_code ec_;
    std::size_t n_ = 0;
    NextLayer next_layer_;

    void
    fail()
    {
        if(n_ > 0)
            --n_;
        if(! n_)
            throw system_error{ec_};
    }

    bool
    fail(error_code& ec)
    {
        if(n_ > 0)
            --n_;
        if(! n_)
        {
            ec = ec_;
            return true;
        }
        return false;
    }

public:
    using next_layer_type =
        typename std::remove_reference<NextLayer>::type;

    using lowest_layer_type =
        typename beast::detail::get_lowest_layer<
            next_layer_type>::type;

    fail_stream(fail_stream&&) = default;
    fail_stream& operator=(fail_stream&&) = default;

    template<class... Args>
    explicit
    fail_stream(std::size_t n, Args&&... args)
        : ec_(boost::system::errc::make_error_code(
            boost::system::errc::errc_t::invalid_argument))
        , n_(n)
        , next_layer_(std::forward<Args>(args)...)
    {
    }

    next_layer_type&
    next_layer()
    {
        return next_layer_;
    }

    lowest_layer_type&
    lowest_layer()
    {
        return next_layer_.lowest_layer();
    }

    lowest_layer_type const&
    lowest_layer() const
    {
        return next_layer_.lowest_layer();
    }

    boost::asio::io_service&
    get_io_service()
    {
        return next_layer_.get_io_service();
    }

    template<class MutableBufferSequence>
    std::size_t
    read_some(MutableBufferSequence const& buffers)
    {
        fail();
        return next_layer_.read_some(buffers);
    }

    template<class MutableBufferSequence>
    std::size_t
    read_some(MutableBufferSequence const& buffers, error_code& ec)
    {
        if(fail(ec))
            return 0;
        return next_layer_.read_some(buffers, ec);
    }

    template<class MutableBufferSequence, class ReadHandler>
    typename async_completion<
        ReadHandler, void(error_code)>::result_type
    async_read_some(MutableBufferSequence const& buffers,
        ReadHandler&& handler)
    {
        error_code ec;
        if(fail(ec))
        {
            async_completion<
                ReadHandler, void(error_code, std::size_t)
                    > completion(handler);
            next_layer_.get_io_service().post(
                bind_handler(completion.handler, ec, 0));
            return completion.result.get();
        }
        return next_layer_.async_read_some(buffers,
            std::forward<ReadHandler>(handler));
    }

    template<class ConstBufferSequence>
    std::size_t
    write_some(ConstBufferSequence const& buffers)
    {
        fail();
        return next_layer_.write_some(buffers);
    }

    template<class ConstBufferSequence>
    std::size_t
    write_some(ConstBufferSequence const& buffers, error_code& ec)
    {
        if(fail(ec))
            return 0;
        return next_layer_.write_some(buffers, ec);
    }

    template<class ConstBufferSequence, class WriteHandler>
    typename async_completion<
        WriteHandler, void(error_code)>::result_type
    async_write_some(ConstBufferSequence const& buffers,
        WriteHandler&& handler)
    {
        error_code ec;
        if(fail(ec))
        {
            async_completion<
                WriteHandler, void(error_code, std::size_t)
                    > completion(handler);
            next_layer_.get_io_service().post(
                bind_handler(completion.handler, ec, 0));
            return completion.result.get();
        }
        return next_layer_.async_write_some(buffers,
            std::forward<WriteHandler>(handler));
    }
};

template<class NextLayer>
void
teardown(fail_stream<NextLayer>& stream,
    boost::system::error_code& ec)
{
    websocket_helpers::call_teardown(stream.next_layer(), ec);
}

template<class NextLayer, class TeardownHandler>
void
async_teardown(fail_stream<NextLayer>& stream,
    TeardownHandler&& handler)
{
    websocket_helpers::call_async_teardown(
        stream.next_layer(), std::forward<TeardownHandler>(handler));
}

} // test
} // beast

#endif
