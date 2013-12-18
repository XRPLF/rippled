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

#ifndef BEAST_ASIO_ABSTRACTHANDLER_H_INCLUDED
#define BEAST_ASIO_ABSTRACTHANDLER_H_INCLUDED

namespace beast {

namespace detail {

struct AbstractHandlerCallBase : SharedObject
{
    //typedef SharedFunction <void(void),
    //    AbstractHandlerAllocator <char> > invoked_type;

    typedef SharedFunction <void(void)> invoked_type;

    virtual void* allocate (std::size_t size) = 0;
    virtual void deallocate (void* p, std::size_t size) = 0;
    virtual bool is_continuation () = 0;
    virtual void invoke (invoked_type& invoked) = 0;

    template <typename Function>
    void invoke (BEAST_MOVE_ARG(Function) f)
    {
        invoked_type invoked (BEAST_MOVE_CAST(Function)(f)
            //, AbstractHandlerAllocator<char>(this)
            );
        invoke (invoked);
    }
};

/*
template <typename T>
struct AbstractHandlerAllocator
{
    typedef T value_type;
    typedef T* pointer;
    typedef T& reference;
    typedef T const* const_pointer;
    typedef T const& const_reference;
    typedef std::size_t size_type;
    typedef std::ptrdiff_t difference_type;

    AbstractHandlerAllocator (AbstractHandler* handler) noexcept
        : m_ptr (handler)
    {
    }

    AbstractHandlerAllocator (SharedPtr <AbstractHandler> const& ptr) noexcept
        : m_ptr (ptr)
    {
    }

    template <typename U>
    AbstractHandlerAllocator (AbstractHandlerAllocator <U> const& other)
        : m_ptr (other.m_ptr)
    {
    }

    template <typename U>
    struct rebind
    {
        typedef AbstractHandlerAllocator <U> other;
    };

    pointer address (reference x) const
    {
        return &x;
    }

    const_pointer address (const_reference x) const
    {
        return &x;
    }

    pointer allocate (size_type n) const
    {
        size_type const bytes = n * sizeof (value_type);
        return static_cast <pointer> (m_ptr->allocate (bytes));
    }

    void deallocate (pointer p, size_type n) const
    {
        size_type const bytes = n * sizeof (value_type);
        m_ptr->deallocate (p, bytes);
    }

    size_type max_size () const noexcept
    {
        return std::numeric_limits <size_type>::max () / sizeof (value_type);
    }

    void construct (pointer p, const_reference val) const
    {
        new ((void *)p) value_type (val);
    }

    void destroy (pointer p) const
    {
        p->~value_type ();
    }

private:
    template <typename>
    friend struct AbstractHandlerAllocator;
    friend class AbstractHandler;

    SharedPtr <AbstractHandler> m_ptr;
};
*/

}

/** A reference counted, abstract completion handler. */
template <typename Signature, class Allocator = std::allocator <char> >
struct AbstractHandler;

//------------------------------------------------------------------------------

// arity 0
template <class R, class A>
struct AbstractHandler <R (void), A>
{
    typedef R result_type;
    struct Call : detail::AbstractHandlerCallBase
        { virtual R operator() () = 0; };

    template <typename H>
    struct CallType : public Call
    {
        typedef typename A:: template rebind <CallType <H> >::other Allocator;
        CallType (BEAST_MOVE_ARG(H) h, A a = A ())
            : m_h (BEAST_MOVE_CAST(H)(h)), m_alloc (a)
            { }
        R operator()()
            { return (m_h)(); }
        R operator()() const
            { return (m_h)(); }
        void* allocate (std::size_t size)
            { return boost_asio_handler_alloc_helpers::allocate(size, m_h); }
        void deallocate (void* pointer, std::size_t size)
            { boost_asio_handler_alloc_helpers::deallocate( pointer, size, m_h); }
        bool is_continuation ()
#if BEAST_ASIO_HAS_CONTINUATION_HOOKS
            { return boost_asio_handler_cont_helpers::is_continuation(m_h); }
#else
            { return false; }
#endif
        void invoke (typename detail::AbstractHandlerCallBase::invoked_type& invoked)
            { boost_asio_handler_invoke_helpers::invoke <
                typename detail::AbstractHandlerCallBase::invoked_type, H> (invoked, m_h); }
    private:
        H m_h;
        Allocator m_alloc;
    };

