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

#ifndef BEAST_THREADS_SERVICEQUEUE_H_INCLUDED
#define BEAST_THREADS_SERVICEQUEUE_H_INCLUDED

#include <beast/intrusive/List.h>
#include <beast/threads/SharedData.h>
#include <beast/threads/ThreadLocalValue.h>
#include <beast/threads/WaitableEvent.h>

#include <beast/threads/detail/DispatchedHandler.h>

namespace beast {

namespace detail {

//------------------------------------------------------------------------------

// VFALCO NOTE This allocator is a work in progress

#if 0

class ServiceQueueAllocatorArena : public SharedObject
{
public:
    typedef std::size_t size_type;

    class Page : public LockFreeStack <Page>::Node
    {
    public:
        size_type const m_pageBytes;
        Atomic <int> m_refs;
        char* m_pos;
        bool m_full;
        size_type m_count;

        static std::size_t overhead()
            { return sizeof (Page); }
        
        static std::size_t pointer_overhead()
            { return sizeof (Page*); }

        // pageBytes doesn't include the Page structure
        explicit Page (size_type pageBytes)
            : m_pageBytes (pageBytes)
            , m_pos (begin())
            , m_full (false)
            , m_count (0)
        {
        }

        ~Page ()
        {
            // This means someone forgot to deallocate something
            bassert (!m_full && m_refs.get() == 0);
        }

        static Page* create (size_type pageBytes)
        {
            return new (new std::uint8_t[pageBytes + overhead()]) Page (pageBytes);
        }

        static void destroy (Page* page)
        {
            page->~Page();
            delete[] ((std::uint8_t*)page);
        }

        void reset ()
        {
            m_refs.set (0);
            m_pos = begin();
            m_full = false;
            m_count = 0;
        }

        bool full() const
        {
            return m_full;
        }

        void* allocate (size_type n)
        {
            size_type const needed (n + pointer_overhead());
            char* pos = m_pos + needed;
            if (pos > end())
            {
                m_full = true;
                return nullptr;
            }
            ++m_refs;
            void* p (m_pos + pointer_overhead());
            get_page(p) = this;
            m_pos = pos;
            ++m_count;
            return p;
        }

        char* begin() const
        {
            return const_cast <char*> (
                reinterpret_cast <char const*> (this) + overhead());
        }

        char const* end() const
        {
            return begin() + m_pageBytes;
        }

        // Returns true if the page can be recycled
        bool deallocate (void* p, size_type)
        {
            bool const unused ((--m_refs) == 0);
            return unused && m_full;
        }

        // Returns a reference to the per-allocation overhead area
        static Page*& get_page (void* p)
        {
            return *reinterpret_cast <Page**>(
                static_cast <char*>(p) - pointer_overhead());
        }
    };

    struct State
    {
        State()
        {
        }

        ~State()
        {
            // If this goes off, someone forgot to call deallocate!
            bassert (full.get() == 0);

            destroy (active);
            destroy (recycle);
        }

        void destroy (LockFreeStack <Page>& stack)
        {
            for(;;)
            {
                Page* const page (stack.pop_front());
                if (page == nullptr)
                    break;
                Page::destroy (page);
            }
        }

        Atomic <int> full;
        Atomic <int> page_count;
        LockFreeStack <Page> active;
        LockFreeStack <Page> recycle;
    };

    typedef SharedData <State> SharedState;

    size_type const m_maxBytes;
    SharedState m_state;

    explicit ServiceQueueAllocatorArena (size_type maxBytes = 16 * 1024)
        : m_maxBytes (maxBytes)
    {
    }

    ~ServiceQueueAllocatorArena()
    {
    }

    void* allocate (size_type n)
    {
        SharedState::UnlockedAccess state (m_state);

        // Loop until we satisfy the allocation from an
        // active page, or we run out of active pages.
        //
        for (;;)
        {
            // Acquire ownership of an active page
            // This prevents other threads from seeing it.
            Page* page (state->active.pop_front());
            if (page == nullptr)
                break;

            void* p = page->allocate (n);
            if (p != nullptr)
            {
                // Put the page back so other threads can use it
                state->active.push_front (page);
                return p;
            }

            // Page is full, count it for diagnostics
            ++state->full;
        }

        // No active page, get a recycled page or create a new page.
        //
        Page* page (state->recycle.pop_front());
        if (page == nullptr)
        {
            page = Page::create (std::max (m_maxBytes, n));
            ++state->page_count;
        }

        void* p = page->allocate (n);
        bassert (p != nullptr);
        // Throw page into the active list so other threads can use it
        state->active.push_front (page);
        return p;
    }

