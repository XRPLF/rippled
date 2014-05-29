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

#ifndef BEAST_ASIO_HANDSHAKE_HANDSHAKEDETECTOR_H_INCLUDED
#define BEAST_ASIO_HANDSHAKE_HANDSHAKEDETECTOR_H_INCLUDED

#include <beast/boost/get_pointer.h>
#include <beast/asio/bind_handler.h>
#include <beast/asio/wrap_handler.h>
#include <beast/asio/placeholders.h>
#include <beast/asio/shared_handler.h>

#include <boost/asio/detail/handler_cont_helpers.hpp>

namespace beast {
namespace asio {

/** A wrapper to decode the handshake data on a Stream.

    Stream must meet these requirements

    For detect:
        SyncReadStream

    For async_detect:
        AsyncReadStream

    Logic must meet this requirement:
        HandshakeDetectLogic
*/
template <typename Stream, typename Logic>
class HandshakeDetectorType
{
protected:
    typedef boost::system::error_code error_code;

public:
    Logic& getLogic ()
    {
        return m_logic.get ();
    }

    //--------------------------------------------------------------------------

    /** Synchronous handshake detect.
        The bytes from the input sequence in the specified buffer
        are used first.
    */
    template <typename Allocator>
    error_code detect (Stream& stream,
        boost::asio::basic_streambuf <Allocator>& buffer)
    {
        typedef boost::asio::basic_streambuf <Allocator> BuffersType;

        error_code ec;

        do
        {
            m_logic.analyze (buffer.data ());

            if (m_logic.finished ())
            {
                // consume what we used (for SSL its 0)
                std::size_t const consumed = m_logic.bytes_consumed ();
                bassert (consumed <= buffer.size ());
                buffer.consume (consumed);
                break;
            }

            std::size_t const available = buffer.size ();
            std::size_t const needed = m_logic.max_needed ();

            // If postcondition fails, loop will never end
            if (meets_postcondition (available < needed))
            {
                typename BuffersType::mutable_buffers_type buffers (
                    buffer.prepare (needed - available));
                buffer.commit (stream.read_some (buffers, ec));
            }
        }
        while (! ec);

        return ec;
    }

    //--------------------------------------------------------------------------

    /** Asynchronous handshake detect.
        The bytes from the input sequence in the specified buffer
        are used first.

        DetectHandler must have this signature:
            void(error_code)
    */
    template <typename Allocator>
    void async_detect (Stream& stream,
        boost::asio::basic_streambuf <Allocator>& buffer,
            asio::shared_handler <void(error_code)> handler)
    {
        typedef AsyncOp <Allocator> Op;
        auto const op (std::make_shared <Op> (std::ref (m_logic),
            std::ref (stream), std::ref (buffer), std::cref (handler)));
        //op->start();
        stream.get_io_service().post (asio::wrap_handler (std::bind (
            &Op::start, op), handler));
    }

private:
    template <typename Allocator>
    class AsyncOp
        : public std::enable_shared_from_this <AsyncOp <Allocator>>
    {
    public:
        typedef boost::asio::basic_streambuf <Allocator> BuffersType;
        
        AsyncOp (HandshakeDetectLogicType <Logic>& logic, Stream& stream,
            BuffersType& buffer, asio::shared_handler <
                void(error_code)> const& handler)
            : m_logic (logic)
            , m_stream (stream)
            , m_buffer (buffer)
            , m_handler (handler)
            , m_continuation (false)
        {
        }

        // Set breakpoint to prove it gets destroyed
        ~AsyncOp ()
        {
        }

        void start()
        {
            async_read_some (error_code(), 0);
        }

        void on_read (error_code ec, size_t bytes_transferred)
        {
            m_continuation = true;
            async_read_some (ec, bytes_transferred);
        }

        void async_read_some (error_code ec, size_t bytes_transferred)
        {
            if (! ec)
            {
                m_buffer.commit (bytes_transferred);

                m_logic.analyze (m_buffer.data ());

                if (!m_logic.finished ())
                {
                    std::size_t const available = m_buffer.size ();
                    std::size_t const needed = m_logic.max_needed ();

                    // If postcondition fails, loop will never end
                    if (meets_postcondition (available < needed))
                    {
                        typename BuffersType::mutable_buffers_type buffers (
                            m_buffer.prepare (needed - available));

                        m_stream.async_read_some (buffers, asio::wrap_handler (
                            std::bind (&AsyncOp <Allocator>::on_read,
                                this->shared_from_this(), asio::placeholders::error,
                                    asio::placeholders::bytes_transferred),
                                        m_handler, m_continuation));
                    }

                    return;
                }

                std::size_t const consumed = m_logic.bytes_consumed ();
                m_buffer.consume (consumed);
            }

            // Finalize with a call to the original handler.
            if (m_continuation)
            {
                m_handler (ec);
                return;
            }
            // Post, otherwise we would call the
            // handler from the initiating function.
            m_stream.get_io_service ().post (asio::bind_handler (
                m_handler, ec));
        }

    private:
        HandshakeDetectLogicType <Logic>& m_logic;
        Stream& m_stream;
        BuffersType& m_buffer;
        asio::shared_handler <void(error_code)> m_handler;
        bool m_continuation;
    };

private:
    HandshakeDetectLogicType <Logic> m_logic;
};

}
}

#endif
