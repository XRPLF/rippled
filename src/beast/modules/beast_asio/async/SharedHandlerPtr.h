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

#ifndef BEAST_ASIO_ASYNC_SHAREDHANDLERPTR_H_INCLUDED
#define BEAST_ASIO_ASYNC_SHAREDHANDLERPTR_H_INCLUDED

/** RAII container for a SharedHandler.

    This object behaves exactly like a SharedHandler except that it
    merely contains a shared pointer to the underlying SharedHandler.
    All calls are forwarded to the underlying SharedHandler, and all
    of the execution safety guarantees are met by forwarding them through
    to the underlying SharedHandler.
*/
class SharedHandlerPtr
{
protected:
    typedef boost::system::error_code error_code;

public:
    // For asio::async_result<>
    typedef void result_type;

    /** Construct a null handler.
        A null handler cannot be called. It can, however, be checked
        for validity by calling isNull, and later assigned.

        @see isNull, isNotNull
    */
    inline SharedHandlerPtr ()
    {
    }

    /** Construct from an existing SharedHandler.
        Ownership of the handler is transferred to the container.
    */
    inline SharedHandlerPtr (SharedHandler* handler)
        : m_ptr (handler)
    {
    }

    /** Construct a reference from an existing container. */
    inline SharedHandlerPtr (SharedHandlerPtr const& other)
        : m_ptr (other.m_ptr)
    {
    }

    /** Assign a reference from an existing container. */
    inline SharedHandlerPtr& operator= (SharedHandlerPtr const& other)
    {
        m_ptr = other.m_ptr;
        return *this;
    }

#if BEAST_COMPILER_SUPPORTS_MOVE_SEMANTICS
    /** Move-construct a reference from an existing container.
        The other container is set to a null handler.
    */
    inline SharedHandlerPtr (SharedHandlerPtr&& other)
        : m_ptr (other.m_ptr)
    {
        other.m_ptr = nullptr;
    }

    /** Move-assign a reference from an existing container.
        The other container is set to a null handler.
    */
    inline SharedHandlerPtr& operator= (SharedHandlerPtr&& other)
    {
        m_ptr = other.m_ptr;
        other.m_ptr = nullptr;
        return *this;
    }
#endif

    /** Returns true if the handler is a null handler. */
    inline bool isNull () const
    {
        return m_ptr == nullptr;
    }

    /** Returns true if the handler is not a null handler. */
    inline bool isNotNull () const
    {
        return m_ptr != nullptr;
    }

    /** Dereference the container.
        This returns a reference to the underlying SharedHandler object.
    */
    inline SharedHandler& operator* () const
    {
        return *m_ptr;
    }

    /** SharedHandler member access.
        This lets you call functions directly on the SharedHandler.
    */
    inline SharedHandler* operator-> () const
    {
        return m_ptr.get ();
    }

    /** Retrieve the SharedHandler as a Context.

        This can be used for invoking functions in the context:

        @code

        template <Function>
        void callOnHandler (Function f, SharedHandlerPtr ptr)
        {
            boost::asio_handler_invoke (f, ptr.get ());
        }

        @endcode
    */
    inline SharedHandler* get () const
    {
        return m_ptr.get ();
    }

    /** Invoke the SharedHandler with signature void(void)
        Normally this is called by a dispatcher, you shouldn't call it directly.
    */
    inline void operator() () const
    {
        (*m_ptr)();
    }

    /** Invoke the SharedHandler with signature void(error_code)
        Normally this is called by a dispatcher, you shouldn't call it directly.
    */
    inline void operator() (error_code const& ec) const
    {
        (*m_ptr)(ec);
    }

    /** Invoke the SharedHandler with signature void(error_code, size_t)
        Normally this is called by a dispatcher, you shouldn't call it directly.
    */
    inline void operator() (error_code const& ec, std::size_t bytes_transferred) const
    {
        (*m_ptr)(ec, bytes_transferred);
    }

private:
    // These ensure that SharedHandlerPtr invocations adhere to
    // the asio::io_service execution guarantees of the underlying SharedHandler.
    //
    template <typename Function>
    friend void  asio_handler_invoke (BOOST_ASIO_MOVE_ARG(Function) f, SharedHandlerPtr*);
    friend void* asio_handler_allocate (std::size_t, SharedHandlerPtr*);
    friend void  asio_handler_deallocate (void*, std::size_t, SharedHandlerPtr*);
    friend bool  asio_handler_is_continuation (SharedHandlerPtr*);

    SharedHandler::Ptr m_ptr;
};

//--------------------------------------------------------------------------
//
// Context execution guarantees
//

template <class Function>
void asio_handler_invoke (BOOST_ASIO_MOVE_ARG(Function) f, SharedHandlerPtr* ptr)
{
    boost_asio_handler_invoke_helpers::
        invoke <Function, SharedHandler>
            (BOOST_ASIO_MOVE_CAST(Function)(f), *ptr->get ());
}

