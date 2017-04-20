//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_TEST_STRING_OSTREAM_HPP
#define BEAST_TEST_STRING_OSTREAM_HPP

#include <beast/core/async_completion.hpp>
#include <beast/core/bind_handler.hpp>
#include <beast/core/error.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/io_service.hpp>
#include <string>

namespace beast {
namespace test {

class string_ostream
{
    boost::asio::io_service& ios_;

public:
    std::string str;

    explicit
    string_ostream(boost::asio::io_service& ios)
        : ios_(ios)
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
            throw system_error{ec};
        return n;
    }

    template<class MutableBufferSequence>
    std::size_t
    read_some(MutableBufferSequence const& buffers,
        error_code& ec)
    {
        return 0;
    }

    template<class MutableBufferSequence, class ReadHandler>
    typename async_completion<ReadHandler,
        void(error_code, std::size_t)>::result_type
    async_read_some(MutableBufferSequence const& buffers,
        ReadHandler&& handler)
    {
        async_completion<ReadHandler,
            void(error_code, std::size_t)> completion{handler};
        ios_.post(bind_handler(completion.handler,
            error_code{}, 0));
        return completion.result.get();
    }

    template<class ConstBufferSequence>
    std::size_t
    write_some(ConstBufferSequence const& buffers)
    {
        error_code ec;
        auto const n = write_some(buffers, ec);
        if(ec)
            throw system_error{ec};
        return n;
    }

    template<class ConstBufferSequence>
    std::size_t
    write_some(
        ConstBufferSequence const& buffers, error_code&)
    {
        auto const n = buffer_size(buffers);
        using boost::asio::buffer_size;
        using boost::asio::buffer_cast;
        str.reserve(str.size() + n);
        for(auto const& buffer : buffers)
            str.append(buffer_cast<char const*>(buffer),
                buffer_size(buffer));
        return n;
    }

    template<class ConstBufferSequence, class WriteHandler>
    typename async_completion<
        WriteHandler, void(error_code)>::result_type
    async_write_some(ConstBufferSequence const& buffers,
        WriteHandler&& handler)
    {
        error_code ec;
        auto const bytes_transferred = write_some(buffers, ec);
        async_completion<
            WriteHandler, void(error_code, std::size_t)
                > completion{handler};
        get_io_service().post(
            bind_handler(completion.handler, ec, bytes_transferred));
        return completion.result.get();
    }
};

} // test
} // beast

#endif