    template <typename H>
    AbstractHandler (BEAST_MOVE_ARG(H) h, A a = A ())
        : m_call (new (
            typename A:: template rebind <CallType <H> >::other (a)
                .allocate (1)) CallType <H> (BEAST_MOVE_CAST(H)(h), a))
        { }
    R operator() ()
        { return (*m_call)(); }
    R operator() () const
        { return (*m_call)(); }
    void* allocate (std::size_t size) const { return m_call->allocate(size); }
    void deallocate (void* pointer, std::size_t size) const { m_call->deallocate(pointer,size); }
    bool is_continuation () const { return m_call->is_continuation(); }
    template <typename Function>
    void invoke (Function& function) const
    {
        m_call->invoke(function);
    }
    template <typename Function>
    void invoke (Function const& function) const
    {
        m_call->invoke(function);
    }

private:
    SharedPtr <Call> m_call;
};

template <class R, class A>
void* asio_handler_allocate (std::size_t size,
    AbstractHandler <R (void), A>* handler)
{
    return handler->allocate (size);
}

template <class R, class A>
void asio_handler_deallocate (void* pointer, std::size_t size,
    AbstractHandler <R (void), A>* handler)
{
    handler->deallocate (pointer, size);
}

template <class R, class A>
bool asio_handler_is_continuation(
    AbstractHandler <R (void), A>* handler)
{
    return handler->is_continuation();
}

template <typename Function, class R, class A>
void asio_handler_invoke (BEAST_MOVE_ARG(Function) function,
    AbstractHandler <R (void), A>* handler)
{
    handler->invoke (BEAST_MOVE_CAST(Function)(function));
}

//------------------------------------------------------------------------------

// arity 1
template <class R, class P1, class A>
struct AbstractHandler <R (P1), A>
{
    typedef R result_type;
    struct Call : detail::AbstractHandlerCallBase
        { virtual R operator() (P1) = 0; };

    template <typename H>
    struct CallType : public Call
    {
        typedef typename A:: template rebind <CallType <H> >::other Allocator;
        CallType (H h, A a = A ())
            : m_h (h)
            , m_alloc (a)
        {
        }

        R operator()(P1 p1)
            { return (m_h)(p1); }
        R operator()(P1 p1) const
            { return (m_h)(p1); }
        void* allocate (std::size_t size)
            { return boost_asio_handler_alloc_helpers::allocate(size, m_h); }
        void deallocate (void* pointer, std::size_t size)
            { boost_asio_handler_alloc_helpers::deallocate( pointer, size, m_h); }
        bool is_continuation ()
#if BEAST_ASIO_HAS_CONTINUATION_HOOKS
            { return boost_asio_handler_cont_helpers::is_continuation(m_h); }
#else
            { return false; }
#endif
        void invoke (typename detail::AbstractHandlerCallBase::invoked_type& invoked)
            { boost_asio_handler_invoke_helpers::invoke <
                typename detail::AbstractHandlerCallBase::invoked_type, H> (invoked, m_h); }
    private:
        H m_h;
        Allocator m_alloc;
    };

    template <typename H>
    AbstractHandler (H h, A a = A ())
        : m_call (new (
            typename A:: template rebind <CallType <H> >::other (a)
                .allocate (1)) CallType <H> (h, a))
    {
    }

