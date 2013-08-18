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

#ifndef BEAST_HANDLERCALL_H_INCLUDED
#define BEAST_HANDLERCALL_H_INCLUDED

/** A polymorphic Handler that can wrap any other handler.

    This is very lightweight container that just holds a shared pointer
    to the actual handler. This means it can be copied cheaply. The
    allocation and deallocation of the handler is performed according
    to the requirements of asio_handler_allocate and asio_handler_delete.
    All calls also satisfy the safety guarantees of asio_handler_invoke.

    All constructors will take ownership of the passed in handler
    if your compiler has MoveConstructible and MoveAssignable support enabled.

    Supports these concepts:
        DefaultConstructible
        MoveConstructible (C++11)
        CopyConstructible
        MoveAssignable (C++11)
        CopyAssignable
        Destructible
*/
class HandlerCall
{
private:
    typedef boost::system::error_code error_code;

public:
    typedef void result_type;

    // Really there are only 3 kings of functions.
    // Except for the composed connect, which we haven't done yet.
    //
    struct Post { };        // void()
    struct Error { };       // void(error_code)
    struct Transfer { };    // void(error_code, std::size_t)

    // These tags tell us what kind of Handler we have.
    // It would be nice if we could deduce this, but the
    // return value of a bind() seems to satisfy the
    // requirements of ANY handler so that was scrapped.

    // CompletionHandler
    //
    // http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/CompletionHandler.html
    //
    typedef Post Completion;

    // AcceptHandler
    // http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/AcceptHandler.html
    //
    typedef Error Accept;

    // ConnectHandler
    // http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/ConnectHandler.html
    //
    typedef Error Connect;

    // ShutdownHandler
    // http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/ShutdownHandler.html
    //
    typedef Error Shutdown;

    // HandshakeHandler
    // http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/HandshakeHandler.html
    //
    typedef Error Handshake;

    // ReadHandler
    // http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/ReadHandler.html
    //
    typedef Transfer Read;

    // WriteHandler
    // http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/WriteHandler.html
    //
    typedef Transfer Write;

    // BufferedHandshakeHandler
    // http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/BufferedHandshakeHandler.html
    //
    typedef Transfer BufferedHandshake;

    //--------------------------------------------------------------------------

    HandlerCall () noexcept
    {
    }

    template <typename Handler>
    HandlerCall (BOOST_ASIO_MOVE_ARG(Handler) handler, Completion)
        : m_call (construct <PostCallType> (
            BOOST_ASIO_MOVE_CAST(Handler)(handler)))
    {
    }

    template <typename Handler, typename Arg1>
    HandlerCall (BOOST_ASIO_MOVE_ARG(Handler) handler, Arg1 arg1, Completion)
        : m_call (construct <PostCallType1> (
            BOOST_ASIO_MOVE_CAST(Handler)(handler), arg1))
    {
    }

    template <typename Handler, typename Arg1, typename Arg2>
    HandlerCall (BOOST_ASIO_MOVE_ARG(Handler) handler, Arg1 arg1, Arg2 arg2, Completion)
        : m_call (construct <PostCallType2> (
            BOOST_ASIO_MOVE_CAST(Handler)(handler), arg1, arg2))
    {
    }

    template <typename Handler>
    HandlerCall (BOOST_ASIO_MOVE_ARG(Handler) handler, Error)
        : m_call (construct <ErrorCallType> (
            BOOST_ASIO_MOVE_CAST(Handler)(handler)))

    {
    }

    template <typename Handler>
    HandlerCall (BOOST_ASIO_MOVE_ARG(Handler) handler, Transfer)
        : m_call (construct <TransferCallType> (
            BOOST_ASIO_MOVE_CAST(Handler)(handler)))
    {
    }

    inline HandlerCall (HandlerCall const& other) noexcept
        : m_call (other.m_call)
    { 
    }

    inline HandlerCall& operator= (HandlerCall const& other) noexcept
    {
        m_call = other.m_call;
        return *this;
    }

#if BEAST_COMPILER_SUPPORTS_MOVE_SEMANTICS
    inline HandlerCall (HandlerCall && other) noexcept
        : m_call (other.m_call)
    {
        other.m_call = nullptr;
    }

    inline HandlerCall& operator= (HandlerCall&& other) noexcept
    {
        m_call = other.m_call;
        other.m_call = nullptr;
        return *this;
    }
#endif

    inline bool isNull () const noexcept
    {
        return m_call == nullptr;
    }

    inline bool isNotNull () const noexcept
    {
        return m_call != nullptr;
    }

    inline void operator() ()
    {
        (*m_call)();
    }

    inline void operator() (error_code const& ec)
    {
        (*m_call)(ec);
    }

