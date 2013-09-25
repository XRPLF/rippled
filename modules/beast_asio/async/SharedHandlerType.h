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

#ifndef BEAST_ASIO_ASYNC_SHAREDHANDLERTYPE_H_INCLUDED
#define BEAST_ASIO_ASYNC_SHAREDHANDLERTYPE_H_INCLUDED

/** An instance of SharedHandler that wraps an existing Handler.
    The wrapped handler will meet all the execution guarantees of
    the original Handler object.
*/
template <typename Handler>
class SharedHandlerType : public SharedHandler
{
protected:
    SharedHandlerType (std::size_t size,
        BOOST_ASIO_MOVE_ARG(Handler) handler)
        : m_size (size)
        , m_handler (BOOST_ASIO_MOVE_CAST(Handler)(handler))
    {
    }

    ~SharedHandlerType ()
    {

    }

    void invoke (invoked_type& invoked)
    {
        boost_asio_handler_invoke_helpers::
            invoke <invoked_type, Handler> (invoked, m_handler);
    }

    void* allocate (std::size_t size)
    {
        return boost_asio_handler_alloc_helpers::
            allocate <Handler> (size, m_handler);
    }

    void deallocate (void* p, std::size_t size)
    {
        boost_asio_handler_alloc_helpers::
            deallocate <Handler> (p, size, m_handler);
    }

    bool is_continuation ()
    {
#if BEAST_ASIO_HAS_CONTINUATION_HOOKS
        return boost_asio_handler_cont_helpers::
            is_continuation <Handler> (m_handler);
#else
        return false;
#endif
    }

    // Called by SharedObject hook to destroy the object. We need
    // this because we allocated it using a custom allocator.
    // Destruction is tricky, the algorithm is as follows:
    //
    // First we move-assign the handler to our stack. If the build
    // doesn't support move-assignment it will be a copy, still ok.
    // We convert 'this' to a pointer to the polymorphic base, to
    // ensure that the following direct destructor call will reach
    // the most derived class. Finally, we deallocate the memory
    // using the handler that is local to the stack.
    //
    // For this to work we need to make sure regular operator delete
    // is never called for our object (it's private). We also need
    // the size from the original allocation, which we saved at
    // the time of construction.
    //
    void destroy () const
    {
        Handler local (BOOST_ASIO_MOVE_CAST(Handler)(m_handler));
        std::size_t const size (m_size);
        SharedHandler* const shared (
            const_cast <SharedHandler*> (
                static_cast <SharedHandler const*>(this)));
        shared->~SharedHandler ();
        boost_asio_handler_alloc_helpers::
            deallocate <Handler> (shared, size, local);
    }

protected:
    std::size_t const m_size;
    Handler mutable m_handler;
};

//--------------------------------------------------------------------------
//
// A SharedHandlerType for this signature:
//  void(void)
//
template <typename Handler>
class PostSharedHandlerType : public SharedHandlerType <Handler>
{
public:
    PostSharedHandlerType (std::size_t size,
        BOOST_ASIO_MOVE_ARG(Handler) handler)
        : SharedHandlerType <Handler> (size,
            BOOST_ASIO_MOVE_CAST(Handler)(handler))
    {
    }

protected:
    void operator() ()
    {
        this->m_handler ();
    }
};

//--------------------------------------------------------------------------
//
// A SharedHandlerType for this signature:
//  void(error_code)
//
template <typename Handler>
class ErrorSharedHandlerType : public SharedHandlerType <Handler>
{
public:
    ErrorSharedHandlerType (std::size_t size,
        BOOST_ASIO_MOVE_ARG(Handler) handler)
        : SharedHandlerType <Handler> (size,
            BOOST_ASIO_MOVE_CAST(Handler)(handler))
    {
    }

protected:
    void operator() (boost::system::error_code const& ec)
    {
        this->m_handler (ec);
    }
};

//--------------------------------------------------------------------------
//
// A SharedHandlerType for this signature:
//  void(error_code, size_t)
//
template <typename Handler>
class TransferSharedHandlerType : public SharedHandlerType <Handler>
{
public:
    TransferSharedHandlerType (std::size_t size,
        BOOST_ASIO_MOVE_ARG(Handler) handler)
        : SharedHandlerType <Handler> (size,
            BOOST_ASIO_MOVE_CAST(Handler)(handler))
    {
    }

protected:
    void operator() (boost::system::error_code const& ec,
        std::size_t bytes_transferred)
    {
        this->m_handler (ec, bytes_transferred);
    }
};

//--------------------------------------------------------------------------
//
// These specializations will make sure we don't do
// anything silly like wrap ourselves in our own wrapper...
//
#if 1
template <>
class PostSharedHandlerType <SharedHandler>
{
};

template <>
class ErrorSharedHandlerType <SharedHandler>
{
};
template <>
class TransferSharedHandlerType <SharedHandler>
{
};
#endif


//--------------------------------------------------------------------------

/** Construct a wrapped handler using the context's allocation hooks.
*/
template <template <typename> class Container, typename Handler>
Container <Handler>* newSharedHandlerContainer (BOOST_ASIO_MOVE_ARG(Handler) handler)
{
    typedef Container <Handler> ContainerType;
    std::size_t const size (sizeof (ContainerType));
    Handler local (BOOST_ASIO_MOVE_CAST(Handler)(handler));
    void* const p (boost_asio_handler_alloc_helpers::
        allocate <Handler> (size, local));
    return new (p) ContainerType (size, BOOST_ASIO_MOVE_CAST(Handler)(local));
}

#endif
