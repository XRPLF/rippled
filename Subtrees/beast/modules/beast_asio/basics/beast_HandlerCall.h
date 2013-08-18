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
        CopyConstructible
        CopyAssignable
        Destructible
*/
class HandlerCall
{
private:
    typedef boost::system::error_code error_code;
    typedef boost::function <void(void)> invoked_type;

    // Forward declarations needed for friendship
    template <typename>
    struct CallType;
    struct Call;

public:
    typedef void result_type;

    struct Context;

    //--------------------------------------------------------------------------

    /** HandlerCall construction tags.

        These tags are used at the end of HandlerCall constructor parameter
        lists to tell it what kind of Handler you are passing. For example:

        @code

        struct MyClass
        {
            void on_connect (error_code const& ec);

            void connect (Address address)
            {
                HandlerCall myHandler (
                    bind (&MyClass::foo, this),
                        Connect ());

                socket.async_connect (address, myHandler);
            }
        };

        @endcode

        It would be nice if we could deduce the type of handler from the template
        argument alone, but the return value of most implementations of bind seem
        to satisfy the traits of ANY handler so that was scrapped.
    */        
    /** @{ */
    // These are the three basic supported function signatures
    //
    // ComposedConnectHandler is to-do
    //
    struct Post { };        // void()
    struct Error { };       // void(error_code)
    struct Transfer { };    // void(error_code, std::size_t)

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
    /** @} */

    //--------------------------------------------------------------------------

    /** Construct a null HandlerCall.
        
        A default constructed handler has no associated call. Passing
        it as a handler to an asynchronous operation will result in
        undefined behavior. This constructor exists so that you can do
        things like have a class member "originalHandler" which starts
        out as null, and then later assign it. Routines are provided to
        test the handler against null.
    */
    HandlerCall () noexcept;

    /** Construct a HandlerCall.

        Handler must meet this requirement:
            CompletionHandler

        HandlerCall will meet this requirement:
            CompletionHandler
    */
    /** @{ */
    template <typename Handler>
    HandlerCall (Post, BOOST_ASIO_MOVE_ARG(Handler) handler)
        : m_call (construct <PostCallType> (Context (),
            BOOST_ASIO_MOVE_CAST(Handler)(handler)))
    {
    }

    template <typename Handler>
    HandlerCall (Post, Context context, BOOST_ASIO_MOVE_ARG(Handler) handler)
        : m_call (construct <PostCallType> (context,
            BOOST_ASIO_MOVE_CAST(Handler)(handler)))
    {
    }
    /** @} */

    /** Construct a HandlerCall with one bound parameter.

        Produce a CompletionHandler that includes one bound parameter.
        This can be useful if you want to call io_service::post() on
        the handler and you need to give it an already existing value,
        like an error_code.

        Invoking operator() on the HandlerCall will be the same as:

        @code

        handler (arg1);

        @endcode

        Handler must meet one of these requirements:
            AcceptHandler
            ConnectHandler
            ShutdownHandler
            HandshakeHandler

        HandlerCall will meet this requirement:
            CompletionHandler
    */
    /** @{ */
    template <typename Handler, typename Arg1>
    HandlerCall (Post, BOOST_ASIO_MOVE_ARG(Handler) handler, Arg1 arg1)
        : m_call (construct <PostCallType1> (Context (),
            BOOST_ASIO_MOVE_CAST(Handler)(handler), arg1))
    {
    }

    template <typename Handler, typename Arg1>
    HandlerCall (Post, Context context, BOOST_ASIO_MOVE_ARG(Handler) handler, Arg1 arg1)
        : m_call (construct <PostCallType1> (context,
            BOOST_ASIO_MOVE_CAST(Handler)(handler), arg1))
    {
    }
    /** @} */