    R operator() (P1 p1)
        { return (*m_call)(p1); }
    R operator() (P1 p1) const
        { return (*m_call)(p1); }
    void* allocate (std::size_t size) const { return m_call->allocate(size); }
    void deallocate (void* pointer, std::size_t size) const { m_call->deallocate(pointer,size); }
    bool is_continuation () const { return m_call->is_continuation(); }
    template <typename Function>
    void invoke (Function& function) const
    {
        m_call->invoke(function);
    }
    template <typename Function>
    void invoke (Function const& function) const
    {
        m_call->invoke(function);
    }

private:
    SharedPtr <Call> m_call;
};

template <class R, class P1, class A>
void* asio_handler_allocate (std::size_t size,
    AbstractHandler <R (P1), A>* handler)
{
    return handler->allocate (size);
}

template <class R, class P1, class A>
void asio_handler_deallocate (void* pointer, std::size_t size,
    AbstractHandler <R (P1), A>* handler)
{
    handler->deallocate (pointer, size);
}

template <class R, class P1, class A>
bool asio_handler_is_continuation(
    AbstractHandler <R (P1), A>* handler)
{
    return handler->is_continuation();
}

template <typename Function, class R, class P1, class A>
void asio_handler_invoke (BEAST_MOVE_ARG(Function) function,
    AbstractHandler <R (P1), A>* handler)
{
    handler->invoke (BEAST_MOVE_CAST(Function)(function));
}

//------------------------------------------------------------------------------

// arity 2
template <class R, class P1, class P2, class A>
struct AbstractHandler <R (P1, P2), A>
{
    typedef R result_type;
    struct Call : detail::AbstractHandlerCallBase
        { virtual R operator() (P1, P2) = 0; };

    template <typename H>
    struct CallType : public Call
    {
        typedef typename A:: template rebind <CallType <H> >::other Allocator;
        CallType (BEAST_MOVE_ARG(H) h, A a = A ())
            : m_h (BEAST_MOVE_CAST(H)(h)), m_alloc (a)
            { }
        R operator()(P1 p1, P2 p2)
            { return (m_h)(p1, p2); }
        R operator()(P1 p1, P2 p2) const
            { return (m_h)(p1, p2); }
        void* allocate (std::size_t size)
            { return boost_asio_handler_alloc_helpers::allocate(size, m_h); }
        void deallocate (void* pointer, std::size_t size)
            { boost_asio_handler_alloc_helpers::deallocate( pointer, size, m_h); }
        bool is_continuation ()
#if BEAST_ASIO_HAS_CONTINUATION_HOOKS
            { return boost_asio_handler_cont_helpers::is_continuation(m_h); }
#else
            { return false; }
#endif
        void invoke (typename detail::AbstractHandlerCallBase::invoked_type& invoked)
            { boost_asio_handler_invoke_helpers::invoke <
                typename detail::AbstractHandlerCallBase::invoked_type, H> (invoked, m_h); }
    private:
        H m_h;
        Allocator m_alloc;
    };

    template <typename H>
    AbstractHandler (BEAST_MOVE_ARG(H) h, A a = A ())
        : m_call (new (
            typename A:: template rebind <CallType <H> >::other (a)
                .allocate (1)) CallType <H> (BEAST_MOVE_CAST(H)(h), a))
        { }
    R operator() (P1 p1, P2 p2)
        { return (*m_call)(p1, p2); }
    R operator() (P1 p1, P2 p2) const
        { return (*m_call)(p1, p2); }
    void* allocate (std::size_t size) const { return m_call->allocate(size); }
    void deallocate (void* pointer, std::size_t size) const { m_call->deallocate(pointer,size); }
    bool is_continuation () const { return m_call->is_continuation(); }
    template <typename Function>
    void invoke (Function& function) const
    {
        m_call->invoke(function);
    }
    template <typename Function>
    void invoke (Function const& function) const
    {
        m_call->invoke(function);
    }

private:
    SharedPtr <Call> m_call;
};

template <class R, class P1, class P2, class A>
void* asio_handler_allocate (std::size_t size,
    AbstractHandler <R (P1, P2), A>* handler)
{
    return handler->allocate (size);
}

template <class R, class P1, class P2, class A>
void asio_handler_deallocate (void* pointer, std::size_t size,
    AbstractHandler <R (P1, P2), A>* handler)
{
    handler->deallocate (pointer, size);
}

template <class R, class P1, class P2, class A>
bool asio_handler_is_continuation(
    AbstractHandler <R (P1, P2), A>* handler)
{
    return handler->is_continuation();
}

template <typename Function, class R, class P1, class P2, class A>
void asio_handler_invoke (BEAST_MOVE_ARG(Function) function,
    AbstractHandler <R (P1, P2), A>* handler)
{
    handler->invoke (BEAST_MOVE_CAST(Function)(function));
}

//------------------------------------------------------------------------------

// arity 3
template <class R, class P1, class P2, class P3, class A>
struct AbstractHandler <R (P1, P2, P3), A>
{
    typedef R result_type;
    struct Call : detail::AbstractHandlerCallBase
        { virtual R operator() (P1, P2, P3) = 0; };