inline void* asio_handler_allocate (std::size_t size, SharedHandlerPtr* ptr)
{
    return boost_asio_handler_alloc_helpers::
        allocate <SharedHandler> (size, *ptr->get ());
}

inline void asio_handler_deallocate (void* p, std::size_t size, SharedHandlerPtr* ptr)
{
    boost_asio_handler_alloc_helpers::
        deallocate <SharedHandler> (p, size, *ptr->get ());
}

inline bool asio_handler_is_continuation (SharedHandlerPtr* ptr)
{
#if BEAST_ASIO_HAS_CONTINUATION_HOOKS
    return boost_asio_handler_cont_helpers::
        is_continuation <SharedHandler> (*ptr->get ());
#else
    return false;
#endif
}

//--------------------------------------------------------------------------
//
// Helpers
//
//--------------------------------------------------------------------------

// void(error_code)
template <typename Handler>
SharedHandlerPtr newErrorHandler (
    BOOST_ASIO_MOVE_ARG(Handler) handler)
{
    return newSharedHandlerContainer <ErrorSharedHandlerType> (
        BOOST_ASIO_MOVE_CAST(Handler)(handler));
}

// void(error_code, size_t)
template <typename Handler>
SharedHandlerPtr newTransferHandler (
    BOOST_ASIO_MOVE_ARG(Handler) handler)
{
    return newSharedHandlerContainer <TransferSharedHandlerType> (
        BOOST_ASIO_MOVE_CAST(Handler)(handler));
}

//--------------------------------------------------------------------------

// CompletionHandler
//
// http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/CompletionHandler.html
//
template <typename CompletionHandler>
SharedHandlerPtr newCompletionHandler (
    BOOST_ASIO_MOVE_ARG(CompletionHandler) handler)
{
    return newSharedHandlerContainer <PostSharedHandlerType> (
        BOOST_ASIO_MOVE_CAST(CompletionHandler)(handler));
}

// AcceptHandler
// http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/AcceptHandler.html
//
template <typename AcceptHandler>
SharedHandlerPtr newAcceptHandler (
    BOOST_ASIO_MOVE_ARG(AcceptHandler) handler)
{
    return newSharedHandlerContainer <ErrorSharedHandlerType> (
        BOOST_ASIO_MOVE_CAST(AcceptHandler)(handler));
}

// ConnectHandler
// http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/ConnectHandler.html
//
template <typename ConnectHandler>
SharedHandlerPtr newConnectHandler (
    BOOST_ASIO_MOVE_ARG(ConnectHandler) handler)
{
    return newSharedHandlerContainer <ErrorSharedHandlerType> (
        BOOST_ASIO_MOVE_CAST(ConnectHandler)(handler));
}

// ShutdownHandler
// http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/ShutdownHandler.html
//
template <typename ShutdownHandler>
SharedHandlerPtr newShutdownHandler(
    BOOST_ASIO_MOVE_ARG(ShutdownHandler) handler)
{
    return newSharedHandlerContainer <ErrorSharedHandlerType> (
        BOOST_ASIO_MOVE_CAST(ShutdownHandler)(handler));
}

// HandshakeHandler
// http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/HandshakeHandler.html
//
template <typename HandshakeHandler>
SharedHandlerPtr newHandshakeHandler(
    BOOST_ASIO_MOVE_ARG(HandshakeHandler) handler)
{
    return newSharedHandlerContainer <ErrorSharedHandlerType> (
        BOOST_ASIO_MOVE_CAST(HandshakeHandler)(handler));
}

// ReadHandler
// http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/ReadHandler.html
//
template <typename ReadHandler>
SharedHandlerPtr newReadHandler(
    BOOST_ASIO_MOVE_ARG(ReadHandler) handler)
{
    return newSharedHandlerContainer <TransferSharedHandlerType> (
        BOOST_ASIO_MOVE_CAST(ReadHandler)(handler));
}

// WriteHandler
// http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/WriteHandler.html
//
template <typename WriteHandler>
SharedHandlerPtr newWriteHandler(
    BOOST_ASIO_MOVE_ARG(WriteHandler) handler)
{
    return newSharedHandlerContainer <TransferSharedHandlerType> (
        BOOST_ASIO_MOVE_CAST(WriteHandler)(handler));
}

// BufferedHandshakeHandler
// http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/BufferedHandshakeHandler.html
//
template <typename BufferedHandshakeHandler>
SharedHandlerPtr newBufferedHandshakeHandler(
    BOOST_ASIO_MOVE_ARG(BufferedHandshakeHandler) handler)
{
     return newSharedHandlerContainer <TransferSharedHandlerType> (
        BOOST_ASIO_MOVE_CAST(BufferedHandshakeHandler)(handler));
}

#endif
