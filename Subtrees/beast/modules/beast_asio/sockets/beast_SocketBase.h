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

#ifndef BEAST_SOCKETBASE_H_INCLUDED
#define BEAST_SOCKETBASE_H_INCLUDED

/** Implementation details for AbstractSocket.
    Normally you wont need to use this.
*/
class SocketBase
{
protected:
    //--------------------------------------------------------------------------
    //
    // Buffers
    //
    //--------------------------------------------------------------------------

    /** Storage for a BufferSequence.

        Meets these requirements:
            BufferSequence
            ConstBufferSequence (when Buffer is mutable_buffer)
            MutableBufferSequence (when Buffer is const_buffer)
    */
    template <class Buffer>
    class Buffers
    {
    public:
        typedef Buffer value_type;
        typedef typename std::vector <Buffer>::const_iterator const_iterator;

        Buffers ()
            : m_size (0)
        {
        }

        template <class OtherBuffers>
        explicit Buffers (OtherBuffers const& buffers)
            : m_size (0)
        {
            m_buffers.reserve (std::distance (buffers.begin (), buffers.end ()));
            BOOST_FOREACH (typename OtherBuffers::value_type buffer, buffers)
            {
                m_size += boost::asio::buffer_size (buffer);
                m_buffers.push_back (buffer);
            }
        }

        /** Determine the total size of all buffers.
            This is faster than calling boost::asio::buffer_size.
        */
        std::size_t size () const noexcept
        {
            return m_size;
        }

        const_iterator begin () const noexcept
        {
            return m_buffers.begin ();
        }

        const_iterator end () const noexcept
        {
            return m_buffers.end ();
        }

        /** Retrieve a consumed BufferSequence. */
        Buffers consumed (std::size_t bytes) const
        {
            Buffers result;
            result.m_buffers.reserve (m_buffers.size ());
            BOOST_FOREACH (Buffer buffer, m_buffers)
            {
                std::size_t const have = boost::asio::buffer_size (buffer);
                std::size_t const reduce = std::min (bytes, have);
                bytes -= reduce;

                if (have > reduce)
                    result.m_buffers.push_back (buffer + reduce);
            }
            return result;
        }

    private:
        std::size_t m_size;
        std::vector <Buffer> m_buffers;
    };

    /** Meets the requirements of ConstBufferSequence */
    typedef Buffers <boost::asio::const_buffer> ConstBuffers;

    /** Meets the requirements of MutableBufferSequence */
    typedef Buffers <boost::asio::mutable_buffer> MutableBuffers;

    //--------------------------------------------------------------------------
    //
    // Handler abstractions
    //
    //--------------------------------------------------------------------------

    //  Meets these requirements:
    //
    //      CompletionHandler
    //      http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/CompletionHandler.html
    //
    class CompletionCall
    {
    public:
        typedef void result_type;

        template <class Handler>
        CompletionCall (BOOST_ASIO_MOVE_ARG (Handler) handler)
            : m_call (new CallType <Handler> (handler))
        {
        }

        CompletionCall (CompletionCall const& other)
            : m_call (other.m_call)
        { 
        }

        void operator() ()
        {
            (*m_call) ();
        }

    private:
        struct Call : SharedObject, LeakChecked <Call>
        {
            virtual void operator() () = 0;
        };

        template <class Handler>
        struct CallType : Call
        {
            CallType (BOOST_ASIO_MOVE_ARG (Handler) handler)
                : m_handler (handler)
            {
            }

            void operator() ()
            {
                m_handler ();
            }

            Handler m_handler;
        };

    private:
        SharedObjectPtr <Call> m_call;
    };

    //  Meets these requirements:
    //
    //      AcceptHandler
    //      http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/AcceptHandler.html
    //
    //      ConnectHandler
    //      http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/ConnectHandler.html
    //
    //      ShutdownHandler
    //      http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/ShutdownHandler.html
    //
    //      HandshakeHandler
    //      http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/HandshakeHandler.html
    //
    class ErrorCall
    {
    public:
        typedef void result_type;

        template <class Handler>
        ErrorCall (BOOST_ASIO_MOVE_ARG (Handler) handler)
            : m_call (new CallType <Handler> (handler))
        {
        }

        ErrorCall (ErrorCall const& other)
            : m_call (other.m_call)
        { 
        }

        void operator() (boost::system::error_code const& ec)
        {
            (*m_call) (ec);
        }

    private:
        struct Call : SharedObject, LeakChecked <Call>
        {
            virtual void operator() (boost::system::error_code const&) = 0;
        };

        template <class Handler>
        struct CallType : Call
        {
            CallType (BOOST_ASIO_MOVE_ARG (Handler) handler)
                : m_handler (handler)
            {
            }

            void operator() (boost::system::error_code const& ec)
            {
                m_handler (ec);
            }

            Handler m_handler;
        };

    private:
        SharedObjectPtr <Call> m_call;
    };

    //  Meets these requirements
    //
    //      ReadHandler
    //      http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/ReadHandler.html
    //
    //      WriteHandler
    //      http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/WriteHandler.html
    //
    //      BUfferedHandshakeHandler
    //      http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/BufferedHandshakeHandler.html
    //
    class TransferCall
    {
    public:
        typedef void result_type;

        template <class Handler>
        TransferCall (BOOST_ASIO_MOVE_ARG (Handler) handler)
            : m_call (new CallType <Handler> (handler))
        {
        }

        TransferCall (TransferCall const& other)
            : m_call (other.m_call)
        { 
        }

        void operator() (boost::system::error_code const& ec, std::size_t bytes_transferred)
        {
            (*m_call) (ec, bytes_transferred);
        }

    private:
        struct Call : SharedObject, LeakChecked <Call>
        {
            virtual void operator() (boost::system::error_code const&, std::size_t) = 0;
        };

        template <class Handler>
        struct CallType : Call
        {
            CallType (BOOST_ASIO_MOVE_ARG (Handler) handler)
                : m_handler (handler)
            {
            }

            void operator() (boost::system::error_code const& ec, std::size_t bytes_transferred)
            {
                m_handler (ec, bytes_transferred);
            }

            Handler m_handler;
        };

    private:
        SharedObjectPtr <Call> m_call;
    };

    /** Called when the underlying object does not support the interface. */
    void throw_error (boost::system::error_code const& ec)
    {
        if (ec)
            Throw (boost::system::system_error (ec), __FILE__, __LINE__);
    }
};


#endif