    inline void operator() (error_code const& ec, std::size_t bytes_transferred)
    {
        (*m_call)(ec, bytes_transferred);
    }

private:

    //--------------------------------------------------------------------------
    //
    // Implementation
    //
    //--------------------------------------------------------------------------

    struct Call;

    // These construct the reference counted polymorphic wrapper (Call)
    // around the handler. We use MoveAssignable (rvalue-references)
    // assignments to take ownership of the object and bring it to our stack.
    // From there, we use the context's hooked allocation function to create
    // a single piece of memory to hold our wrapper. Then we use placement
    // new to construct the wrapper. The wrapper uses rvalue assignment
    // to take ownership of the handler from the stack.

    template <template <typename> class Container, typename Handler>
    static Call* construct (BOOST_ASIO_MOVE_ARG(Handler) handler)
    {
        typedef Container <Handler> ContainerType;
        Handler local (BOOST_ASIO_MOVE_CAST(Handler)(handler)); // move to stack
        std::size_t const size (sizeof (ContainerType));
        void* const p = boost_asio_handler_alloc_helpers::
            allocate <Handler> (size, local);
        return ::new (p) ContainerType (
            BOOST_ASIO_MOVE_CAST(Handler)(local), size);
    }

    template <template <typename, typename> class Container,
        typename Handler, typename Arg1>
    static Call* construct (BOOST_ASIO_MOVE_ARG(Handler) handler, Arg1 arg1)
    {
        typedef Container <Handler, Arg1> ContainerType;
        Handler local (BOOST_ASIO_MOVE_CAST(Handler)(handler)); // move to stack
        std::size_t const size (sizeof (ContainerType));
        void* const p = boost_asio_handler_alloc_helpers::
            allocate <Handler> (size, local);
        return ::new (p) ContainerType (
            BOOST_ASIO_MOVE_CAST(Handler)(local), size, arg1);
    }

    template <template <typename, typename, typename> class Container,
        typename Handler, typename Arg1, typename Arg2>
    static Call* construct (BOOST_ASIO_MOVE_ARG(Handler) handler, Arg1 arg1, Arg2 arg2)
    {
        typedef Container <Handler, Arg1, Arg2> ContainerType;
        Handler local (BOOST_ASIO_MOVE_CAST(Handler)(handler)); // move to stack
        std::size_t const size (sizeof (ContainerType));
        void* const p = boost_asio_handler_alloc_helpers::
            allocate <Handler> (size, local);
        return ::new (p) ContainerType (
            BOOST_ASIO_MOVE_CAST(Handler)(local), size, arg1, arg2);
    }

    inline void* allocate (std::size_t size)
    {
        return m_call->allocate (size);
    }

    inline void deallocate (void* p, std::size_t size)
    {
        m_call->deallocate (p, size);
    }

    inline void destroy ()
    {
        m_call->destroy ();
    }

    //--------------------------------------------------------------------------

    // Custom Allocator compatible with std::allocator and boost::function
    // which uses the underlying context to allocate memory. This is
    // vastly more efficient in a variety of situations especially during
    // an upcall.
    //
    template <typename T>
    struct Allocator
    {
        typedef T value_type;
        typedef T* pointer;
        typedef T& reference;
        typedef T const* const_pointer;
        typedef T const& const_reference;
        typedef std::size_t size_type;
        typedef std::ptrdiff_t difference_type;

        explicit Allocator (SharedObjectPtr <Call> const& call)
            : m_call (call)
        {
        }

        template <typename U>
        Allocator (Allocator <U> const& other)
            : m_call (other.m_call)
        {
        }

        template <typename U>
        struct rebind
        {
            typedef Allocator <U> other;
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
            return static_cast <pointer> (m_call->allocate (bytes));
        }

        void deallocate (pointer p, size_type n) const
        {
            size_type const bytes = n * sizeof (value_type);
            m_call->deallocate (p, bytes);
        }

        size_type max_size () const noexcept
        {
            return std::numeric_limits <size_type>::max () / sizeof (value_type);
        }

        void construct (pointer p, const_reference val) const
        {
            ::new ((void *)p) value_type (val);
        }

        void destroy (pointer p) const
        {
            p->~value_type ();
        }

    private:
        template <class>
        friend struct Allocator;

        // The wrapped handler is stored in a reference counted
        // container, so copies and pass by value is very cheap.
        //
        SharedObjectPtr <Call> m_call;
    };

    //--------------------------------------------------------------------------

    // We use a boost::function to hold the Function from asio_handler_invoke.
    //
    typedef boost::function <void(void)> invoked_type;

