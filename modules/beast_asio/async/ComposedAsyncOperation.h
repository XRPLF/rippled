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

#ifndef BEAST_ASIO_ASYNC_COMPOSEDASYNCOPERATION_H_INCLUDED
#define BEAST_ASIO_ASYNC_COMPOSEDASYNCOPERATION_H_INCLUDED

/** Base class for creating composed asynchronous operations.
    The composed operation will have its operator() overloads called with
    the same context and execution safety guarantees as the original
    SharedHandler.
*/
class ComposedAsyncOperation : public SharedHandler
{
protected:
    /** Construct the composed operation.
        The composed operation will execute in the context of the
        SharedHandler. A reference to the SharedHandler is maintained
        for the lifetime of the composed operation.
    */
    explicit ComposedAsyncOperation (SharedHandlerPtr const& ptr)
        : m_ptr (ptr)
    {
        // Illegal to do anything with handler here, because
        // usually it hasn't been assigned by the derived class yet.
    }

    ~ComposedAsyncOperation ()
    {
    }

    void invoke (invoked_type& invoked)
    {
        boost_asio_handler_invoke_helpers::
            invoke (invoked, *m_ptr);
    }

    void* allocate (std::size_t size)
    {
        return boost_asio_handler_alloc_helpers::
            allocate (size, *m_ptr);
    }

    void deallocate (void* p, std::size_t size)
    {
        boost_asio_handler_alloc_helpers::
            deallocate (p, size, *m_ptr);
    }

    /** Override this function as needed.
        Usually you will logical-and your own continuation condition. In
        the following example, isContinuing is a derived class member:

        @code

        bool derived::is_continuation ()
        {
            bool const ourResult = this->isContinuing ()
            return ourResult || ComposedAsyncOperation <Handler>::is_contiation ();
        }

        @endcode
    */
    bool is_continuation ()
    {
#if BEAST_ASIO_HAS_CONTINUATION_HOOKS
        return boost_asio_handler_cont_helpers::
            is_continuation (*m_ptr);
#else
        return false;
#endif
    }

    void destroy () const
    {
        delete this;
    }

private:
    SharedHandlerPtr const m_ptr;
};

#endif
