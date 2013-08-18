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

#ifndef BEAST_HANDSHAKEDETECTSTREAM_H_INCLUDED
#define BEAST_HANDSHAKEDETECTSTREAM_H_INCLUDED

/** A stream that can detect a handshake.
*/
/** @{ */
template <class Logic>
class HandshakeDetectStream
{
protected:
    typedef boost::system::error_code error_code;

public:
    typedef Logic LogicType;

    /** Called when the state is known.
        
        This could be called from any thread, most likely an io_service
        thread but don't rely on that.

        The Callback must be allocated via operator new.
    */
    struct Callback
    {
        virtual ~Callback () { }

        /** Called for synchronous ssl detection.

            Note that the storage for the buffers passed to the
            callback is owned by the detector class and becomes
            invalid when the detector class is destroyed, which is
            a common thing to do from inside your callback.

            @param ec A modifiable error code that becomes the return
                      value of handshake.
            @param buffers The bytes that were read in.
            @param is_ssl True if the sequence is an ssl handshake.
        */
        virtual void on_detect (Logic& logic,
            error_code& ec, ConstBuffers const& buffers) = 0;

        virtual void on_async_detect (Logic& logic,
            error_code const& ec, ConstBuffers const& buffers,
                HandlerCall const& origHandler) = 0;
    };
};

//------------------------------------------------------------------------------