    // Invoke the specified Function object. The boost::function object is
    // created partially on the stack, using a custom Allocator which calls
    // into the handler's context to perform allocation and deallocation.
    // We take ownership of the passed Function object, and then ownership
    //
    // All of the safety guarantees of the original context are preserved
    // when going through this invoke function.
    //
    template <typename Function>
    void invoke (BOOST_ASIO_MOVE_ARG(Function) f)
    {
        invoked_type invoked (BOOST_ASIO_MOVE_CAST(Function)(f),
            Allocator <invoked_type> (m_call));
        m_call->invoke (invoked);
    }

    //--------------------------------------------------------------------------
    //
    // Abstract handler wrapper
    //
    struct Call : public SharedObject
    {
        Call ()
        {
        }

        ~Call ()
        {

        }

        virtual void operator() ()
        {
            pure_virtual_called ();
        }

        virtual void operator() (error_code const&)
        {
            pure_virtual_called ();
        }

        virtual void operator() (error_code const&, std::size_t)
        {
            pure_virtual_called ();
        }

        virtual void* allocate (std::size_t) = 0;

        virtual void  deallocate (void*, std::size_t) = 0;

        virtual void  invoke (invoked_type&) = 0;

        virtual void destroy () = 0;

        static void* pure_virtual_called ()
        {
            // These shouldn't be getting called. But since the object returned
            // by most implementations of bind have operator() up to high arity
            // levels, it is not generally possible to write a traits test that
            // works in all scenarios for detecting a particular signature of a
            // handler.
            //
            fatal_error ("pure virtual called");
            return nullptr;
        }
    };

    // Required for gaining access to our hooks
    //
    friend struct ContainerDeletePolicy <Call>;

    //--------------------------------------------------------------------------

    // Holds the original handler with all of its type information.
    // Can also perform the invoke, allocate, and deallocate operations
    // in the same context as the original handler.
    //
    template <typename Handler>
    struct CallType : Call
    {
        // We take ownership of the handler here, through move assignment.
        // The size parameter corresponds to how much we allocated using
        // the custom allocator, and is required for deallocation.
        //
        CallType (BOOST_ASIO_MOVE_ARG(Handler) handler, std::size_t size)
            : m_handler (BOOST_ASIO_MOVE_CAST(Handler)(handler))
            , m_size (size)
        {
        }

        ~CallType ()
        {

        }

        // Allocate using the handler's context.
        //
        void* allocate (std::size_t bytes)
        {
            return boost_asio_handler_alloc_helpers::
                allocate <Handler> (bytes, m_handler);
        }

        // Deallocate using the handlers context.
        // Note that the original size is required.
        //
        void deallocate (void* p, std::size_t size)
        {
            boost_asio_handler_alloc_helpers::
                deallocate <Handler> (p, size, m_handler);
        }

        // Invoke the specified function on the handlers context.
        // invoked has this signature void(void).
        //
        void invoke (invoked_type& invoked)
        {
            boost_asio_handler_invoke_helpers::
                invoke <invoked_type, Handler> (invoked, m_handler);
        }

        // Called by our ContainerDeletePolicy hook to destroy the
        // object. We need this because we allocated it using a custom
        // allocator. Destruction is tricky, the algorithm is as follows:
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
        void destroy ()
        {
            // Move the handler onto our stack so we have
            // a context in which to perform the deallcation.
            Handler local (BOOST_ASIO_MOVE_CAST(Handler)(m_handler));
            std::size_t const size (m_size); // save the size member
            Call* const call (dynamic_cast <Call*>(this));
            call->~Call ();
            boost_asio_handler_alloc_helpers::
                deallocate <Handler> (call, size, local);
        }

        // These better not be getting called since we use our own allocators.
        //
        // NOTE Couldn't make them private since GCC complained.
        //
        void* operator new (std::size_t)
        {
            return pure_virtual_called ();
        }

        void operator delete (void*)
        {
            pure_virtual_called ();
        }

    protected:
        std::size_t const m_size;
        Handler m_handler;
    };

    //--------------------------------------------------------------------------

    template <typename Handler>
    struct PostCallType : CallType <Handler>
    {
        PostCallType (BOOST_ASIO_MOVE_ARG(Handler) handler, std::size_t size)
            : CallType <Handler> (BOOST_ASIO_MOVE_CAST(Handler)(handler), size)
        {
        }

        void operator() ()
        {
            this->m_handler ();
        }
    };

    template <typename Handler, typename Arg1>
    struct PostCallType1 : CallType <Handler>
    {
        PostCallType1 (BOOST_ASIO_MOVE_ARG(Handler) handler, std::size_t size, Arg1 arg1)
            : CallType <Handler> (BOOST_ASIO_MOVE_CAST(Handler)(handler), size)
            , m_arg1 (arg1)
        {
        }

