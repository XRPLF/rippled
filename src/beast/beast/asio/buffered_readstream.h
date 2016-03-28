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

#include <beast/asio/streambuf.h>
#include <boost/asio/buffer.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/system/error_code.hpp>
#include <cstdint>
#include <utility>

namespace beast {
namespace asio {

/** A read/write stream with a buffer in between the read side. */
template<class Stream,
    class Streambuf = beast::asio::streambuf>
class buffered_readstream
{
    using error_code = boost::system::error_code;

    template<class Buffers, class Handler>
    class read_some_op;

    Streambuf sb_;
    std::size_t size_ = 0;
    Stream next_layer_;

public:
    /// The type of the next layer.
    using next_layer_type =
        std::remove_reference_t<Stream>;

    /// The type of the lowest layer.
    using lowest_layer_type =
        typename next_layer_type::lowest_layer_type;

    template<class... Args>
    explicit
    buffered_readstream(Args&&... args);

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

    /** Set the buffer size.

        This changes the maximum size of the internal buffer used
        to hold read data. No bytes are discarded by this call. If
        the buffer size is set to zero, no more data will be buffered.

        Thread safety:
            The caller is responsible for making sure the call is
            made from the same implicit or explicit strand.

        @param size The number of bytes in the read buffer.
    */
    void
    reserve(std::size_t size)
    {
        size_ = size;
    }

    /// Write the given data to the stream. Returns the number of bytes written.
    /// Throws an exception on failure.
    template<class ConstBufferSequence>
    std::size_t
    write_some(ConstBufferSequence const& buffers)
    {
        return next_layer_.write_some(buffers);
    }

    /// Write the given data to the stream. Returns the number of bytes written,
    /// or 0 if an error occurred.
    template <class ConstBufferSequence>
    std::size_t
    write_some(ConstBufferSequence const& buffers,
        error_code& ec)
    {
        return next_layer_.write_some(buffers, ec);
    }

    /// Start an asynchronous write. The data being written must be valid for the
    /// lifetime of the asynchronous operation.
    template<class ConstBufferSequence, class WriteHandler>
    void
    async_write_some(ConstBufferSequence const& buffers,
        WriteHandler&& handler);

    /// Read some data from the stream. Returns the number of bytes read.
    /// Throws an exception on failure.
    template<class MutableBufferSequence>
    std::size_t
    read_some(MutableBufferSequence const& buffers);

    /// Read some data from the stream. Returns the number of bytes read
    /// or 0 if an error occurred.
    template<class MutableBufferSequence>
    std::size_t
    read_some(MutableBufferSequence const& buffers,
        error_code& ec);

    /// Start an asynchronous read. The buffer into which the data will be read
    /// must be valid for the lifetime of the asynchronous operation.
    template<class MutableBufferSequence, class ReadHandler>
    void
    async_read_some(MutableBufferSequence const& buffers,
        ReadHandler&& handler);
};

} // asio
} // beast

#include <beast/asio/impl/buffered_readstream.ipp>

#endif