    template <typename H>
    struct CallType : public Call
    {
        typedef typename A:: template rebind <CallType <H> >::other Allocator;
        CallType (BEAST_MOVE_ARG(H) h, A a = A ())
            : m_h (BEAST_MOVE_CAST(H)(h)), m_alloc (a)
            { }
        R operator()(P1 p1, P2 p2, P3 p3)
            { return (m_h)(p1, p2, p3); }
        R operator()(P1 p1, P2 p2, P3 p3) const
            { return (m_h)(p1, p2, p3); }
        void* allocate (std::size_t size)
            { return boost_asio_handler_alloc_helpers::allocate(size, m_h); }
        void deallocate (void* pointer, std::size_t size)
            { boost_asio_handler_alloc_helpers::deallocate( pointer, size, m_h); }
        bool is_continuation ()
#if BEAST_ASIO_HAS_CONTINUATION_HOOKS
            { return boost_asio_handler_cont_helpers::is_continuation(m_h); }
#else
            { return false; }
#endif
        void invoke (typename detail::AbstractHandlerCallBase::invoked_type& invoked)
            { boost_asio_handler_invoke_helpers::invoke <
                typename detail::AbstractHandlerCallBase::invoked_type, H> (invoked, m_h); }
    private:
        H m_h;
        Allocator m_alloc;
    };

    template <typename H>
    AbstractHandler (BEAST_MOVE_ARG(H) h, A a = A ())
        : m_call (new (
            typename A:: template rebind <CallType <H> >::other (a)
                .allocate (1)) CallType <H> (BEAST_MOVE_CAST(H)(h), a))
        { }
    R operator() (P1 p1, P2 p2, P3 p3)
        { return (*m_call)(p1, p2, p3); }
    R operator() (P1 p1, P2 p2, P3 p3) const
        { return (*m_call)(p1, p2, p3); }
    void* allocate (std::size_t size) const { return m_call->allocate(size); }
    void deallocate (void* pointer, std::size_t size) const { m_call->deallocate(pointer,size); }
    bool is_continuation () const { return m_call->is_continuation(); }
    template <typename Function>
    void invoke (Function& function) const
    {
        m_call->invoke(function);
    }
    template <typename Function>
    void invoke (Function const& function) const
    {
        m_call->invoke(function);
    }

private:
    SharedPtr <Call> m_call;
};

template <class R, class P1, class P2, class P3, class A>
void* asio_handler_allocate (std::size_t size,
    AbstractHandler <R (P1, P2, P3), A>* handler)
{
    return handler->allocate (size);
}

template <class R, class P1, class P2, class P3, class A>
void asio_handler_deallocate (void* pointer, std::size_t size,
    AbstractHandler <R (P1, P2, P3), A>* handler)
{
    handler->deallocate (pointer, size);
}

template <class R, class P1, class P2, class P3, class A>
bool asio_handler_is_continuation(
    AbstractHandler <R (P1, P2, P3), A>* handler)
{
    return handler->is_continuation();
}

template <typename Function, class R, class P1, class P2, class P3, class A>
void asio_handler_invoke (BEAST_MOVE_ARG(Function) function,
    AbstractHandler <R (P1, P2, P3), A>* handler)
{
    handler->invoke (BEAST_MOVE_CAST(Function)(function));
}

//------------------------------------------------------------------------------

// arity 4
template <class R, class P1, class P2, class P3, class P4, class A>
struct AbstractHandler <R (P1, P2, P3, P4), A>
{
    typedef R result_type;
    struct Call : detail::AbstractHandlerCallBase
        { virtual R operator() (P1, P2, P3, P4) = 0; };