    /** Construct a HandlerCall with two bound parameters.

        Produce a CompletionHandler that includes two bound parameters.
        This can be useful if you want to call io_service::post() on
        the handler and you need to give it two already existing values,
        like an error_code and bytes_transferred.

        Invoking operator() on the HandlerCall will be the same as:

        @code

        handler (arg1, arg2);

        @endcode

        Handler must meet one of these requirements:
            ReadHandler
            WriteHandler
            BufferedHandshakeHandler

        The HandlerCall will meet these requirements:
            CompletionHandler
    */
    /** @{ */
    template <typename Handler, typename Arg1, typename Arg2>
    HandlerCall (Post, BOOST_ASIO_MOVE_ARG(Handler) handler, Arg1 arg1, Arg2 arg2)
        : m_call (construct <PostCallType2> (Context (),
            BOOST_ASIO_MOVE_CAST(Handler)(handler), arg1, arg2))
    {
    }

    template <typename Handler, typename Arg1, typename Arg2>
    HandlerCall (Post, Context context, BOOST_ASIO_MOVE_ARG(Handler) handler, Arg1 arg1, Arg2 arg2)
        : m_call (construct <PostCallType2> (context,
            BOOST_ASIO_MOVE_CAST(Handler)(handler), arg1, arg2))
    {
    }
    /** @} */

    /** Construct a HandlerCall from a handler that takes an error_code

        Handler must meet one of these requirements:
            AcceptHandler
            ConnectHandler
            ShutdownHandler
            HandshakeHandler

        The HandlerCall will meet these requirements:
            AcceptHandler
            ConnectHandler
            ShutdownHandler
            HandshakeHandler
    */
    /** @{ */
    template <typename Handler>
    HandlerCall (Error, BOOST_ASIO_MOVE_ARG(Handler) handler)
        : m_call (construct <ErrorCallType> (Context (),
            BOOST_ASIO_MOVE_CAST(Handler)(handler)))
    {
    }

    template <typename Handler>
    HandlerCall (Error, Context context, BOOST_ASIO_MOVE_ARG(Handler) handler)
        : m_call (construct <ErrorCallType> (context,
            BOOST_ASIO_MOVE_CAST(Handler)(handler)))
    {
    }
    /** @} */

    /** Construct a HandlerCall from a handler that takes an error_code and std::size

        Handler must meet one of these requirements:
            ReadHandler
            WriteHandler
            BufferedHandshakeHandler

        The HandlerCall will meet these requirements:
            ReadHandler
            WriteHandler
            BufferedHandshakeHandler
    */
    /** @{ */
    template <typename Handler>
    HandlerCall (Transfer, BOOST_ASIO_MOVE_ARG(Handler) handler)
        : m_call (construct <TransferCallType> (Context (),
            BOOST_ASIO_MOVE_CAST(Handler)(handler)))
    {
    }

    template <typename Handler>
    HandlerCall (Transfer, Context context, BOOST_ASIO_MOVE_ARG(Handler) handler)
        : m_call (construct <TransferCallType> (context,
            BOOST_ASIO_MOVE_CAST(Handler)(handler)))
    {
    }
    /** @} */

    /** Copy construction and assignment.

        HandlerCall is a very lightweight object that holds a shared
        pointer to the wrapped handler. It is cheap to copy and pass
        around.

        Construction and assignment from other HandlerCall objects
        executes in constant time, does not allocate or free memory,
        and invokes no methods calls on the wrapped handler, except
        for the case where the last reference on an existing handler
        is removed.
    */
    /** @{ */
    HandlerCall (HandlerCall const& other) noexcept;
    HandlerCall& operator= (HandlerCall const& other) noexcept;
    /** @} */

    //--------------------------------------------------------------------------

    /** Returns true if the HandlerCall is null.

        A null HandlerCall object may not be passed to
        an asynchronous completion function.
    */
    bool isNull () const noexcept;

    /** Returns true if the HandlerCall is not null. */
    bool isNotNull () const noexcept;

    /** Retrieve the context associated with a handler.

        The context will only be valid while the handler exists. In particular,
        if the handler is dispatched all references to its context will become
        invalid, and undefined behavior will result.

        The typical use case is to acquire the context from a caller provided
        completion handler, and use the context in a series of composed
        operations. At the conclusion of the composed operations, the original
        handler is invoked and the context is no longer needed.

        Various methods are provided for easily creating your own completion
        handlers that are associated with an existing context.
    */
    Context getContext () const noexcept;