template <typename Stream, typename Logic>
class HandshakeDetectStreamType
    : public HandshakeDetectStream <Logic>
    , public boost::asio::ssl::stream_base
    , public boost::asio::socket_base
{
private:
    typedef boost::system::error_code error_code;

    typedef HandshakeDetectStreamType <Stream, Logic> this_type;
    typedef boost::asio::streambuf buffer_type;
    typedef typename boost::remove_reference <Stream>::type stream_type;

public:
    typedef typename HandshakeDetectStream <Logic>::Callback CallbackType;

    /** This takes ownership of the callback.
        The callback must be allocated with operator new.
    */
    template <typename Arg>
    HandshakeDetectStreamType (CallbackType* callback, Arg& arg)
        : m_callback (callback)
        , m_next_layer (arg)
        , m_stream (m_next_layer)
    {
    }

    // This puts bytes that you already have into the detector buffer
    // Any leftovers will be given to the callback.
    // A copy of the data is made.
    //
    template <typename ConstBufferSequence>
    void fill (ConstBufferSequence const& buffers)
    {
        m_buffer.commit (boost::asio::buffer_copy (
            m_buffer.prepare (boost::asio::buffer_size (buffers)),
                buffers));
    }

    // basic_io_object

    boost::asio::io_service& get_io_service ()
    {
        return m_next_layer.get_io_service ();
    }

    // basic_socket

    typedef typename stream_type::protocol_type protocol_type;
    typedef typename stream_type::lowest_layer_type lowest_layer_type;

    lowest_layer_type& lowest_layer ()
    {
        return m_next_layer.lowest_layer ();
    }

    lowest_layer_type const& lowest_layer () const
    {
        return m_next_layer.lowest_layer ();
    }

    // ssl::stream

    error_code handshake (handshake_type type, error_code& ec)
    {
        return do_handshake (type, ec, ConstBuffers ());
    }

    template <typename HandshakeHandler>
    BEAST_ASIO_INITFN_RESULT_TYPE(HandshakeHandler, void (error_code))
    async_handshake (handshake_type type, BOOST_ASIO_MOVE_ARG(HandshakeHandler) handler)
    {
#if BEAST_ASIO_HAS_FUTURE_RETURNS
        boost::asio::detail::async_result_init<
            HandshakeHandler, void (error_code)> init(
                BOOST_ASIO_MOVE_CAST(HandshakeHandler)(handler));
        // init.handler is copied
        m_origHandler = HandlerCall (
            BOOST_ASIO_MOVE_CAST(HandshakeHandler)
                (HandshakeHandler(init.handler)));
        async_do_handshake (type, ConstBuffers ());
        return init.result.get();
#else
        m_origHandler = HandlerCall (
            BOOST_ASIO_MOVE_CAST(HandshakeHandler)(handler));
        async_do_handshake (type, ConstBuffers ());
#endif
    }

    //--------------------------------------------------------------------------

    error_code do_handshake (handshake_type, error_code& ec, ConstBuffers const& buffers)
    {
        ec = error_code ();

        // Transfer caller data to our buffer.
        m_buffer.commit (boost::asio::buffer_copy (m_buffer.prepare (
            boost::asio::buffer_size (buffers)), buffers));

        do
        {
            std::size_t const available = m_buffer.size ();
            std::size_t const needed = m_logic.max_needed ();
            if (available < needed)
            {
                buffer_type::mutable_buffers_type buffers (
                    m_buffer.prepare (needed - available));
                m_buffer.commit (m_next_layer.read_some (buffers, ec));
            }

            if (! ec)
            {
                m_logic.analyze (m_buffer.data ());

                if (m_logic.finished ())
                {
                    // consume what we used (for SSL its 0)
                    std::size_t const consumed = m_logic.bytes_consumed ();
                    bassert (consumed <= m_buffer.size ());
                    m_buffer.consume (consumed);
                    m_callback->on_detect (m_logic.get (), ec,
                        ConstBuffers (m_buffer.data ()));
                    break;
                }

                // If this fails it means we will never finish
                check_postcondition (available < needed);
            }
        }
        while (! ec);

        return ec;
    }

    //--------------------------------------------------------------------------

    void async_do_handshake (handshake_type type, ConstBuffers const& buffers)
    {
        // Get the execution context from the original handler
        // and signal the beginning of our composed operation.
        //
        m_origHandler.beginComposed ();
        m_context = m_origHandler.getContext ();

        bassert (m_context.isNotNull ());

        // Transfer caller data to our buffer.
        // We commit the bytes in on_async_read_some.
        //
        std::size_t const bytes_transferred (boost::asio::buffer_copy (
            m_buffer.prepare (boost::asio::buffer_size (buffers)), buffers));

        // bootstrap the asynchronous loop
        on_async_read_some (error_code (), bytes_transferred);
    }

    // asynchronous version of the synchronous loop found in handshake ()
    //
    void on_async_read_some (error_code const& ec, std::size_t bytes_transferred)
    {
        if (! ec)
        {
            m_buffer.commit (bytes_transferred);

            std::size_t const available = m_buffer.size ();
            std::size_t const needed = m_logic.max_needed ();

            if (bytes_transferred > 0)
                m_logic.analyze (m_buffer.data ());

            if (m_logic.finished ())
            {
                // consume what we used (for SSL its 0)
                std::size_t const consumed = m_logic.bytes_consumed ();
                bassert (consumed <= m_buffer.size ());
                m_buffer.consume (consumed);

                // The composed operation has completed and
                // the original handler will eventually get called.
                //
                m_origHandler.endComposed ();
                m_callback->on_async_detect (m_logic.get (), ec,
                   ConstBuffers (m_buffer.data ()), m_origHandler);
                return;
            }

            // If this fails it means we will never finish
            check_postcondition (available < needed);

            buffer_type::mutable_buffers_type buffers (m_buffer.prepare (
                needed - available));

            // Perform the asynchronous operation using the context
            // of the original handler. This ensures that we meet the
            // execution safety requirements of the handler.
            //
            HandlerCall handler (HandlerCall::Read (), m_context,
                boost::bind (&this_type::on_async_read_some, this,
                    boost::asio::placeholders::error,
                        boost::asio::placeholders::bytes_transferred));
            m_next_layer.async_read_some (buffers, handler);

            return;
        }

        // Error condition

        m_origHandler.endComposed ();
        m_callback->on_async_detect (m_logic.get (), ec,
            ConstBuffers (m_buffer.data ()), m_origHandler);
    }

private:
    ScopedPointer <CallbackType> m_callback;
    Stream m_next_layer;
    buffer_type m_buffer;
    boost::asio::buffered_read_stream <stream_type&> m_stream;
    HandshakeDetectLogicType <Logic> m_logic;
    HandlerCall m_origHandler;
    HandlerCall m_origBufferedHandler;
    HandlerCall::Context m_context;
};
/** @} */

#endif
