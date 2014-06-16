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

#ifndef BEAST_ASIO_SHARED_HANDLER_H_INCLUDED
#define BEAST_ASIO_SHARED_HANDLER_H_INCLUDED

#include <beast/Config.h>

#include <beast/utility/is_call_possible.h>

#include <boost/utility/base_from_member.hpp>
#include <boost/asio/detail/handler_alloc_helpers.hpp>
#include <boost/asio/detail/handler_cont_helpers.hpp>
#include <boost/asio/detail/handler_invoke_helpers.hpp>

#include <beast/utility/noexcept.h>
#include <functional>
#include <memory>
#include <beast/cxx14/type_traits.h> // <type_traits>

#ifndef BEAST_ASIO_NO_ALLOCATE_SHARED
#define BEAST_ASIO_NO_ALLOCATE_SHARED 0
#endif

#ifndef BEAST_ASIO_NO_HANDLER_RESULT_OF
#define BEAST_ASIO_NO_HANDLER_RESULT_OF 1
#endif

namespace beast {
namespace asio {

class shared_handler_wrapper_base
{
public:
    virtual ~shared_handler_wrapper_base()
    {
    }

    virtual void invoke (std::function <void (void)> f) = 0;
    virtual void* allocate (std::size_t size) = 0;
    virtual void deallocate (void* p, std::size_t size) = 0;
    virtual bool is_continuation () = 0;
};

//------------------------------------------------------------------------------

template <class Signature>
class shared_handler_wrapper_func
    : public shared_handler_wrapper_base
{
private:
    std::function <Signature> m_func;

public:
    template <class Handler>
    explicit shared_handler_wrapper_func (Handler&& handler)
        : m_func (std::ref (std::forward <Handler> (handler)))
    {
    }

    template <class... Args>
#if BEAST_ASIO_NO_HANDLER_RESULT_OF
    void
#else
    std::result_of_t <std::function <Signature> (Args...)>
#endif
    operator() (Args&&... args) const
    {
        return m_func (std::forward <Args> (args)...);
    }
};

//------------------------------------------------------------------------------

namespace detail {

#ifdef _MSC_VER
#pragma warning (push)
#pragma warning (disable: 4512) // assignment operator could not be generated
#endif

template <class Signature, class Handler>
class shared_handler_wrapper
    : private boost::base_from_member <Handler>
    , public shared_handler_wrapper_func <Signature>
{
private:
    typedef boost::base_from_member <Handler> Base;

    BEAST_DEFINE_IS_CALL_POSSIBLE(has_is_continuation, is_continuation);

public:
    shared_handler_wrapper (Handler&& handler)
        : boost::base_from_member <Handler> (std::move (handler))
        , shared_handler_wrapper_func <Signature> (Base::member)
    {
    }

    shared_handler_wrapper (Handler const& handler)
        : boost::base_from_member <Handler> (handler)
        , shared_handler_wrapper_func <Signature> (Base::member)
    {
    }

private:
    void
    invoke (std::function <void (void)> f) override
    {
        return boost_asio_handler_invoke_helpers::
            invoke (f, Base::member);
    }

    void*
    allocate (std::size_t size) override
    {
        return boost_asio_handler_alloc_helpers::
            allocate (size, Base::member);
    }

    void
    deallocate (void* p, std::size_t size) override
    {
        boost_asio_handler_alloc_helpers::
            deallocate (p, size, Base::member);
    }

    bool
    is_continuation () override
    {
        return is_continuation (std::integral_constant <bool,
            has_is_continuation <Handler, bool(void)>::value>());
    }

    bool
    is_continuation (std::true_type)
    {
        return Base::member.is_continuation();
    }

    bool
    is_continuation (std::false_type)
    {
        return boost_asio_handler_cont_helpers::
            is_continuation (Base::member);
    }
};

#ifdef _MSC_VER
#pragma warning (pop)
#endif

template <class T>
struct is_shared_handler : public std::false_type
{
};

//------------------------------------------------------------------------------

template <class T, class Handler>
class handler_allocator
{
private:
    // We want a partial template specialization as a friend
    // but that isn't allowed so we friend all versions. This
    // should produce a compile error if Handler is not constructible
    // from H.
    //
    template <class U, class H>
    friend class handler_allocator;

    Handler m_handler;

public:
    typedef T value_type;
    typedef T* pointer;

    template <class U>
    struct rebind
    {
    public:
        typedef handler_allocator <U, Handler> other;
    };

    handler_allocator() = delete;

    handler_allocator (Handler const& handler)
        : m_handler (handler)
    {
    }

    template <class U>
    handler_allocator (
        handler_allocator <U, Handler> const& other)
        : m_handler (other.m_handler)
    {
    }

    handler_allocator&
    operator= (handler_allocator const&) = delete;

    pointer
    allocate (std::ptrdiff_t n)
    {
        auto const size (n * sizeof (T));
        return static_cast <pointer> (
            boost_asio_handler_alloc_helpers::allocate (
                size, m_handler));
    }