    void deallocate (void* p, size_type n)
    {
        SharedState::UnlockedAccess state (m_state);
        Page* const page (Page::get_page(p));
        if (page->deallocate (p, n))
        {
            --state->full;
            page->reset();
            Page::destroy (page);
            //state->recycle.push_front (page);
        }
    }
};

//------------------------------------------------------------------------------

template <typename T>
struct ServiceQueueAllocator
{
    typedef T               value_type;
    typedef T*              pointer;
    typedef T&              reference;
    typedef T const*        const_pointer;
    typedef T const&        const_reference;
    typedef std::size_t     size_type;
    typedef std::ptrdiff_t  difference_type;

    ServiceQueueAllocator ()
        : m_arena (new ServiceQueueAllocatorArena)
    {
    }

    ServiceQueueAllocator (ServiceQueueAllocator const& other)
        : m_arena (other.m_arena)
    {
    }

    template <typename U>
    ServiceQueueAllocator (ServiceQueueAllocator <U> const& other)
        : m_arena (other.m_arena)
    {
    }

    template <typename U>
    struct rebind
    {
        typedef ServiceQueueAllocator <U> other;
    };

    pointer address (reference x) const
    {
        return &x;
    }

    const_pointer address (const_reference x) const
    {
        return &x;
    }

    pointer allocate (size_type n,
        std::allocator<void>::const_pointer = nullptr) const
    {
        size_type const bytes (n * sizeof (value_type));
        pointer const p (static_cast <pointer> (
            m_arena->allocate (bytes)));
        return p;
    }

    void deallocate (pointer p, size_type n) const
    {
        size_type const bytes = (n * sizeof (value_type));
        m_arena->deallocate (p, bytes);
    }

    size_type max_size () const
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
    friend struct ServiceQueueAllocator;

    SharedPtr <ServiceQueueAllocatorArena> m_arena;
};

#endif

}

//------------------------------------------------------------------------------

class ServiceQueueBase
{
public:
    ServiceQueueBase();
    ~ServiceQueueBase();

    std::size_t poll();
    std::size_t poll_one();
    std::size_t run();
    std::size_t run_one();
    void stop();
    bool stopped() const
        { return m_stopped.get() != 0; }
    void reset();

protected:
    class Item;
    class Waiter;
    class ScopedServiceThread;

    void wait();
    virtual void enqueue (Item* item);
	bool empty();

    virtual std::size_t dequeue() = 0;
    virtual Waiter* new_waiter() = 0;

    //--------------------------------------------------------------------------

    class Item : public List <Item>::Node
    {
    public:
        virtual ~Item() { }
        virtual void operator()() = 0;
        virtual std::size_t size() const = 0;
    };

    //--------------------------------------------------------------------------

    class Waiter : public List <Waiter>::Node
    {
    public:
        Waiter()
            { }
        void wait()
            { m_event.wait(); }
        void signal()
            { m_event.signal(); }
    private:
        WaitableEvent m_event;
    };

    //--------------------------------------------------------------------------

    struct State
    {
        // handlers
        List <Item> handlers;
        List <Waiter> waiting;
        List <Waiter> unused;
    };

    typedef SharedData <State> SharedState;
    SharedState m_state;
    Atomic <int> m_stopped;

    static ThreadLocalValue <ServiceQueueBase*> s_service;
};

//------------------------------------------------------------------------------

/** A queue for disatching function calls on other threads.
    Handlers are guaranteed to be called only from threads that are currently
    calling run, run_one, poll, or poll_one.
*/
template <class Allocator = std::allocator <char> >
class ServiceQueueType : public ServiceQueueBase
{
private:
    using ServiceQueueBase::Item;
    using ServiceQueueBase::Waiter;

    template <typename Handler>
    class ItemType : public Item
    {
    public:
        explicit ItemType (BEAST_MOVE_ARG(Handler) handler)
            : m_handler (BEAST_MOVE_CAST(Handler)(handler))
            { }
        void operator() ()
            { m_handler(); }
        std::size_t size() const
            { return sizeof (*this); }
    private:
        Handler m_handler;
    };

public:
    typedef Allocator allocator_type; // for std::uses_allocator<>

    explicit ServiceQueueType (std::size_t expectedConcurrency = 1,
        Allocator alloc = Allocator())
        : m_alloc (alloc)
    {
        typename Allocator::template rebind <Waiter>::other a (m_alloc);
        SharedState::Access state (m_state);
        while (expectedConcurrency--)
            state->unused.push_front (
                *new (a.allocate (1)) Waiter);
    }

    ~ServiceQueueType()
    {
        SharedState::Access state (m_state);

        // Must be empty
        //bassert (state->handlers.empty());

        // Cannot destroy while threads are waiting
        bassert (state->waiting.empty());

        typename Allocator::template rebind <Waiter>::other a (m_alloc);
        while (! state->unused.empty ())
        {
            Waiter* const waiter (&state->unused.front ());
            state->unused.pop_front ();
            a.destroy (waiter);
            a.deallocate (waiter, 1);
        }
    }

