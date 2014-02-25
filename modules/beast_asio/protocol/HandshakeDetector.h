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

//------------------------------------------------------------------------------

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
    template <typename DetectHandler, typename Allocator>
    void async_detect (Stream& stream,
        boost::asio::basic_streambuf <Allocator>& buffer,
            BOOST_ASIO_MOVE_ARG(DetectHandler) handler)
    {
        async_detect <Allocator> (stream, buffer, SharedHandlerPtr (
            new ErrorSharedHandlerType <DetectHandler> (
                BOOST_ASIO_MOVE_CAST(DetectHandler)(handler))));
    }

    template <typename Allocator>
    void async_detect (Stream& stream,
        boost::asio::basic_streambuf <Allocator>& buffer,
            SharedHandlerPtr handler)
    {
        typedef AsyncOp <Allocator> OpType;
        OpType* const op = new AsyncOp <Allocator> (
            m_logic, stream, buffer, handler);
        stream.get_io_service ().wrap (SharedHandlerPtr (op))
            (error_code (), 0);
    }

private:
    template <typename Allocator>
    struct AsyncOp : ComposedAsyncOperation
    {
        typedef boost::asio::basic_streambuf <Allocator> BuffersType;
        
        AsyncOp (HandshakeDetectLogicType <Logic>& logic, Stream& stream,
            BuffersType& buffer, SharedHandlerPtr const& handler)
            : ComposedAsyncOperation (handler)
            , m_logic (logic)
            , m_stream (stream)
            , m_buffer (buffer)
            , m_handler (handler)
            , m_running (false)
        {
        }

        // Set breakpoint to prove it gets destroyed
        ~AsyncOp ()
        {
        }

        void operator() (error_code const& ec_, size_t bytes_transferred)
        {
            m_running = true;

            error_code ec (ec_);

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

                        m_stream.async_read_some (buffers, SharedHandlerPtr (this));
                    }

                    return;
                }

                std::size_t const consumed = m_logic.bytes_consumed ();
                m_buffer.consume (consumed);
            }

            // Finalize with a call to the original handler.
            m_stream.get_io_service ().wrap (
                BOOST_ASIO_MOVE_CAST (SharedHandlerPtr)(m_handler))
                    (ec);
        }

        bool is_continuation ()
        {
            return m_running
#if BEAST_ASIO_HAS_CONTINUATION_HOOKS
                ||  boost_asio_handler_cont_helpers::is_continuation (m_handler);
#endif
                ;
        }

    private:
        HandshakeDetectLogicType <Logic>& m_logic;
        Stream& m_stream;
        BuffersType& m_buffer;
        SharedHandlerPtr m_handler;
        bool m_running;
    };

private:
    HandshakeDetectLogicType <Logic> m_logic;
};

#endif
