//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_TEST_STRING_STREAM_HPP
#define BEAST_TEST_STRING_STREAM_HPP

#include <beast/bind_handler.hpp>
#include <beast/error.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/io_service.hpp>
#include <string>

namespace beast {
namespace test {

// meets the requirements of AsyncStream, SyncStream
class string_stream
{
    std::string s_;
    boost::asio::io_service& ios_;

public:
    string_stream(boost::asio::io_service& ios,
            std::string s)
        : s_(std::move(s))
        , ios_(ios)
    {
    }

    boost::asio::io_service&
    get_io_service()
    {
        return ios_;
    }

    template<class MutableBufferSequence>
    std::size_t
    read_some(MutableBufferSequence const& buffers)
    {
        error_code ec;
        auto const n = read_some(buffers, ec);
        if(ec)
            throw boost::system::system_error{ec};
        return n;
    }

    template<class MutableBufferSequence>
    std::size_t
    read_some(MutableBufferSequence const& buffers,
        error_code& ec)
    {
        auto const n = boost::asio::buffer_copy(
            buffers, boost::asio::buffer(s_));
        s_.erase(0, n);
        return n;
    }

    template<class MutableBufferSequence, class ReadHandler>
    typename async_completion<ReadHandler,
        void(error_code, std::size_t)>::result_type
    async_read_some(MutableBufferSequence const& buffers,
        ReadHandler&& handler)
    {
        auto const n = boost::asio::buffer_copy(
            buffers, boost::asio::buffer(s_));
        s_.erase(0, n);
        async_completion<ReadHandler,
            void(error_code, std::size_t)> completion(handler);
        ios_.post(bind_handler(
            completion.handler, error_code{}, n));
        return completion.result.get();
    }

    template<class ConstBufferSequence>
    std::size_t
    write_some(ConstBufferSequence const& buffers)
    {
        error_code ec;
        auto const n = write_some(buffers, ec);
        if(ec)
            throw boost::system::system_error{ec};
        return n;
    }

    template<class ConstBufferSequence>
    std::size_t
    write_some(ConstBufferSequence const& buffers,
        error_code&)
    {
        return boost::asio::buffer_size(buffers);
    }

    template<class ConstBuffeSequence, class WriteHandler>
    typename async_completion<WriteHandler,
        void(error_code, std::size_t)>::result_type
    async_write_some(ConstBuffeSequence const& buffers,
        WriteHandler&& handler)
    {
        async_completion<WriteHandler,
            void(error_code, std::size_t)> completion(handler);
        ios_.post(bind_handler(completion.handler,
            error_code{}, boost::asio::buffer_size(buffers)));
        return completion.result.get();
    }
};

} // test
} // beast

#endif
