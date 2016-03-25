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

#ifndef BEAST_ASIO_BUFFERED_READSTREAM_H_INLUDED
#define BEAST_ASIO_BUFFERED_READSTREAM_H_INLUDED

#include <beast/asio/consuming_buffers.h>
#include <boost/asio/async_result.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/system/error_code.hpp>
#include <cstdint>
#include <utility>

namespace beast {
namespace asio {

/// A read/write stream with a buffer in between the read side.
template<class ConstBufferSequence, class Stream>
class buffered_readstream
{
    Stream next_layer_;
    consuming_buffers<
        boost::asio::const_buffer,
            ConstBufferSequence> bs_;

public:
    /// The type of the next layer.
    using next_layer_type =
        std::remove_reference_t<Stream>;

    /// The type of the lowest layer.
    using lowest_layer_type =
        typename next_layer_type::lowest_layer_type;

    template<class DeducedBuffers, class... Args>
    explicit
    buffered_readstream(DeducedBuffers&&bs, Args&&... args);

    /// Get a reference to the next layer.
    next_layer_type&
    next_layer()
    {
        return next_layer_;
    }

    /// Get a reference to the lowest layer.
    lowest_layer_type&
    lowest_layer()
    {
        return next_layer_.lowest_layer();
    }

    /// Get a const reference to the lowest layer.
    lowest_layer_type const&
    lowest_layer() const
    {
        return next_layer_.lowest_layer();
    }

    /// Get the io_service associated with the object.
    boost::asio::io_service&
    get_io_service()
    {
        return next_layer_.get_io_service();
    }

    /// Write the given data to the stream. Returns the number of bytes written.
    /// Throws an exception on failure.
    template<class ConstBuffers>
    std::size_t
    write_some(ConstBuffers&& buffers)
    {
        return next_layer_.write_some(buffers);
    }

    /// Write the given data to the stream. Returns the number of bytes written,
    /// or 0 if an error occurred.
    template <class ConstBufferSequence>
    std::size_t
    write_some(const ConstBufferSequence& buffers,
    boost::system::error_code& ec)
    {
        return next_layer_.write_some(buffers, ec);
    }

    /// Start an asynchronous write. The data being written must be valid for the
    /// lifetime of the asynchronous operation.
    template<class ConstBuffers, class WriteHandler>
    BOOST_ASIO_INITFN_RESULT_TYPE(WriteHandler,
        void (boost::system::error_code, std::size_t))
    async_write_some(ConstBuffers&& buffers,
        WriteHandler&& handler)
    {
        using namespace boost::asio;
        detail::async_result_init<WriteHandler,
            void(boost::system::error_code, std::size_t)> init(
                std::forward<WriteHandler>(handler));
        next_layer_.async_write_some(std::forward<ConstBuffers>(buffers),
            std::forward<BOOST_ASIO_HANDLER_TYPE(WriteHandler,
                void(boost::system::error_code, std::size_t))>(init.handler));
        return init.result.get();
    }

    /// Read some data from the stream. Returns the number of bytes read.
    /// Throws an exception on failure.
    template<class MutableBuffers>
    std::size_t
    read_some(MutableBuffers const& buffers);

    /// Read some data from the stream. Returns the number of bytes read
    /// or 0 if an error occurred.
    template<class MutableBuffers>
    std::size_t
    read_some(MutableBuffers const& buffers,
        boost::system::error_code& ec);

    /// Start an asynchronous read. The buffer into which the data will be read
    /// must be valid for the lifetime of the asynchronous operation.
    template<class MutableBuffers, class ReadHandler>
    BOOST_ASIO_INITFN_RESULT_TYPE(ReadHandler,
        void(boost::system::error_code, std::size_t))
    async_read_some(MutableBuffers&& buffers,
        ReadHandler&& handler);
};

} // asio
} // beast

#include <beast/asio/impl/buffered_readstream.ipp>

#endif