    /** Determine if this handler is the final handler in a composed chain.
        A Context is usually shared during a composed operation.
        Normally you won't need to call this but it's useful for diagnostics.
        Really what this means it that the context is its own wrapped handler.
    */
    bool isFinal () const noexcept;

    /** Mark this handler as part of a composed operation.

        This is only valid to call if the handler is not already sharing
        the context of another handler (i.e. it is already part of a
        composed operation).

        Any handlers that share the same context will result in true being
        passed to the asio_is_continuation_hook. When you are ready to
        issue the final call to the original handler (which will also
        destroy the context), call endComposed on the original
        handler.

        To be clear, beginComposed and endComposed are called on the same
        HandlerCall object, and that object was not constructed from another
        context.

        @see isComposed, endComposed
    */
    HandlerCall const& beginComposed () const noexcept;

    /** Indicate the end of a composed operation.

        A composed operation starts with a handler that uses itself as its own
        context. The composed operation issues new asynchronous calls with its
        own callback, using the original handler's context. To optimize the
        strategy for calling completion handlers, call beginComposed.
    */
    HandlerCall const& endComposed () const noexcept;

    /** Invoke the wrapped handler.

        Normally you will not need to call this yourself. Since this is a
        polymorphic wrapper, any attempt to use an operator that doesn't
        correspond to the signature of the wrapped handler (taking into
        account arguments bound at construction), will result in a fatal
        error at run-time.
    */
    /** @{ */
    void operator() () const;
    void operator() (error_code const& ec) const;
    void operator() (error_code const& ec, std::size_t bytes_transferred) const;
    /** @} */

    //--------------------------------------------------------------------------

    /** The context of execution of a particular handler.

        When writing composed operations (a sequence of asynchronous function
        calls), it is important that the intermediate handlers run in the same
        context as the handler originally provided to signal the end of the
        composed operation.

        An object of this type abstracts the execution context of any handler.
        You can extract the context from an existing handler and associate
        new handlers you create with that context. This allows composed
        operations to be written easily, lifting the burden of meeting the
        composed operation requirements.

        In all cases, the Context will only be valid while the original handler
        exists. It is the caller's responsibility to manage the usage and
        lifetimes of these objects.

        Context objects are lightweight and just hold a reference to the
        underlying context. They are cheap to copy and pass around.
        
        Supports these concepts:
            DefaultConstructible
            CopyConstructible
            CopyAssignable
            Destructible
    */
    struct Context
    {
        /** Construct a null Context.
            When a null Context is specified as the Context to use when
            creating a HandlerCall, it means to use the wrapped handler
            as its own context. This is the default behavior.
        */
        Context () noexcept;

        /** Construct a Context from another Context. */
        Context (Context const& other) noexcept;

        /** Construct a Context from an existing handler's Context. */
        Context (HandlerCall const& handler) noexcept;

        /** Assign this Context from another Context. */
        Context& operator= (Context other) noexcept;

        /** Determine if this context is a composed asynchronous operation.

            When a handler begins a composed operation it becomes its own
            context (it is not constructed with a specified context). When
            a composed operation starts, this will return true for all
            handlers which share the context including the original handler.

            You have to indicate that a composed operation is starting by
            calling beginComposed on the original handler, performing
            your operations using its context, and then call endComposed
            before calling the original handler.

            @see beginComposed, endComposed
        */
        bool isComposed () const noexcept;

        /** Determine whether or not this Context is a null Context.
            Note that a non-null Context is no guarantee of
            the validity of the context.
        */
        /** @{ */
        bool isNull () const noexcept;
        bool isNotNull () const noexcept;
        /** @} */

        /** Compare two contexts.
            This determines if the two contexts refer to the same underlying
            completion handler and would have the same execution guarantees.
            The behavior is undefined if either of the contexts have been
            destroyed.
        */
        /** @{ */
        bool operator== (Context other) const noexcept;
        bool operator!= (Context other) const noexcept;
        /** @} */

        /** Allocate and deallocate memory.
            This takes into account any hooks installed by the context.
            Normally you wont need to call this unless you are writing your
            own asio_handler_invoke hook, or creating wrapped handlers.
        */
        /** @{ */
        void* allocate (std::size_t size) const;
        void  deallocate (void* p, std::size_t size) const;
        /** @} */

