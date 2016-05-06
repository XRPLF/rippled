//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_STREAMBUF_READSTREAM_HPP
#define BEAST_STREAMBUF_READSTREAM_HPP

#include <beast/async_completion.hpp>
#include <beast/buffer_concepts.hpp>
#include <beast/stream_concepts.hpp>
#include <beast/streambuf.hpp>
#include <beast/detail/get_lowest_layer.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/system/error_code.hpp>
#include <cstdint>
#include <utility>

namespace beast {

/** A @b `Stream` with attached @b `Streambuf` to buffer reads.

    This wraps a @b `Stream` implementation so that calls to write are
    passed through to the underlying stream, while calls to read will
    first consume the input sequence stored in a @b `Streambuf` which
    is part of the object.

    The use-case for this class is different than that of the
    `boost::asio::buffered_readstream`. It is designed to facilitate
    the use of `boost::asio::read_until`, and to allow buffers
    acquired during detection of handshakes to be made transparently
    available to callers. A hypothetical implementation of the
    buffered version of `boost::asio::ssl::stream::async_handshake`
    could make use of this wrapper.

    Uses:

    @li Transparently leave untouched input acquired in calls
      to `boost::asio::read_until` behind for subsequent callers.

    @li "Preload" a stream with handshake input data acquired
      from other sources.

    Example:
    @code
    // Process the next HTTP headers on the stream,
    // leaving excess bytes behind for the next call.
    //
    template<class Streambuf>
    void process_http_message(
        streambuf_readstream<Streambuf>& stream)
    {
        // Read up to and including the end of the HTTP
        // headers, leaving the sequence in the stream's
        // buffer. read_until may read past the end of the
        // headers; the return value will include only the
        // part up to the end of the delimiter.
        //
        std::size_t bytes_transferred =
            boost::asio::read_until(
                stream.next_layer(), stream.buffer(), "\r\n\r\n");

        // Use prepare_buffers() to limit the input
        // sequence to only the data up to and including
        // the trailing "\r\n\r\n".
        //
        auto header_buffers = prepare_buffers(
            bytes_transferred, stream.buffer().data());

        ...

        // Discard the portion of the input corresponding
        // to the HTTP headers.
        //
        stream.buffer().consume(bytes_transferred);

        // Everything we read from the stream
        // is part of the content-body.
    }
    @endcode

    @tparam Stream The type of stream to wrap.

    @tparam Streambuf The type of stream buffer to use.
*/
template<class Stream, class Streambuf>
class streambuf_readstream
{
    static_assert(is_Streambuf<Streambuf>::value,
        "Streambuf requirements not met");

    using error_code = boost::system::error_code;

    template<class Buffers, class Handler>
    class read_some_op;

    Streambuf sb_;
    std::size_t size_ = 0;
    Stream next_layer_;

public:
    /// The type of the internal buffer
    using streambuf_type = Streambuf;

    /// The type of the next layer.
    using next_layer_type =
        typename std::remove_reference<Stream>::type;

    /// The type of the lowest layer.
    using lowest_layer_type =
#if GENERATING_DOCS
        implementation_defined;
#else
        typename detail::get_lowest_layer<
            next_layer_type>::type;
#endif

    /** Move constructor.

        @note The behavior of move assignment on or from streams
        with active or pending operations is undefined.
    */
    streambuf_readstream(streambuf_readstream&&) = default;

    /** Move assignment.

        @note The behavior of move assignment on or from streams
        with active or pending operations is undefined.
    */
    streambuf_readstream& operator=(streambuf_readstream&&) = default;

    /** Construct the wrapping stream.

        @param args Parameters forwarded to the `Stream` constructor.
    */
    template<class... Args>
    explicit
    streambuf_readstream(Args&&... args);

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

    /** Access the internal buffer.

        The internal buffer is returned. It is possible for the
        caller to break invariants with this function. For example,
        by causing the internal buffer size to increase beyond
        the caller defined maximum.
    */
    Streambuf&
    buffer()
    {
        return sb_;
    }

    /** Access the internal buffer.

        The internal buffer is returned. It is possible for the
        caller to break invariants with this function. For example,
        by causing the internal buffer size to increase beyond
        the caller defined maximum.
    */
    Streambuf const&
    buffer() const
    {
        return sb_;
    }

    /** Set the maximum buffer size.

        This changes the maximum size of the internal buffer used
        to hold read data. No bytes are discarded by this call. If
        the buffer size is set to zero, no more data will be buffered.

        Thread safety:
            The caller is responsible for making sure the call is
            made from the same implicit or explicit strand.

        @param size The number of bytes in the read buffer.

        @note This is a soft limit. If the new maximum size is smaller
        than the amount of data in the buffer, no bytes are discarded.
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
        static_assert(is_SyncWriteStream<next_layer_type>::value,
            "SyncWriteStream requirements not met");
        return next_layer_.write_some(buffers);
    }

    /// Write the given data to the stream. Returns the number of bytes written,
    /// or 0 if an error occurred.
    template <class ConstBufferSequence>
    std::size_t
    write_some(ConstBufferSequence const& buffers,
        error_code& ec)
    {
        static_assert(is_SyncWriteStream<next_layer_type>::value,
            "SyncWriteStream requirements not met");
        return next_layer_.write_some(buffers, ec);
    }

    /// Start an asynchronous write. The data being written must be valid for the
    /// lifetime of the asynchronous operation.
    template<class ConstBufferSequence, class WriteHandler>
#if GENERATING_DOCS
    void_or_deduced
#else
    typename async_completion<WriteHandler, void(error_code)>::result_type
#endif
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
#if GENERATING_DOCS
    void_or_deduced
#else
    typename async_completion<ReadHandler, void(error_code)>::result_type
#endif
    async_read_some(MutableBufferSequence const& buffers,
        ReadHandler&& handler);
};

} // beast

#include <beast/impl/streambuf_readstream.ipp>

#endif
