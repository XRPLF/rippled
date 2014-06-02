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

#ifndef BEAST_ASIO_HANDSHAKE_PREFILLEDREADSTREAM_H_INCLUDED
#define BEAST_ASIO_HANDSHAKE_PREFILLEDREADSTREAM_H_INCLUDED

#include <beast/cxx14/type_traits.h> // <type_traits>
#include <utility>

namespace beast {
namespace asio {

/** Front-ends a stream with a provided block of data.

    When read operations are performed on this object, bytes will first be
    returned from the buffer provided on construction. When those bytes
    are exhausted, read operations will then pass through to the underlying
    stream.

    Write operations are all simply passed through.
*/
template <class Stream>
class PrefilledReadStream : public Uncopyable
{
protected:
    typedef boost::system::error_code error_code;

    static void throw_if (error_code const& ec)
    {
        throw boost::system::system_error (ec);
    }

public:
    typedef std::remove_reference_t <Stream> next_layer_type;
    typedef typename next_layer_type::lowest_layer_type lowest_layer_type;

    /** Single argument constructor for when we are wrapped in something.
        arg is passed through to the next layer's constructor.
    */
    template <class Arg>
    explicit PrefilledReadStream (Arg& arg)
        : m_next_layer (arg)
    {
    }

    /** Construct with the buffer, and argument passed through.
        This creates a copy of the data. The argument is passed through
        to the constructor of Stream.
    */
    template <class Arg, class ConstBufferSequence>
    PrefilledReadStream (Arg& arg, ConstBufferSequence const& buffers)
        : m_next_layer (arg)
    {
        fill (buffers);
    }

    /** Place some input into the prefilled buffer.
        Note that this is in no way thread safe. The only reason this function
        is here is for the case when you can't pass the buffer through the
        constructor because there is another object wrapping this stream.
    */
    template <class ConstBufferSequence>
    void fill (ConstBufferSequence const& buffers)
    {
        // We don't assume the caller's buffers will
        // remain valid for the lifetime of this object.
        //
        m_buffer.commit (boost::asio::buffer_copy (
            m_buffer.prepare (boost::asio::buffer_size (buffers)),
                buffers));
    }

    next_layer_type& next_layer()
    {
        return m_next_layer;
    }

    next_layer_type const& next_layer() const
    {
        return m_next_layer;
    }

    lowest_layer_type& lowest_layer()
    {
        return m_next_layer.lowest_layer();
    }

    const lowest_layer_type& lowest_layer() const
    {
        return m_next_layer.lowest_layer();
    }

    boost::asio::io_service& get_io_service()
    {
        return m_next_layer.get_io_service();
    }

    void close()
    {
        error_code ec;
        close (ec);
        throw_if (ec);
    }

    error_code close (error_code& ec)
    {
        // VFALCO NOTE This is questionable. We can't
        // call m_next_layer.close() because Stream might not
        // support that function. For example, ssl::stream has no close()
        //
        return lowest_layer ().close(ec);
    }

    template <class MutableBufferSequence>
    std::size_t read_some (MutableBufferSequence const& buffers)
    {
        error_code ec;
        std::size_t const amount = read_some (buffers, ec);
        throw_if (ec);
        return amount;
    }

    template <class MutableBufferSequence>
    std::size_t read_some (MutableBufferSequence const& buffers,
        error_code& ec)
    {
        if (m_buffer.size () > 0)
        {
            ec = error_code ();
            std::size_t const bytes_transferred = boost::asio::buffer_copy (
                buffers, m_buffer.data ());
            m_buffer.consume (bytes_transferred);
            return bytes_transferred;
        }
        return m_next_layer.read_some (buffers, ec);
    }

    template <class ConstBufferSequence>
    std::size_t write_some (ConstBufferSequence const& buffers)
    {
        error_code ec;
        auto const amount (write_some (buffers, ec));
        throw_if (ec);
        return amount;
    }

    template <class ConstBufferSequence>
    std::size_t write_some (ConstBufferSequence const& buffers,
        error_code& ec)
    {
        return m_next_layer.write_some (buffers, ec);
    }

    template <class MutableBufferSequence, class ReadHandler>
    void async_read_some (MutableBufferSequence const& buffers,
        ReadHandler&& handler)
    {
        if (m_buffer.size () > 0)
        {
            auto const bytes_transferred (boost::asio::buffer_copy (
                buffers, m_buffer.data ()));
            m_buffer.consume (bytes_transferred);
            get_io_service ().post (bind_handler (
                std::forward <ReadHandler> (handler),
                    error_code (), bytes_transferred));
            return;
        }
        m_next_layer.async_read_some (buffers,
            std::forward <ReadHandler> (handler));
    }

    template <class ConstBufferSequence, class WriteHandler>
    void async_write_some (ConstBufferSequence const& buffers,
        WriteHandler&& handler)
    {
        m_next_layer.async_write_some (buffers,
            std::forward <WriteHandler> (handler));
    }

private:
    Stream m_next_layer;
    boost::asio::streambuf m_buffer;
};

}
}

#endif
