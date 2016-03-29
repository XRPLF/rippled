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

#ifndef BEAST_ASIO_BUFFERS_READSTREAM_H_INLUDED
#define BEAST_ASIO_BUFFERS_READSTREAM_H_INLUDED

#include <beast/asio/consuming_buffers.h>
#include <boost/asio/buffer.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/system/error_code.hpp>
#include <cstdint>
#include <utility>

namespace beast {
namespace asio {

/** A read/write stream with a ConstBufferSequence on the read side. */
template<class Stream,
    class ConstBufferSequence>
class buffers_readstream
{
    using error_code = boost::system::error_code;

    template<class Buffers, class Handler>
    class read_some_op;

    std::size_t size_;
    Stream next_layer_;
    consuming_buffers<ConstBufferSequence> cb_;

public:
    /// The type of the next layer.
    using next_layer_type =
        std::remove_reference_t<Stream>;

    /// The type of the lowest layer.
    using lowest_layer_type =
        typename next_layer_type::lowest_layer_type;

    template<class... Args>
    explicit
    buffers_readstream(
        ConstBufferSequence const& buffers, Args&&... args);

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

    /** Returns the unused portion of the input sequence. */
    auto const&
    data() const
    {
        return cb_;
    }

    /// Write the given data to the stream. Returns the number of bytes written.
    /// Throws an exception on failure.
    template<class OtherConstBufferSequence>
    std::size_t
    write_some(OtherConstBufferSequence const& buffers)
    {
        return next_layer_.write_some(buffers);
    }

    /// Write the given data to the stream. Returns the number of bytes written,
    /// or 0 if an error occurred.
    template <class OtherConstBufferSequence>
    std::size_t
    write_some(OtherConstBufferSequence const& buffers,
        error_code& ec)
    {
        return next_layer_.write_some(buffers, ec);
    }

    /// Start an asynchronous write. The data being written must be valid for the
    /// lifetime of the asynchronous operation.
    template<class OtherConstBufferSequence, class WriteHandler>
    void
    async_write_some(OtherConstBufferSequence const& buffers,
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

#include <beast/asio/impl/buffers_readstream.ipp>

#endif