    template <typename H>
    struct CallType : public Call
    {
        typedef typename A:: template rebind <CallType <H> >::other Allocator;
        CallType (BEAST_MOVE_ARG(H) h, A a = A ())
            : m_h (BEAST_MOVE_CAST(H)(h)), m_alloc (a)
            { }
        R operator()(P1 p1, P2 p2, P3 p3, P4 p4)
            { return (m_h)(p1, p2, p3, p4); }
        R operator()(P1 p1, P2 p2, P3 p3, P4 p4) const
            { return (m_h)(p1, p2, p3, p4); }
        void* allocate (std::size_t size)
            { return boost_asio_handler_alloc_helpers::allocate(size, m_h); }
        void deallocate (void* pointer, std::size_t size)
            { boost_asio_handler_alloc_helpers::deallocate( pointer, size, m_h); }
        bool is_continuation ()
#if BEAST_ASIO_HAS_CONTINUATION_HOOKS
            { return boost_asio_handler_cont_helpers::is_continuation(m_h); }
#else
            { return false; }
#endif
        void invoke (typename detail::AbstractHandlerCallBase::invoked_type& invoked)
            { boost_asio_handler_invoke_helpers::invoke <
                typename detail::AbstractHandlerCallBase::invoked_type, H> (invoked, m_h); }
    private:
        H m_h;
        Allocator m_alloc;
    };

    template <typename H>
    AbstractHandler (BEAST_MOVE_ARG(H) h, A a = A ())
        : m_call (new (
            typename A:: template rebind <CallType <H> >::other (a)
                .allocate (1)) CallType <H> (BEAST_MOVE_CAST(H)(h), a))
        { }
    R operator() (P1 p1, P2 p2, P3 p3, P4 p4)
        { return (*m_call)(p1, p2, p3, p4); }
    R operator() (P1 p1, P2 p2, P3 p3, P4 p4) const
        { return (*m_call)(p1, p2, p3, p4); }
    void* allocate (std::size_t size) const { return m_call->allocate(size); }
    void deallocate (void* pointer, std::size_t size) const { m_call->deallocate(pointer,size); }
    bool is_continuation () const { return m_call->is_continuation(); }
    template <typename Function>
    void invoke (Function& function) const
    {
        m_call->invoke(function);
    }
    template <typename Function>
    void invoke (Function const& function) const
    {
        m_call->invoke(function);
    }

private:
    SharedPtr <Call> m_call;
};

template <class R, class P1, class P2, class P3, class P4, class A>
void* asio_handler_allocate (std::size_t size,
    AbstractHandler <R (P1, P2, P3, P4), A>* handler)
{
    return handler->allocate (size);
}

template <class R, class P1, class P2, class P3, class P4, class A>
void asio_handler_deallocate (void* pointer, std::size_t size,
    AbstractHandler <R (P1, P2, P3, P4), A>* handler)
{
    handler->deallocate (pointer, size);
}

template <class R, class P1, class P2, class P3, class P4, class A>
bool asio_handler_is_continuation(
    AbstractHandler <R (P1, P2, P3, P4), A>* handler)
{
    return handler->is_continuation();
}

template <typename Function, class R, class P1, class P2, class P3, class P4, class A>
void asio_handler_invoke (BEAST_MOVE_ARG(Function) function,
    AbstractHandler <R (P1, P2, P3, P4), A>* handler)
{
    handler->invoke (BEAST_MOVE_CAST(Function)(function));
}

//------------------------------------------------------------------------------

// arity 5
template <class R, class P1, class P2, class P3, class P4, class P5, class A>
struct AbstractHandler <R (P1, P2, P3, P4, P5), A>
{
    typedef R result_type;
    struct Call : detail::AbstractHandlerCallBase
        { virtual R operator() (P1, P2, P3, P4, P5) = 0; };