        void operator() ()
        {
            this->m_handler (m_arg1);
        }

        Arg1 m_arg1;
    };

    template <typename Handler, typename Arg1, typename Arg2>
    struct PostCallType2 : CallType <Handler>
    {
        PostCallType2 (BOOST_ASIO_MOVE_ARG(Handler) handler, std::size_t size, Arg1 arg1, Arg2 arg2)
            : CallType <Handler> (BOOST_ASIO_MOVE_CAST(Handler)(handler), size)
            , m_arg1 (arg1)
            , m_arg2 (arg2)
        {
        }

        void operator() ()
        {
            this->m_handler (m_arg1, m_arg2);
        }

        Arg1 m_arg1;
        Arg2 m_arg2;
    };

    template <typename Handler>
    struct ErrorCallType : CallType <Handler>
    {
        ErrorCallType (BOOST_ASIO_MOVE_ARG(Handler) handler, std::size_t size)
            : CallType <Handler> (BOOST_ASIO_MOVE_CAST(Handler)(handler), size)
        {
        }

        void operator() (error_code const& ec)
        {
            this->m_handler (ec);
        }
    };

    template <typename Handler>
    struct TransferCallType : CallType <Handler>
    {
        TransferCallType (BOOST_ASIO_MOVE_ARG(Handler) handler, std::size_t size)
            : CallType <Handler> (BOOST_ASIO_MOVE_CAST(Handler)(handler), size)
        {
        }

        void operator() (error_code const& ec, std::size_t bytes_transferred)
        {
            this->m_handler (ec, bytes_transferred);
        }
    };

private:
    template <class Function>
    friend void  asio_handler_invoke (BOOST_ASIO_MOVE_ARG(Function), HandlerCall*);
    friend void* asio_handler_allocate (std::size_t, HandlerCall*);
    friend void  asio_handler_deallocate (void*, std::size_t, HandlerCall*);

    SharedObjectPtr <Call> m_call;
};

//------------------------------------------------------------------------------

class CompletionCall : public HandlerCall
{
public:
    CompletionCall ()
    {
    }

    template <typename Handler>
    explicit CompletionCall (BOOST_ASIO_MOVE_ARG(Handler) handler)
        : HandlerCall (BOOST_ASIO_MOVE_CAST(Handler)(handler), HandlerCall::Post ())
    {
    }

    template <typename Handler, typename Arg1>
    explicit CompletionCall (BOOST_ASIO_MOVE_ARG(Handler) handler, Arg1 arg1)
        : HandlerCall (BOOST_ASIO_MOVE_CAST(Handler)(handler), arg1, HandlerCall::Post ())
    {
    }
    template <typename Handler, typename Arg1, typename Arg2>
    explicit CompletionCall (BOOST_ASIO_MOVE_ARG(Handler) handler, Arg1 arg1, Arg2 arg2)
        : HandlerCall (BOOST_ASIO_MOVE_CAST(Handler)(handler), arg1, arg2, HandlerCall::Post ())
    {
    }
};

//------------------------------------------------------------------------------

class ErrorCall : public HandlerCall
{
public:
    ErrorCall ()
    {
    }

    template <typename Handler>
    explicit ErrorCall (BOOST_ASIO_MOVE_ARG(Handler) handler)
        : HandlerCall (BOOST_ASIO_MOVE_CAST(Handler)(handler), HandlerCall::Error ())
    {
    }
};

//------------------------------------------------------------------------------

class TransferCall : public HandlerCall
{
public:
    TransferCall ()
    {
    }

    template <typename Handler>
    explicit TransferCall (BOOST_ASIO_MOVE_ARG(Handler) handler)
        : HandlerCall (BOOST_ASIO_MOVE_CAST(Handler)(handler), HandlerCall::Transfer ())
    {
    }
};

//------------------------------------------------------------------------------
//
// Specializations
//
//  asio_handler_invoke, asio_handler_allocate, asio_handler_deallocate
//

template <class Function>
void asio_handler_invoke (BOOST_ASIO_MOVE_ARG(Function) f, HandlerCall* call)
{
    call->invoke (BOOST_ASIO_MOVE_CAST(Function)(f));
}

inline void* asio_handler_allocate (std::size_t size, HandlerCall* call)
{
    return call->allocate (size);
}

inline void asio_handler_deallocate (void* p, std::size_t size, HandlerCall* call)
{
    call->deallocate (p, size);
}

template <>
struct ContainerDeletePolicy <HandlerCall::Call>
{
    static void destroy (HandlerCall::Call* call)
    {
        call->destroy ();
    }
};

#endif