    /** Returns the allocator associated with the container. */
    allocator_type get_allocator() const
    {
        return m_alloc;
    }

    /** Returns `true` if the current thread is processing events.
        If the current thread of execution is inside a call to run,
        run_one, poll, or poll_one, this function returns `true`.
    */
    bool is_service_thread() const
        { return s_service.get() == this; }

    /** Run the handler on a service thread.
        If the current thread of execution is a service thread then this
        function wil dispatch the handler on the caller's thread before
        returning.       
        The function signature of the handler must be:
        @code
            void handler(); 
        @endcode
    */
    template <typename Handler>
    void dispatch (BEAST_MOVE_ARG(Handler) handler)
    {
        if (is_service_thread())
        {
            handler();
        }
        else
        {
            typename Allocator::template rebind <ItemType <Handler> >::other a (m_alloc);
            enqueue (new (a.allocate (1))
                ItemType <Handler> (BEAST_MOVE_CAST(Handler)(handler)));
        }
    }

    /** Request the handler to run on a service thread.
        This returns immediately, even if the current thread of execution is
        a service thread.
        The function signature of the handler must be:
        @code
            void handler(); 
        @endcode
    */
    template <typename Handler>
    void post (BEAST_MOVE_ARG(Handler) handler)
    {
        typename Allocator::template rebind <ItemType <Handler> >::other a (m_alloc);
        enqueue (new (a.allocate (1))
            ItemType <Handler> (BEAST_MOVE_CAST(Handler)(handler)));
    }

    /** Return a new handler that dispatches the wrapped handler on the queue. */
    template <typename Handler>
    detail::DispatchedHandler <ServiceQueueType&, Handler> wrap (
        BEAST_MOVE_ARG(Handler) handler)
    {
        return detail::DispatchedHandler <ServiceQueueType&, Handler> (
            *this, BEAST_MOVE_CAST(Handler)(handler));
    }

    /** Run the event loop to execute ready handlers.
        This runs handlers that are ready to run, without blocking, until
        there are no more handlers ready or the service queue has been stopped.
        @return The number of handlers that were executed.
    */
    std::size_t poll ()
        { return ServiceQueueBase::poll(); }

    /** Run the event loop to execute at most one ready handler.
        This will run zero or one handlers, without blocking, depending on
        whether or not there is handler immediately ready to run.
        @return The number of handlers that were executed.
    */
    std::size_t poll_one ()
        { return ServiceQueueBase::poll_one(); }

    /** Runs the queue's processing loop.
        The current thread of execution becomes a service thread. This call
        blocks until there is no more work remaining.
        @return The number of handlers that were executed.
    */
    std::size_t run ()
        { return ServiceQueueBase::run(); }

    /** Runs the queue's processing loop to execute at most one handler.
        @return The number of handlers that were executed.
    */
    std::size_t run_one ()
        { return ServiceQueueBase::run_one(); }

    /** Stop the queue's processing loop.
        All threads executing run or run_one will return as soon as possible.
        Future calls to run, run_one, poll, or poll_one will return immediately
        until reset is called.
        @see reset
    */
    void stop()
        { return ServiceQueueBase::stop(); }

    /** Returns `true` if the queue has been stopped.
        When a queue is stopped, calls to run, run_one, poll, or poll_one will
        return immediately without invoking any handlers.
    */
    bool stopped() const
        { return ServiceQueueBase::stopped(); }

    /** Reset the queue after a stop.
        This allows the event loop to be restarted. This may not be called while
        there are any threads currently executing the run, run_one, poll, or
        poll_one functions, or undefined behavior will result.
    */
    void reset()
        { return ServiceQueueBase::reset(); }

private:
    // Dispatch a single queued handler if possible.
    // Returns the number of handlers dispatched (0 or 1)
    //
    std::size_t dequeue ()
    {
        if (stopped())
            return 0;

        Item* item (nullptr);

        {
            SharedState::Access state (m_state);
            if (state->handlers.empty())
                return 0;
            item = &state->handlers.front();
            state->handlers.erase (
                state->handlers.iterator_to (*item));
        }

        (*item)();

        typename Allocator::template rebind <std::uint8_t>::other a (m_alloc);
        std::size_t const size (item->size());
        item->~Item();
        a.deallocate (reinterpret_cast<std::uint8_t*>(item), size);
        return 1;
    }

    // Create a new Waiter
    Waiter* new_waiter()
    {
        typename Allocator::template rebind <Waiter>::other a (m_alloc);
        return new (a.allocate (1)) Waiter;
    }

    Allocator m_alloc;
};

typedef ServiceQueueType <std::allocator <char> > ServiceQueue;

}

#endif