        /** Invoke the specified Function on the context.

            This is equivalent to the following:

            @code

            template <typename Function>
            void callOnContext (Function f, Context& context)
            {
                using boost_asio_handler_invoke_helpers;
                invoke <Function, Context> (f, context);
            }
                
            @endcode

            Usually you won't need to call this unless you
            are writing your own asio_handler_invoke hook.
        
            The boost::function object is created partially on the
            stack, using a custom Allocator which calls into the handler's
            context to perform allocation and deallocation. We take ownership
            of the passed Function object.

            All of the safety guarantees of the original context are preserved
            when going through this invoke function.
        */
        template <typename Function>
        void invoke (BOOST_ASIO_MOVE_ARG(Function) f)
        {
            invoked_type invoked (BOOST_ASIO_MOVE_CAST(Function)(f),
                Allocator <invoked_type> (*this));
            m_call->invoke (invoked);
        }

        bool operator== (Call const* call) const noexcept;
        bool operator!= (Call const* call) const noexcept;

    private:
        template <typename Handler>
        friend struct CallType;
        friend struct Call;

        Context (Call* call) noexcept;

        // Note that we only store a pointer here. If the original
        // Call is destroyed, the context will become invalid.
        //
        Call* m_call;
    };

private:
    //--------------------------------------------------------------------------
    //
    // Implementation
    //
    //--------------------------------------------------------------------------

    // These construct the Call, a reference counted polymorphic wrapper around
    // the handler and its context. We use MoveAssignable (rvalue-references)
    // assignments to take ownership of the object and bring it to our stack.
    // From there, we use the context's hooked allocation function to create a
    // single piece of memory to hold our wrapper. Then we use placement new to
    // construct the wrapper. The wrapper uses rvalue assignment to take
    // ownership of the handler from the stack.

    template <template <typename> class Container, typename Handler>
    static Call* construct (Context context, BOOST_ASIO_MOVE_ARG(Handler) handler)
    {
        typedef Container <Handler> ContainerType;
        Handler local (BOOST_ASIO_MOVE_CAST(Handler)(handler));
        std::size_t const size (sizeof (ContainerType));
        void* const p = boost_asio_handler_alloc_helpers::
            allocate <Handler> (size, local);
        return ::new (p) ContainerType (context, size,
            BOOST_ASIO_MOVE_CAST(Handler)(local));
    }

    template <template <typename, typename> class Container,
        typename Handler, typename Arg1>
    static Call* construct (Context context, BOOST_ASIO_MOVE_ARG(Handler) handler, Arg1 arg1)
    {
        typedef Container <Handler, Arg1> ContainerType;
        Handler local (BOOST_ASIO_MOVE_CAST(Handler)(handler));
        std::size_t const size (sizeof (ContainerType));
        void* const p = boost_asio_handler_alloc_helpers::
            allocate <Handler> (size, local);
        return ::new (p) ContainerType (context, size,
            BOOST_ASIO_MOVE_CAST(Handler)(local), arg1);
    }

    template <template <typename, typename, typename> class Container,
        typename Handler, typename Arg1, typename Arg2>
    static Call* construct (Context context, BOOST_ASIO_MOVE_ARG(Handler) handler, Arg1 arg1, Arg2 arg2)
    {
        typedef Container <Handler, Arg1, Arg2> ContainerType;
        Handler local (BOOST_ASIO_MOVE_CAST(Handler)(handler));
        std::size_t const size (sizeof (ContainerType));
        void* const p = boost_asio_handler_alloc_helpers::
            allocate <Handler> (size, local);
        return ::new (p) ContainerType (context, size,
            BOOST_ASIO_MOVE_CAST(Handler)(local), arg1, arg2);
    }

    //--------------------------------------------------------------------------
    //
    // Custom Allocator compatible with std::allocator and boost::function
    // which uses the underlying context to allocate memory. This is vastly
    // more efficient in a variety of situations especially during an upcall.
    //
    // The context must be valid for the duration of the invocation.
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

        // Make sure this is the right context!
        //
        explicit Allocator (Context context)
            : m_context (context)
        {
        }