    template <typename H>
    struct CallType : public Call
    {
        typedef typename A:: template rebind <CallType <H> >::other Allocator;
        CallType (BEAST_MOVE_ARG(H) h, A a = A ())
            : m_h (BEAST_MOVE_CAST(H)(h)), m_alloc (a)
            { }
        R operator()(P1 p1, P2 p2, P3 p3, P4 p4, P5 p5)
            { return (m_h)(p1, p2, p3, p4, p5); }
        R operator()(P1 p1, P2 p2, P3 p3, P4 p4, P5 p5) const
            { return (m_h)(p1, p2, p3, p4, p5); }
        void* allocate (std::size_t size)
            { return boost_asio_handler_alloc_helpers::allocate(size, m_h); }
        void deallocate (void* pointer, std::size_t size)
            { boost_asio_handler_alloc_helpers::deallocate( pointer, size, m_h); }
        bool is_continuation ()
#if BEAST_ASIO_HAS_CONTINUATION_HOOKS
            { return boost_asio_handler_cont_helpers::is_continuation(m_h); }
#else
            { return false; }
#endif
        void invoke (typename detail::AbstractHandlerCallBase::invoked_type& invoked)
            { boost_asio_handler_invoke_helpers::invoke <
                typename detail::AbstractHandlerCallBase::invoked_type, H> (invoked, m_h); }
    private:
        H m_h;
        Allocator m_alloc;
    };

    template <typename H>
    AbstractHandler (BEAST_MOVE_ARG(H) h, A a = A ())
        : m_call (new (
            typename A:: template rebind <CallType <H> >::other (a)
                .allocate (1)) CallType <H> (BEAST_MOVE_CAST(H)(h), a))
        { }
    R operator() (P1 p1, P2 p2, P3 p3, P4 p4, P5 p5)
        { return (*m_call)(p1, p2, p3, p4, p5); }
    R operator() (P1 p1, P2 p2, P3 p3, P4 p4, P5 p5) const
        { return (*m_call)(p1, p2, p3, p4, p5); }
    void* allocate (std::size_t size) const { return m_call->allocate(size); }
    void deallocate (void* pointer, std::size_t size) const { m_call->deallocate(pointer,size); }
    bool is_continuation () const { return m_call->is_continuation(); }
    template <typename Function>
    void invoke (Function& function) const
    {
        m_call->invoke(function);
    }
    template <typename Function>
    void invoke (Function const& function) const
    {
        m_call->invoke(function);
    }

private:
    SharedPtr <Call> m_call;
};

template <class R, class P1, class P2, class P3, class P4, class P5, class A>
void* asio_handler_allocate (std::size_t size,
    AbstractHandler <R (P1, P2, P3, P4, P5), A>* handler)
{
    return handler->allocate (size);
}

template <class R, class P1, class P2, class P3, class P4, class P5, class A>
void asio_handler_deallocate (void* pointer, std::size_t size,
    AbstractHandler <R (P1, P2, P3, P4, P5), A>* handler)
{
    handler->deallocate (pointer, size);
}

template <class R, class P1, class P2, class P3, class P4, class P5, class A>
bool asio_handler_is_continuation(
    AbstractHandler <R (P1, P2, P3, P4, P5), A>* handler)
{
    return handler->is_continuation();
}

template <typename Function, class R, class P1, class P2, class P3, class P4, class P5, class A>
void asio_handler_invoke (BEAST_MOVE_ARG(Function) function,
    AbstractHandler <R (P1, P2, P3, P4, P5), A>* handler)
{
    handler->invoke (BEAST_MOVE_CAST(Function)(function));
}

}

#endif