    void
    deallocate (pointer p, std::ptrdiff_t n)
    {
        auto const size (n * sizeof (T));
        boost_asio_handler_alloc_helpers::deallocate (
            p, size, m_handler);
    }

    // Work-around for MSVC not using allocator_traits
    // in the implementation of shared_ptr
    //
#ifdef _MSC_VER
    void
    destroy (T* t)
    {
        t->~T();
    }
#endif

    friend
    bool
    operator== (handler_allocator const& lhs, handler_allocator const& rhs)
    {
        return true;
    }

    friend
    bool
    operator!= (handler_allocator const& lhs, handler_allocator const& rhs)
    {
        return ! (lhs == rhs);
    }
};

}

//------------------------------------------------------------------------------

/** Handler shared reference that provides io_service execution guarantees. */
template <class Signature>
class shared_handler
{
private:
    template <class T>
    friend class shared_handler_allocator;

    typedef shared_handler_wrapper_func <
        Signature> wrapper_type;

    typedef std::shared_ptr <wrapper_type> ptr_type;

    ptr_type m_ptr;

public:
    shared_handler()
    {
    }

    template <
        class DeducedHandler,
        class = std::enable_if_t <
            ! detail::is_shared_handler <
                std::decay_t <DeducedHandler>>::value &&
            std::is_constructible <std::function <Signature>,
                std::decay_t <DeducedHandler>>::value
            >
    >
    shared_handler (DeducedHandler&& handler)
    {
        typedef std::remove_reference_t <DeducedHandler> Handler;

    #if BEAST_ASIO_NO_ALLOCATE_SHARED
        m_ptr = std::make_shared <detail::shared_handler_wrapper <
            Signature, Handler>> (std::forward <DeducedHandler> (handler));
    #else
        m_ptr = std::allocate_shared <detail::shared_handler_wrapper <
            Signature, Handler>> (detail::handler_allocator <char, Handler> (
                handler), std::forward <DeducedHandler> (handler));
    #endif
    }

    shared_handler (shared_handler&& other)
        : m_ptr (std::move (other.m_ptr))
    {
    }

    shared_handler (shared_handler const& other)
        : m_ptr (other.m_ptr)
    {
    }

    shared_handler&
    operator= (std::nullptr_t)
    {
        m_ptr = nullptr;
        return *this;
    }

    shared_handler&
    operator= (shared_handler const& rhs)
    {
        m_ptr = rhs.m_ptr;
        return *this;
    }

    shared_handler&
    operator= (shared_handler&& rhs)
    {
        m_ptr = std::move (rhs.m_ptr);
        return *this;
    }

    explicit
    operator bool() const noexcept
    {
        return m_ptr.operator bool();
    }

    void
    reset()
    {
        m_ptr.reset();
    }

    template <class... Args>
#if BEAST_ASIO_NO_HANDLER_RESULT_OF
    void
#else
    std::result_of_t <std::function <Signature> (Args...)>
#endif
    operator() (Args&&... args) const
    {
        return (*m_ptr)(std::forward <Args> (args)...);
    }

    template <class Function>
    friend
    void
    asio_handler_invoke (Function&& f, shared_handler* h)
    {
        return h->m_ptr->invoke (f);
    }

    friend
    void*
    asio_handler_allocate (
        std::size_t size, shared_handler* h)
    {
        return h->m_ptr->allocate (size);
    }

    friend
    void
    asio_handler_deallocate (
        void* p, std::size_t size, shared_handler* h)
    {
        return h->m_ptr->deallocate (p, size);
    }

    friend
    bool
    asio_handler_is_continuation (
        shared_handler* h)
    {
        return h->m_ptr->is_continuation ();
    }
};

//------------------------------------------------------------------------------

namespace detail {

template <
    class Signature
>
struct is_shared_handler <
    shared_handler <Signature>
> : public std::true_type
{
};

}

//------------------------------------------------------------------------------

template <class T>
class shared_handler_allocator
{
private:
    template <class U>
    friend class shared_handler_allocator;

    std::shared_ptr <shared_handler_wrapper_base> m_ptr;

public:
    typedef T value_type;
    typedef T* pointer;

    shared_handler_allocator() = delete;

    template <class Signature>
    shared_handler_allocator (
        shared_handler <Signature> const& handler)
        : m_ptr (handler.m_ptr)
    {
    }

    template <class U>
    shared_handler_allocator (
        shared_handler_allocator <U> const& other)
        : m_ptr (other.m_ptr)
    {
    }

    pointer
    allocate (std::ptrdiff_t n)
    {
        auto const size (n * sizeof (T));
        return static_cast <pointer> (
            m_ptr->allocate (size));
    }

    void
    deallocate (pointer p, std::ptrdiff_t n)
    {
        auto const size (n * sizeof (T));
        m_ptr->deallocate (p, size);
    }

    friend
    bool
    operator== (shared_handler_allocator const& lhs,
        shared_handler_allocator const& rhs)
    {
        return lhs.m_ptr == rhs.m_ptr;
    }

    friend
    bool
    operator!= (shared_handler_allocator const& lhs,
        shared_handler_allocator const& rhs)
    {
        return ! (lhs == rhs);
    }
};

}
}

#endif