        template <typename U>
        Allocator (Allocator <U> const& other)
            : m_context (other.m_context)
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
            return static_cast <pointer> (m_context.allocate (bytes));
        }

        void deallocate (pointer p, size_type n) const
        {
            size_type const bytes = n * sizeof (value_type);
            m_context.deallocate (p, bytes);
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

        // The context must remain valid for the lifetime of the allocator.
        //
        Context m_context;
    };

    //--------------------------------------------------------------------------
    //
    // Abstract handler wrapper
    //
    struct Call : public SharedObject
    {
    public:
        explicit Call (Context context) noexcept;
        ~Call ();

        Context getContext () const noexcept;

        // Determine if the completion handler is a continuation
        // of a composed operation. It's generally not possible
        // to know this from here, so there's an interface to
        // set the flag.
        //
        // Our asio_handler_is_continuation hook calls this.
        //
        bool is_continuation () const noexcept;
        void set_continuation () noexcept;
        void set_final_continuation () noexcept;

        void operator() ();
        void operator() (error_code const&);
        void operator() (error_code const&, std::size_t);

        virtual void* allocate (std::size_t) = 0;
        virtual void  deallocate (void*, std::size_t) = 0;
        virtual void  destroy () = 0;

        //----------------------------------------------------------------------

    protected:
        void check_continuation () noexcept;

        virtual void dispatch ();
        virtual void dispatch (error_code const&);
        virtual void dispatch (error_code const&, std::size_t);

        static void* pure_virtual_called ();

        Context const m_context;
        bool m_is_continuation;
        bool m_is_final_continuation;

    private:
        // called by Context
        friend struct Context;
        virtual void invoke (invoked_type&) = 0;
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
        // If the passed context is null, we will use the handler itself
        // as the context (this is what normally happens when you pass a
        // handler into an asynchronous function operation).
        //
        // Context must have Call as a base or this will result in
        // undefined behavior.
        //
        CallType (Context context, std::size_t size, BOOST_ASIO_MOVE_ARG(Handler) handler)
            : Call (context)
            , m_size (size)
            , m_handler (BOOST_ASIO_MOVE_CAST(Handler)(handler))
        {
        }

        ~CallType ()
        {

        }

        // Allocate using the original handler as the context.
        //
        void* allocate (std::size_t bytes)
        {
            // If this goes off someone didn't call getContext()!
            bassert (m_context == this);
            return boost_asio_handler_alloc_helpers::
                allocate <Handler> (bytes, m_handler);
        }

        // Deallocate using the handlers context.
        // Note that the original size is required.
        //
        void deallocate (void* p, std::size_t size)
        {
            // If this goes off someone didn't call getContext()!
            bassert (m_context == this);
            boost_asio_handler_alloc_helpers::
                deallocate <Handler> (p, size, m_handler);
        }

        // Invoke the specified function on the handlers context.
        // invoked has this signature void(void).
        //
        void invoke (invoked_type& invoked)
        {
            // If this goes off someone didn't call getContext()!
            bassert (m_context == this);
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

    private:
        std::size_t const m_size;

    protected:
        Handler m_handler;
    };

    //--------------------------------------------------------------------------

    template <typename Handler>
    struct PostCallType : CallType <Handler>
    {
        PostCallType (Context context, std::size_t size, BOOST_ASIO_MOVE_ARG(Handler) handler)
            : CallType <Handler> (context, size, BOOST_ASIO_MOVE_CAST(Handler)(handler))
        {
        }

        void dispatch ()
        {
            this->m_handler ();
        }
    };

    template <typename Handler, typename Arg1>
    struct PostCallType1 : CallType <Handler>
    {
        PostCallType1 (Context context, std::size_t size, BOOST_ASIO_MOVE_ARG(Handler) handler, Arg1 arg1)
            : CallType <Handler> (context, size, BOOST_ASIO_MOVE_CAST(Handler)(handler))
            , m_arg1 (arg1)
        {
        }

        void dispatch ()
        {
            this->m_handler (m_arg1);
        }

        Arg1 m_arg1;
    };

    template <typename Handler, typename Arg1, typename Arg2>
    struct PostCallType2 : CallType <Handler>
    {
        PostCallType2 (Context context, std::size_t size, BOOST_ASIO_MOVE_ARG(Handler) handler, Arg1 arg1, Arg2 arg2)
            : CallType <Handler> (context, size, BOOST_ASIO_MOVE_CAST(Handler)(handler))
            , m_arg1 (arg1)
            , m_arg2 (arg2)
        {
        }

        void dispatch ()
        {
            this->m_handler (m_arg1, m_arg2);
        }

        Arg1 m_arg1;
        Arg2 m_arg2;
    };

    template <typename Handler>
    struct ErrorCallType : CallType <Handler>
    {
        ErrorCallType (Context context, std::size_t size, BOOST_ASIO_MOVE_ARG(Handler) handler)
            : CallType <Handler> (context, size, BOOST_ASIO_MOVE_CAST(Handler)(handler))
        {
        }

        void dispatch (error_code const& ec)
        {
            this->m_handler (ec);
        }
    };

    template <typename Handler>
    struct TransferCallType : CallType <Handler>
    {
        TransferCallType (Context context, std::size_t size, BOOST_ASIO_MOVE_ARG(Handler) handler)
            : CallType <Handler> (context, size, BOOST_ASIO_MOVE_CAST(Handler)(handler))
        {
        }

        void dispatch (error_code const& ec, std::size_t bytes_transferred)
        {
            this->m_handler (ec, bytes_transferred);
        }
    };

private:
    template <class Function>
    friend void  asio_handler_invoke (BOOST_ASIO_MOVE_ARG(Function), HandlerCall*);
    friend void* asio_handler_allocate (std::size_t, HandlerCall*);
    friend void  asio_handler_deallocate (void*, std::size_t, HandlerCall*);

    template <class Function>
    friend void  asio_handler_invoke (BOOST_ASIO_MOVE_ARG(Function), Call*);
    friend void* asio_handler_allocate (std::size_t, Call*);
    friend void  asio_handler_deallocate (void*, std::size_t, Call*);

    friend bool asio_handler_is_continuation (HandlerCall* call);
    friend bool asio_handler_is_continuation (HandlerCall::Call* call);
    friend bool asio_handler_is_continuation (HandlerCall::Context* context);

    SharedObjectPtr <Call> m_call;
};

