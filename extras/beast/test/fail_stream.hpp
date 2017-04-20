//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_TEST_FAIL_STREAM_HPP
#define BEAST_TEST_FAIL_STREAM_HPP

#include <beast/core/async_completion.hpp>
#include <beast/core/bind_handler.hpp>
#include <beast/core/error.hpp>
#include <beast/core/detail/get_lowest_layer.hpp>
#include <beast/websocket/teardown.hpp>
#include <beast/test/fail_counter.hpp>
#include <boost/optional.hpp>

namespace beast {
namespace test {

/** A stream wrapper that fails.

    On the Nth operation, the stream will fail with the specified
    error code, or the default error code of invalid_argument.
*/
template<class NextLayer>
class fail_stream
{
    boost::optional<fail_counter> fc_;
    fail_counter* pfc_;
    NextLayer next_layer_;

public:
    using next_layer_type =
        typename std::remove_reference<NextLayer>::type;

    using lowest_layer_type =
        typename beast::detail::get_lowest_layer<
            next_layer_type>::type;

    fail_stream(fail_stream&&) = delete;
    fail_stream(fail_stream const&) = delete;
    fail_stream& operator=(fail_stream&&) = delete;
    fail_stream& operator=(fail_stream const&) = delete;

    template<class... Args>
    explicit
    fail_stream(std::size_t n, Args&&... args)
        : fc_(n)
        , pfc_(&*fc_)
        , next_layer_(std::forward<Args>(args)...)
    {
    }

    template<class... Args>
    explicit
    fail_stream(fail_counter& fc, Args&&... args)
        : pfc_(&fc)
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
        pfc_->fail();
        return next_layer_.read_some(buffers);
    }

    template<class MutableBufferSequence>
    std::size_t
    read_some(MutableBufferSequence const& buffers, error_code& ec)
    {
        if(pfc_->fail(ec))
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
        if(pfc_->fail(ec))
        {
            async_completion<
                ReadHandler, void(error_code, std::size_t)
                    > completion{handler};
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
        pfc_->fail();
        return next_layer_.write_some(buffers);
    }

    template<class ConstBufferSequence>
    std::size_t
    write_some(ConstBufferSequence const& buffers, error_code& ec)
    {
        if(pfc_->fail(ec))
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
        if(pfc_->fail(ec))
        {
            async_completion<
                WriteHandler, void(error_code, std::size_t)
                    > completion{handler};
            next_layer_.get_io_service().post(
                bind_handler(completion.handler, ec, 0));
            return completion.result.get();
        }
        return next_layer_.async_write_some(buffers,
            std::forward<WriteHandler>(handler));
    }

    friend
    void
    teardown(websocket::teardown_tag,
        fail_stream<NextLayer>& stream,
            boost::system::error_code& ec)
    {
        if(stream.pfc_->fail(ec))
            return;
        beast::websocket_helpers::call_teardown(stream.next_layer(), ec);
    }

    template<class TeardownHandler>
    friend
    void
    async_teardown(websocket::teardown_tag,
        fail_stream<NextLayer>& stream,
            TeardownHandler&& handler)
    {
        error_code ec;
        if(stream.pfc_->fail(ec))
        {
            stream.get_io_service().post(
                bind_handler(std::move(handler), ec));
            return;
        }
        beast::websocket_helpers::call_async_teardown(
            stream.next_layer(), std::forward<TeardownHandler>(handler));
    }
};

} // test
} // beast

#endif