//------------------------------------------------------------------------------
//
// Specializations
//
//  ContainerDeletePolicy
//  asio_handler_invoke
//  asio_handler_allocate
//  asio_handler_deallocate
//

template <>
struct ContainerDeletePolicy <HandlerCall::Call>
{
    // SharedObjectPtr will use this when
    // the reference count drops to zero.
    //
    static void destroy (HandlerCall::Call* call);
};

template <class Function>
void asio_handler_invoke (BOOST_ASIO_MOVE_ARG(Function) f, HandlerCall::Context* context)
{
    context->invoke (BOOST_ASIO_MOVE_CAST(Function)(f));
}

template <class Function>
void asio_handler_invoke (BOOST_ASIO_MOVE_ARG(Function) f, HandlerCall* call)
{
    // Always go through the call's context.
    call->getContext().invoke (BOOST_ASIO_MOVE_CAST(Function)(f));
}

template <class Function>
void asio_handler_invoke (BOOST_ASIO_MOVE_ARG(Function) f, HandlerCall::Call* call)
{
    // Always go through the call's context.
    call->getContext().invoke (BOOST_ASIO_MOVE_CAST(Function)(f));
}

void* asio_handler_allocate (std::size_t size, HandlerCall* call);
void* asio_handler_allocate (std::size_t size, HandlerCall::Call* call);
void* asio_handler_allocate (std::size_t size, HandlerCall::Context* context);

void  asio_handler_deallocate (void* p, std::size_t size, HandlerCall* call);
void  asio_handler_deallocate (void* p, std::size_t size, HandlerCall::Call* call);
void  asio_handler_deallocate (void* p, std::size_t size, HandlerCall::Context* context);

bool asio_handler_is_continuation (HandlerCall* call);
bool asio_handler_is_continuation (HandlerCall::Call* call);
bool asio_handler_is_continuation (HandlerCall::Context* context);

//------------------------------------------------------------------------------

#endif
